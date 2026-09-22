// Self checking testbench for the generated multiplier karatsuba_mul.
//
//   build/karatsuba_gen 16 -o mul.v
//   iverilog -g2005 -P tb.N=16 -o tb.vvp mul.v tests/tb.v
//   vvp tb.vvp [+nvec=K] [+seed=S]
//
// N must match the width given to the generator. The last line printed is
// "RESULT: PASS" or "RESULT: FAIL". Compile with -DDUMP_VCD to write tb.vcd.
//
// Checks:
//   1. a single operand pair gives its result exactly 3 clocks later;
//   2. corner values, every operand pair (N <= 8) and random values, applied
//      on every clock and then with random gaps in in_valid; on every clock
//      p and out_valid are compared with a reference model.

`timescale 1ns / 1ps

module tb;
    parameter integer N = 8;
    localparam integer LAT = 3;           // required latency
    localparam integer LO = (N + 1) / 2;  // split point of the top level step

    reg clk = 1'b0;
    reg rst_n = 1'b0;
    reg in_valid = 1'b0;
    reg [N-1:0] a = 0;
    reg [N-1:0] b = 0;
    wire out_valid;
    wire [2*N-1:0] p;

    karatsuba_mul dut (.clk(clk), .rst_n(rst_n), .in_valid(in_valid),
                       .a(a), .b(b), .out_valid(out_valid), .p(p));

    always #5 clk = ~clk;

`ifdef DUMP_VCD
    initial begin
        $dumpfile("tb.vcd");
        $dumpvars(0, tb);
    end
`endif

    // Reference model: a*b and in_valid delayed by LAT clocks.
    reg [2*N-1:0] ref_p [0:LAT-1];
    reg [N-1:0]   ref_a [0:LAT-1];
    reg [N-1:0]   ref_b [0:LAT-1];
    reg           ref_v [0:LAT-1];
    integer i;
    always @(posedge clk) begin
        for (i = LAT - 1; i > 0; i = i - 1) begin
            ref_p[i] <= ref_p[i-1];
            ref_a[i] <= ref_a[i-1];
            ref_b[i] <= ref_b[i-1];
            ref_v[i] <= rst_n & ref_v[i-1];
        end
        ref_p[0] <= a * b;
        ref_a[0] <= a;
        ref_b[0] <= b;
        ref_v[0] <= rst_n & in_valid;
    end

    // Compare the outputs with the model half a clock after they change.
    integer errors = 0;
    integer checks = 0;
    always @(negedge clk) begin
        if (rst_n && out_valid !== ref_v[LAT-1]) begin
            errors = errors + 1;
            if (errors <= 20)
                $display("ERROR %0t: out_valid = %b, expected %b", $time, out_valid, ref_v[LAT-1]);
        end else if (rst_n && out_valid) begin
            checks = checks + 1;
            if (p !== ref_p[LAT-1]) begin
                errors = errors + 1;
                if (errors <= 20)
                    $display("ERROR %0t: %0h * %0h = %0h, expected %0h",
                             $time, ref_a[LAT-1], ref_b[LAT-1], p, ref_p[LAT-1]);
            end
        end
    end

    // Apply one operand pair; it is sampled by the next rising edge.
    task send(input [N-1:0] x, input [N-1:0] y, input v);
        begin
            @(negedge clk);
            a <= x;
            b <= y;
            in_valid <= v;
        end
    endtask

    // Random N bit value built from 32 bit pieces. $random is signed, so each
    // piece goes through the unsigned reg r first and is never sign extended.
    integer seed = 32'h12345678;
    function [N-1:0] random_value(input dummy);
        integer j;
        reg [31:0] r;
        begin
            random_value = 0;
            for (j = 0; j < N; j = j + 32) begin
                r = $random(seed);
                random_value = (random_value << 32) | r;
            end
        end
    endfunction

    integer nvec = 256;
    integer k;
    integer m;
    integer clocks;
    reg [N-1:0] max_value;
    reg [N-1:0] split_value;
    reg [N-1:0] pattern;
    initial begin
        if ($value$plusargs("nvec=%d", nvec)) $display("note: nvec = %0d", nvec);
        if ($value$plusargs("seed=%d", seed)) $display("note: seed = %0d", seed);
        max_value = {N{1'b1}};
        split_value = 1;
        split_value = split_value << LO;
        for (k = 0; k < N; k = k + 1) pattern[k] = (k % 2 == 0);  // ...0101
        $display("testbench for karatsuba_mul, N = %0d", N);
        repeat (3) @(negedge clk);
        rst_n <= 1'b1;

        // 1. latency: apply one pair and count rising edges until out_valid
        send(max_value, max_value, 1);
        send(0, 0, 0);  // the edge before this call sampled the pair
        clocks = 1;
        while (!out_valid && clocks < 16) begin
            @(negedge clk);
            clocks = clocks + 1;
        end
        if (clocks == LAT) begin
            $display(" latency    : %0d clocks, as required", clocks);
        end else begin
            errors = errors + 1;
            $display("ERROR: latency is %0d clocks, expected %0d", clocks, LAT);
        end
        repeat (LAT) @(negedge clk);

        // 2. corner values, one pair per clock
        send(0, 0, 1);
        send(0, max_value, 1);
        send(max_value, 0, 1);
        send(1, 1, 1);
        send(1, max_value, 1);
        send(max_value, max_value, 1);
        send(max_value - 1, max_value, 1);
        send(pattern, ~pattern, 1);
        send(pattern, pattern, 1);
        send(split_value, split_value, 1);
        send(split_value - 1, split_value - 1, 1);
        send(split_value - 1, split_value, 1);
        send(split_value + 1, split_value + 1, 1);
        $display(" corners    : done");

        // 3. every operand pair for small N
        if (N <= 8) begin
            for (k = 0; k < (1 << N); k = k + 1)
                for (m = 0; m < (1 << N); m = m + 1)
                    send(k, m, 1);
            $display(" exhaustive : %0d pairs", 1 << (2 * N));
        end

        // 4. random values, one pair per clock, then with random gaps
        for (k = 0; k < nvec; k = k + 1) send(random_value(0), random_value(0), 1);
        for (k = 0; k < nvec; k = k + 1) send(random_value(0), random_value(0), $random(seed));
        $display(" random     : %0d pairs, then %0d with gaps", nvec, nvec);

        send(0, 0, 0);
        repeat (LAT + 2) @(negedge clk);
        $display(" N = %0d, products checked = %0d, errors = %0d", N, checks, errors);
        if (errors == 0)
            $display("RESULT: PASS");
        else
            $display("RESULT: FAIL");
        $finish;
    end
endmodule
