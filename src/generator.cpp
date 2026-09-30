/**
 * @file generator.cpp
 * @brief Emits the synthesisable RTL:
 *        - `karatsuba_mul_leaf_wK`: K x K combinational leaf, long
 *          multiplication from bitwise AND and adders (K <= kLeafWidth);
 *        - `karatsuba_mul_kara_wK`: K x K combinational Karatsuba step;
 *        - `karatsuba_mul`: pipelined top level, its own Karatsuba step is
 *          spread over three register stages (latency 3, one result per clock).
 *        The Verilog multiplication operator '*' is never emitted, the output
 *        contains no '*' character at all (not even in comments): every
 *        product is reduced to AND, addition, subtraction and shifts.
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
 * @param w Operand width, at least 2.
 * @param pipe true: split, multiplication and recombination are separated by
 *        registers (top level); false: purely combinational.
 */
void emit_step(std::ostringstream& os, int w, bool pipe) {
    Split s = split_of(w);
    int pw = 2 * w;

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

    std::string out = "z";
    if (pipe) out = "m";
    emit_phase(os, pipe, 2, "three half width products");
    os << "    wire " << rng(2 * s.lo) << " " << out << "0;\n";
    os << "    wire " << rng(2 * s.hi) << " " << out << "2;\n";
    os << "    wire " << rng(2 * s.mid) << " " << out << "m;\n";
    os << "    " << mul_module(s.lo) << " u_z0 (.a(a0), .b(b0), .p(" << out << "0));\n";
    os << "    " << mul_module(s.hi) << " u_z2 (.a(a1), .b(b1), .p(" << out << "2));\n";
    os << "    " << mul_module(s.mid) << " u_zm (.a(sa), .b(sb), .p(" << out << "m));\n\n";
    if (pipe) {
        emit_stage(os, {{"z0", 2 * s.lo, "m0"}, {"z2", 2 * s.hi, "m2"}, {"zm", 2 * s.mid, "mm"}});
    }

    emit_phase(os, pipe, 3, "z1 = zm - z2 - z0, recombination");
    os << "    wire " << rng(2 * s.mid) << " zd = zm - " << zext("z2", 2 * s.hi, 2 * s.mid) << " - "
       << zext("z0", 2 * s.lo, 2 * s.mid) << ";\n";
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

/// @brief Emits a w x w combinational leaf: the recursion base, where
///        Karatsuba cannot shrink the operands any more.
/// @param os Output stream.
/// @param w Operand width, 1..kLeafWidth.
void emit_leaf(std::ostringstream& os, int w) {
    std::string comment = "// " + num(w) + " x " + num(w) +
                          " combinational leaf: long multiplication,\n"
                          "// the sum of the partial products ({" +
                          num(w) + "{b[i]}} & a) << i\n";
    emit_header(os, mul_module(w), w, comment, "");
    os << "    assign p = ";
    for (int i = 0; i < w; ++i) {
        if (i > 0) os << "\n             + ";
        std::string partial = "({" + num(w) + "{b[" + num(i) + "]}} & a)";
        os << shl(partial, w, i, 2 * w);
    }
    os << ";\n";
    os << "endmodule\n\n";
}

/// @brief Emits a w x w combinational Karatsuba step module.
/// @param os Output stream.
/// @param w Operand width, greater than kLeafWidth.
void emit_kara(std::ostringstream& os, int w) {
    std::string comment = "// " + num(w) + " x " + num(w) + " combinational Karatsuba step\n";
    emit_header(os, mul_module(w), w, comment, "");
    emit_step(os, w, false);
    os << "endmodule\n\n";
}

/// @brief Emits the pipelined top level module.
/// @param os Output stream.
/// @param n Operand width N.
void emit_top(std::ostringstream& os, int n) {
    std::string comment =
        "// " + std::string(kTop) + ": pipelined unsigned multiplier " + num(n) + " x " + num(n) +
        " -> " + num(2 * n) + "\n" +
        "//   a, b are sampled on a rising edge of clk, their product appears on p\n"
        "//   after the 3rd rising edge.  New operands may be applied every clock.\n";
    emit_header(os, kTop, n, comment, "    input  wire clk,\n");

    if (n >= 2) {
        emit_step(os, n, true);
    } else {
        emit_stage(os, {{"s1", 1, "a & b"}});
        emit_stage(os, {{"s2", 1, "s1"}});
        emit_stage(os, {{"s3", 1, "s2"}});
        os << "    assign p = {1'b0, s3};\n";
    }
    os << "endmodule\n";
}

}  // namespace

void validate(int n) {
    if (n < 1 || n > kMaxWidth) {
        throw std::invalid_argument("N must be 1.." + num(kMaxWidth) + ", got " + num(n));
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

std::string mul_module(int w) {
    if (w <= kLeafWidth) return std::string(kTop) + "_leaf_w" + num(w);
    return std::string(kTop) + "_kara_w" + num(w);
}

std::vector<int> required_widths(int n) {
    validate(n);
    std::set<int> need;
    std::vector<int> todo;
    if (n >= 2) todo.push_back(n);
    while (!todo.empty()) {
        int w = todo.back();
        todo.pop_back();
        Split s = split_of(w);
        std::vector<int> parts = {s.lo, s.hi, s.mid};
        for (int part : parts) {
            bool is_new = need.count(part) == 0;
            need.insert(part);
            if (is_new && part > kLeafWidth) todo.push_back(part);
        }
    }
    return std::vector<int>(need.begin(), need.end());
}

std::string generate_rtl(int n) {
    validate(n);
    std::ostringstream os;
    os << "// Unsigned " << n << " x " << n << " -> " << 2 * n
       << " multiplier, Karatsuba algorithm, 3 stage pipeline.\n";
    os << "// Karatsuba recursion down to " << kLeafWidth
       << " bit operands, below that long multiplication.\n";
    os << "// Generated by karatsuba_gen. \n\n";
    os << "`timescale 1ns / 1ps\n";
    os << "`default_nettype none\n\n";

    std::vector<int> widths = required_widths(n);
    for (int w : widths) {
        if (w <= kLeafWidth) {
            emit_leaf(os, w);
        } else {
            emit_kara(os, w);
        }
    }
    emit_top(os, n);

    os << "\n`default_nettype wire\n";
    return os.str();
}

}  