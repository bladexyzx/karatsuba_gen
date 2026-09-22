/**
 * @file generator.cpp
 * @brief Emits the synthesisable RTL (TOP is the top level name):
 *        - `TOP_leaf_wK`: K x K combinational leaf multiplier (K <= base);
 *        - `TOP_kara_wK`: K x K combinational Karatsuba step (K > base);
 *        - `TOP`: pipelined top level, its own Karatsuba step is spread over
 *          three register stages (latency 3, one result per clock).
 */
#include "generator.h"

#include <set>
#include <sstream>
#include <stdexcept>

namespace karatsuba {
namespace {

/// @brief One Verilog signal: a wire or a pipeline register.
struct Sig {
    std::string name;  ///< Signal name.
    int width;         ///< Width in bits.
    std::string expr;  ///< Value assigned to the wire or loaded into the register.
};

/// @brief Decimal text of v.
/// @param v Number to print.
/// @return v as a string.
std::string num(int v) { return std::to_string(v); }

/// @brief Verilog bit range of a w bit vector.
/// @param w Width, at least 1.
/// @return "[w-1:0]".
std::string rng(int w) { return "[" + num(w - 1) + ":0]"; }

/// @brief Verilog constant of n zero bits.
/// @param n Number of bits, at least 1.
/// @return "1'b0" or "{n{1'b0}}".
std::string zeros(int n) {
    if (n == 1) return "1'b0";
    return "{" + num(n) + "{1'b0}}";
}

/**
 * @brief Shifts a Verilog expression left and zero extends it; built from
 *        concatenations so that every width stays explicit.
 * @param e Expression of width from.
 * @param from Width of e.
 * @param sh Shift amount, at least 0.
 * @param to Width of the result.
 * @return Expression of exactly to bits equal to e << sh.
 * @throws std::logic_error if the shifted value does not fit into to bits.
 */
std::string shl(const std::string& e, int from, int sh, int to) {
    int pad = to - from - sh;
    if (pad < 0) throw std::logic_error("shl: result does not fit");
    if (sh == 0 && pad == 0) return e;
    std::string s = e;
    if (sh > 0) s = s + ", " + zeros(sh);
    if (pad > 0) s = zeros(pad) + ", " + s;
    return "{" + s + "}";
}

/// @brief Zero extends an expression.
/// @param e Expression of width from.
/// @param from Width of e.
/// @param to Width of the result, at least from.
/// @return Expression of exactly to bits.
std::string zext(const std::string& e, int from, int to) { return shl(e, from, 0, to); }

/// @brief Horizontal rule used to separate modules.
/// @return A comment line of dashes.
std::string rule() { return "// " + std::string(75, '-') + "\n"; }

/// @brief Declares combinational wires.
/// @param os Output stream.
/// @param sigs Wires with their values.
void emit_wires(std::ostringstream& os, const std::vector<Sig>& sigs) {
    for (const Sig& s : sigs) {
        os << "    wire " << rng(s.width) << " " << s.name << " = " << s.expr << ";\n";
    }
    os << "\n";
}

/// @brief Declares one pipeline stage: registers loaded on every rising edge.
/// @param os Output stream.
/// @param sigs Registers with their next values.
void emit_stage(std::ostringstream& os, const std::vector<Sig>& sigs) {
    for (const Sig& s : sigs) {
        os << "    reg  " << rng(s.width) << " " << s.name << ";\n";
    }
    os << "    always @(posedge clk) begin\n";
    for (const Sig& s : sigs) {
        os << "        " << s.name << " <= " << s.expr << ";\n";
    }
    os << "    end\n\n";
}

/// @brief Emits the comment line in front of one phase of a Karatsuba step.
/// @param os Output stream.
/// @param pipe true for the pipelined top level, which also prints the stage.
/// @param stage Stage number 1..3.
/// @param text Description of the phase.
void emit_phase(std::ostringstream& os, bool pipe, int stage, const std::string& text) {
    os << "    // ";
    if (pipe) os << "stage " << stage << ": ";
    os << text << "\n";
}

/**
 * @brief Emits the body of one Karatsuba step on the w bit ports a, b.
 * @param os Output stream.
 * @param opt Generator settings.
 * @param w Operand width, at least 2.
 * @param pipe true: split, multiplication and recombination are separated by
 *        registers (top level); false: purely combinational.
 */
void emit_step(std::ostringstream& os, const Options& opt, int w, bool pipe) {
    Split s = split_of(w);
    int pw = 2 * w;  // width of the product

    // Phase 1: halves of the operands and their sums.
    std::string a_low = "a" + rng(s.lo);
    std::string b_low = "b" + rng(s.lo);
    std::string a_high = "a[" + num(w - 1) + ":" + num(s.lo) + "]";
    std::string b_high = "b[" + num(w - 1) + ":" + num(s.lo) + "]";
    std::string a_sum = zext(a_low, s.lo, s.mid) + " + " + zext(a_high, s.hi, s.mid);
    std::string b_sum = zext(b_low, s.lo, s.mid) + " + " + zext(b_high, s.hi, s.mid);
    std::vector<Sig> halves = {{"a0", s.lo, a_low},  {"a1", s.hi, a_high}, {"b0", s.lo, b_low},
                               {"b1", s.hi, b_high}, {"sa", s.mid, a_sum}, {"sb", s.mid, b_sum}};
    emit_phase(os, pipe, 1, "split, pre-additions");
    if (pipe) {
        emit_stage(os, halves);
    } else {
        emit_wires(os, halves);
    }

    // Phase 2: three half width products. In the pipelined case the
    // sub-multipliers drive m0, m2, mm, which are registered as z0, z2, zm.
    std::string out = "z";
    if (pipe) out = "m";
    emit_phase(os, pipe, 2, "three half width products");
    os << "    wire " << rng(2 * s.lo) << " " << out << "0;\n";
    os << "    wire " << rng(2 * s.hi) << " " << out << "2;\n";
    os << "    wire " << rng(2 * s.mid) << " " << out << "m;\n";
    os << "    " << mul_module(opt, s.lo) << " u_z0 (.a(a0), .b(b0), .p(" << out << "0));\n";
    os << "    " << mul_module(opt, s.hi) << " u_z2 (.a(a1), .b(b1), .p(" << out << "2));\n";
    os << "    " << mul_module(opt, s.mid) << " u_zm (.a(sa), .b(sb), .p(" << out << "m));\n\n";
    if (pipe) {
        emit_stage(os, {{"z0", 2 * s.lo, "m0"}, {"z2", 2 * s.hi, "m2"}, {"zm", 2 * s.mid, "mm"}});
    }

    // Phase 3: z1 = a1*b0 + a0*b1 < 2^(w+1), so the dropped high bits of zd are zero.
    emit_phase(os, pipe, 3, "z1 = zm - z2 - z0, recombination");
    os << "    /* verilator lint_off UNUSED */\n";
    os << "    wire " << rng(2 * s.mid) << " zd = zm - " << zext("z2", 2 * s.hi, 2 * s.mid) << " - "
       << zext("z0", 2 * s.lo, 2 * s.mid) << ";\n";
    os << "    /* verilator lint_on UNUSED */\n";
    os << "    wire " << rng(w + 1) << " z1 = zd" << rng(w + 1) << ";\n";
    std::string product = shl("z2", 2 * s.hi, 2 * s.lo, pw) + "\n            + " +
                          shl("z1", w + 1, s.lo, pw) + "\n            + " +
                          zext("z0", 2 * s.lo, pw);
    if (pipe) {
        emit_stage(os, {{"pr", pw, product}});
        os << "    assign p = pr;\n";
    } else {
        os << "    assign p = " << product << ";\n";
    }
}

/// @brief Emits the module header with the operand and product ports.
/// @param os Output stream.
/// @param name Module name.
/// @param w Operand width.
/// @param comment Description placed above the module.
/// @param extra_ports Port declarations placed before a, b, p.
void emit_header(std::ostringstream& os, const std::string& name, int w, const std::string& comment,
                 const std::string& extra_ports) {
    os << rule() << comment << rule();
    os << "module " << name << " (\n";
    os << extra_ports;
    os << "    input  wire " << rng(w) << " a,\n";
    os << "    input  wire " << rng(w) << " b,\n";
    os << "    output wire " << rng(2 * w) << " p\n";
    os << ");\n";
}

/// @brief Emits a w x w combinational leaf multiplier.
/// @param os Output stream.
/// @param opt Generator settings.
/// @param w Operand width, 1..base.
void emit_leaf(std::ostringstream& os, const Options& opt, int w) {
    std::string comment = "// " + num(w) + " x " + num(w) + " combinational leaf multiplier\n";
    emit_header(os, mul_module(opt, w), w, comment, "");
    if (opt.leaf_star) {
        os << "    assign p = a * b;\n";
    } else {
        // Long multiplication: sum of the partial products (b[i] ? a : 0) << i.
        os << "    assign p = ";
        for (int i = 0; i < w; ++i) {
            if (i > 0) os << "\n             + ";
            std::string partial = "({" + num(w) + "{b[" + num(i) + "]}} & a)";
            os << shl(partial, w, i, 2 * w);
        }
        os << ";\n";
    }
    os << "endmodule\n\n";
}

/// @brief Emits a w x w combinational Karatsuba step module.
/// @param os Output stream.
/// @param opt Generator settings.
/// @param w Operand width, greater than base.
void emit_kara(std::ostringstream& os, const Options& opt, int w) {
    std::string comment = "// " + num(w) + " x " + num(w) + " combinational Karatsuba step\n";
    emit_header(os, mul_module(opt, w), w, comment, "");
    emit_step(os, opt, w, false);
    os << "endmodule\n\n";
}

/// @brief Emits the pipelined top level module.
/// @param os Output stream.
/// @param opt Generator settings.
void emit_top(std::ostringstream& os, const Options& opt) {
    int n = opt.n;
    std::string comment =
        "// " + opt.top + ": pipelined unsigned multiplier " + num(n) + " x " + num(n) + " -> " +
        num(2 * n) + "\n" +
        "//   a, b are sampled on a rising edge of clk; p = a*b and out_valid appear\n"
        "//   after the 3rd rising edge.  New operands may be applied every clock.\n"
        "//   rst_n: synchronous, active low, clears the valid chain only.\n";
    std::string control_ports =
        "    input  wire clk,\n"
        "    input  wire rst_n,\n"
        "    input  wire in_valid,\n"
        "    output wire out_valid,\n";
    emit_header(os, opt.top, n, comment, control_ports);

    // out_valid is in_valid delayed by three clocks
    os << "    reg [2:0] vld;\n";
    os << "    always @(posedge clk) vld <= rst_n ? {vld[1:0], in_valid} : 3'b000;\n";
    os << "    assign out_valid = vld[2];\n\n";

    if (n >= 2) {
        emit_step(os, opt, n, true);
    } else {
        // 1 bit: the product is a & b, delayed by three registers
        emit_stage(os, {{"s1", 1, "a & b"}});
        emit_stage(os, {{"s2", 1, "s1"}});
        emit_stage(os, {{"s3", 1, "s2"}});
        os << "    assign p = {1'b0, s3};\n";
    }
    os << "endmodule\n";
}

/// Names the top level module may not have: the reserved words of
/// SystemVerilog (IEEE 1800-2017, a superset of Verilog-2005), Icarus' "bool",
/// and the signal names used inside the generated top level.
const char kReserved[] =
    " accept_on alias always always_comb always_ff always_latch and assert assign assume"
    " automatic before begin bind bins binsof bit bool break buf bufif0 bufif1 byte case"
    " casex casez cell chandle checker class clocking cmos config const constraint context"
    " continue cover covergroup coverpoint cross deassign default defparam design disable"
    " dist do edge else end endcase endchecker endclass endclocking endconfig endfunction"
    " endgenerate endgroup endinterface endmodule endpackage endprimitive endprogram"
    " endproperty endspecify endsequence endtable endtask enum event eventually expect"
    " export extends extern final first_match for force foreach forever fork forkjoin"
    " function generate genvar global highz0 highz1 if iff ifnone ignore_bins illegal_bins"
    " implements implies import incdir include initial inout input inside instance int"
    " integer interconnect interface intersect join join_any join_none large let liblist"
    " library local localparam logic longint macromodule matches medium modport module"
    " nand negedge nettype new nexttime nmos nor noshowcancelled not notif0 notif1 null or"
    " output package packed parameter pmos posedge primitive priority program property"
    " protected pull0 pull1 pulldown pullup pulsestyle_ondetect pulsestyle_onevent pure"
    " rand randc randcase randsequence rcmos real realtime ref reg reject_on release repeat"
    " restrict return rnmos rpmos rtran rtranif0 rtranif1 s_always s_eventually s_nexttime"
    " s_until s_until_with scalared sequence shortint shortreal showcancelled signed small"
    " soft solve specify specparam static string strong strong0 strong1 struct super"
    " supply0 supply1 sync_accept_on sync_reject_on table tagged task this throughout time"
    " timeprecision timeunit tran tranif0 tranif1 tri tri0 tri1 triand trior trireg type"
    " typedef union unique unique0 unsigned until until_with untyped use uwire var vectored"
    " virtual void wait wait_order wand weak weak0 weak1 while wildcard wire with within"
    " wor xnor xor"
    " a b p clk rst_n in_valid out_valid vld a0 a1 b0 b1 sa sb m0 m2 mm z0 z2 zm zd z1 pr"
    " s1 s2 s3 u_z0 u_z2 u_zm ";

}  // namespace

bool valid_module_name(const std::string& s) {
    if (s.empty()) return false;
    if (s[0] >= '0' && s[0] <= '9') return false;
    for (char c : s) {
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        bool digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != '_') return false;
    }
    // words in kReserved are separated by spaces, so " name " finds whole words only
    std::string reserved = kReserved;
    return reserved.find(" " + s + " ") == std::string::npos;
}

