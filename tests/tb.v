// Multiplies two numbers with the generated karatsuba_mul.
//
//   build/karatsuba_gen 32
//   iverilog -g2005 -P tb.N=32 -o tb.vvp karatsuba_mul.v tests/tb.v
//   vvp -n tb.vvp +a=123456789 +b=987654321
//
// N must match the width given to karatsuba_gen.

`timescale 1ns / 1ps

module tb;
    parameter integer N = 32;

    reg clk = 1'b0;
    reg [N-1:0] a;
    reg [N-1:0] b;
    wire [2*N-1:0] p;
    wire [2*N-1:0] expected = a * b;

    karatsuba_mul dut (.clk(clk), .a(a), .b(b), .p(p));

    always #5 clk = ~clk;

    initial begin
        if (!$value$plusargs("a=%d", a)) a = 12345;
        if (!$value$plusargs("b=%d", b)) b = 6789;
        repeat (3) @(posedge clk);  // the product appears after the 3rd edge
        #1;
        $display("%0d * %0d = %0d", a, b, p);
        if (p === expected)
            $display("OK");
        else
            $display("ERROR: expected %0d", expected);
        $finish(0);
    end
endmodule
