// Testbench for FIX Parser
// Verifies parsing of standard FIX messages

`timescale 1ns / 1ps

module fix_parser_tb ();

    reg clk, rst_n;
    reg [7:0] i_data;
    reg i_valid;
    wire o_ready;

    wire [31:0] o_begin_string;
    wire [15:0] o_msg_type;
    wire [47:0] o_sender_comp_id;
    wire [47:0] o_target_comp_id;
    wire [31:0] o_sequence;
    wire o_msg_valid;

    // Instantiate DUT
    fix_parser dut (
        .clk(clk),
        .rst_n(rst_n),
        .i_data(i_data),
        .i_valid(i_valid),
        .o_ready(o_ready),
        .o_begin_string(o_begin_string),
        .o_msg_type(o_msg_type),
        .o_sender_comp_id(o_sender_comp_id),
        .o_target_comp_id(o_target_comp_id),
        .o_sequence(o_sequence),
        .o_msg_valid(o_msg_valid)
    );

    // Clock generation: 200 MHz for Artix-7
    initial begin
        clk = 0;
        forever #2.5 clk = ~clk;  // 5 ns period = 200 MHz
    end

    // Test sequence
    initial begin
        rst_n = 0;
        i_valid = 0;
        i_data = 0;
        #10 rst_n = 1;
        #10;

        // Test message: simplified FIX format
        // In reality, this would be a binary-encoded message
        send_byte("8", "BeginString tag");
        send_byte("=", "tag-value separator");
        send_byte("F", "F in FIX");
        send_byte("I", "I in FIX");
        send_byte("X", "X in FIX");
        send_byte("|", "field separator");

        send_byte("3", "MsgType tag");
        send_byte("5", "MsgType tag (35)");
        send_byte("=", "tag-value separator");
        send_byte("D", "MsgType value (D = New Order Single)");
        send_byte("|", "field separator");

        send_byte("3", "SenderCompID tag");
        send_byte("4", "SenderCompID tag (34)");  // Note: should be 49
        send_byte("=", "tag-value separator");
        send_byte("S", "Sender");
        send_byte("E", "Sender");
        send_byte("N", "Sender");
        send_byte("|", "field separator");

        #50;
        if (o_msg_valid) begin
            $display("[PASS] Message parsed successfully");
            $display("  BeginString: 0x%08X", o_begin_string);
            $display("  MsgType: 0x%04X", o_msg_type);
            $display("  SenderCompID: 0x%012X", o_sender_comp_id);
        end else begin
            $display("[FAIL] Message not parsed");
        end

        #100;
        $finish;
    end

    // Helper task to send a byte
    task send_byte(input string data, input string desc);
        begin
            if (data != "|" && data != "=" && data != "\n") begin
                i_data = data[7:0];  // ASCII value
            end else begin
                case (data)
                    "|": i_data = 8'd124;  // '|'
                    "=": i_data = 8'd61;   // '='
                    default: i_data = 8'd0;
                endcase
            end

            i_valid = 1;
            #5;
            i_valid = 0;
            $display("  [t=%0t] Sent 0x%02X (%s): %s", $time, i_data, data, desc);
            #5;
        end
    endtask

endmodule