void validate(const Options& opt) {
    if (opt.n < 1 || opt.n > kMaxWidth) {
        throw std::invalid_argument("N must be 1.." + num(kMaxWidth) + ", got " + num(opt.n));
    }
    if (opt.base < kMinBase || opt.base > kMaxBase) {
        throw std::invalid_argument("leaf width must be " + num(kMinBase) + ".." + num(kMaxBase) +
                                    ", got " + num(opt.base));
    }
    if (!valid_module_name(opt.top)) {
        throw std::invalid_argument("'" + opt.top + "' is not a usable Verilog module name");
    }
}

Split split_of(int w) {
    if (w < 2) throw std::invalid_argument("split_of: width must be at least 2");
    Split s;
    s.lo = (w + 1) / 2;
    s.hi = w - s.lo;
    s.mid = s.lo + 1;
    return s;
}

std::string mul_module(const Options& opt, int w) {
    if (w <= opt.base) return opt.top + "_leaf_w" + num(w);
    return opt.top + "_kara_w" + num(w);
}

std::vector<int> required_widths(const Options& opt) {
    validate(opt);
    std::set<int> need;                     // found widths, std::set keeps them sorted
    std::vector<int> todo;                  // widths that still have to be split
    if (opt.n >= 2) todo.push_back(opt.n);  // the top level splits N itself
    while (!todo.empty()) {
        int w = todo.back();
        todo.pop_back();
        Split s = split_of(w);
        std::vector<int> parts = {s.lo, s.hi, s.mid};
        for (int part : parts) {
            bool is_new = need.count(part) == 0;
            need.insert(part);
            if (is_new && part > opt.base) todo.push_back(part);
        }
    }
    return std::vector<int>(need.begin(), need.end());
}

std::string generate_rtl(const Options& opt) {
    validate(opt);
    std::ostringstream os;
    std::string leaves = "shift-add";
    if (opt.leaf_star) leaves = "'*' operator";
    os << "// Unsigned " << opt.n << " x " << opt.n << " -> " << 2 * opt.n
       << " multiplier, Karatsuba algorithm, 3 stage pipeline.\n";
    os << "// Generated by karatsuba_gen (leaf width " << opt.base << ", " << leaves
       << " leaves). Do not edit.\n\n";
    os << "`timescale 1ns / 1ps\n";
    os << "`default_nettype none\n";
    os << "/* verilator lint_off DECLFILENAME */\n\n";

    std::vector<int> widths = required_widths(opt);
    for (int w : widths) {
        if (w <= opt.base) {
            emit_leaf(os, opt, w);
        } else {
            emit_kara(os, opt, w);
        }
    }
    emit_top(os, opt);

    os << "\n/* verilator lint_on DECLFILENAME */\n";
    os << "`default_nettype wire\n";
    return os.str();
}

}  // namespace karatsuba
