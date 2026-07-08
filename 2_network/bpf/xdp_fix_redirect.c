// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * XDP program to redirect FIX protocol packets to AF_XDP socket
 *
 * This program runs in the kernel and redirects incoming packets on a
 * specific UDP port to an XDP socket (XSK) for zero-copy userspace processing.
 *
 * Compile: clang -O2 -target bpf -c xdp_fix_redirect.c -o xdp_fix_redirect.o
 */

#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/udp.h>
#include <bpf/bpf_helpers.h>

#define FIX_PORT 9876  // Standard FIX port (override in production)

// XDP socket map: kernel uses this to redirect packets
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __uint(key_size, 4);
    __uint(value_size, 4);
} xsks_map SEC(".maps");

// Parser for Ethernet frame to extract UDP port
static __always_inline int parse_ethernet(void *data, uint64_t nh_off,
                                          void *data_end, __u16 *eth_proto) {
    struct ethhdr *eth = data + nh_off;

    if ((void *)(eth + 1) > data_end)
        return -1;

    *eth_proto = eth->h_proto;
    return 0;
}

static __always_inline int parse_ipv4(void *data, uint64_t nh_off,
                                      void *data_end, __u8 *protocol) {
    struct iphdr *iph = data + nh_off;

    if ((void *)(iph + 1) > data_end)
        return -1;

    *protocol = iph->protocol;
    return 0;
}

static __always_inline int parse_udp(void *data, uint64_t nh_off,
                                     void *data_end, __u16 *dport) {
    struct udphdr *udph = data + nh_off;

    if ((void *)(udph + 1) > data_end)
        return -1;

    *dport = bpf_ntohs(udph->dest);
    return 0;
}

// Main XDP program: redirect FIX packets to XSK
SEC("xdp")
int xdp_fix_redirect(struct xdp_md *ctx) {
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    __u16 eth_proto = 0;
    __u8 ip_proto = 0;
    __u16 udp_dport = 0;

    // Parse Ethernet
    if (parse_ethernet(data, 0, data_end, &eth_proto) < 0)
        return XDP_PASS;

    if (eth_proto != ETH_P_IP)
        return XDP_PASS;

    // Parse IPv4
    if (parse_ipv4(data, sizeof(struct ethhdr), data_end, &ip_proto) < 0)
        return XDP_PASS;

    if (ip_proto != IPPROTO_UDP)
        return XDP_PASS;

    // Parse UDP
    if (parse_udp(data, sizeof(struct ethhdr) + sizeof(struct iphdr),
                  data_end, &udp_dport) < 0)
        return XDP_PASS;

    // Redirect to XSK if destination port matches
    if (udp_dport == FIX_PORT) {
        return bpf_redirect_map(&xsks_map, 0, XDP_DROP);
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "Dual BSD/GPL";
