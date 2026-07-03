#!/bin/bash
# Pin a process to a CPU core with real-time scheduling

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <core> <command...>"
    exit 1
fi

CORE=$1
shift

# Pin to core and set real-time FIFO priority
exec taskset -c "$CORE" chrt -f 99 "$@"
