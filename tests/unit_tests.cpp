// Unit tests of the generator (doctest).
// The '*' in this file belongs to the C++ reference only, never to the RTL.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cli.h"
#include "generator.h"

using namespace karatsuba;

namespace {

bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

int count(const std::string& text, const std::string& part) {
    int n = 0;
    std::size_t pos = text.find(part);
    while (pos != std::string::npos) {
        n = n + 1;
        pos = text.find(part, pos + 1);
    }
    return n;
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

// Operand widths that validate() must reject.
const std::vector<int> kBadWidths = {0, -3, kMaxWidth + 1};

// ---------------------------------------------------------------------------
// Model of the generated hardware in C++ for N <= 32 (the product fits into
// 64 bits). It uses the same splits and the same bit widths as the Verilog.

uint64_t mask(int bits) {
    if (bits >= 64) return ~0ULL;
    return (1ULL << bits) - 1;
}

uint64_t model_mul(uint64_t a, uint64_t b, int w);

// One Karatsuba step on w bit operands, as emitted by emit_step().
uint64_t model_step(uint64_t a, uint64_t b, int w) {
    Split s = split_of(w);
    uint64_t a0 = a & mask(s.lo);
    uint64_t a1 = a >> s.lo;
    uint64_t b0 = b & mask(s.lo);
    uint64_t b1 = b >> s.lo;
    uint64_t sa = a0 + a1;
    uint64_t sb = b0 + b1;
    REQUIRE(sa <= mask(s.mid));  // the sums fit into mid bits
    REQUIRE(sb <= mask(s.mid));

    uint64_t z0 = model_mul(a0, b0, s.lo);
    uint64_t z2 = model_mul(a1, b1, s.hi);
    uint64_t zm = model_mul(sa, sb, s.mid);

    uint64_t zd = (zm - z2 - z0) & mask(2 * s.mid);  // wire [2*mid-1:0] zd
    uint64_t z1 = zd & mask(w + 1);                  // wire [w:0] z1 = zd[w:0]
    return ((z2 << (2 * s.lo)) + (z1 << s.lo) + z0) & mask(2 * w);
}

// A leaf, as emitted by emit_leaf(): the sum of ({w{b[i]}} & a) << i.
uint64_t model_leaf(uint64_t a, uint64_t b, int w) {
    uint64_t p = 0;
    for (int i = 0; i < w; ++i) {
        uint64_t partial = a & (0 - ((b >> i) & 1));  // all ones if b[i] is set
        p = p + (partial << i);
    }
    return p & mask(2 * w);
}

// A sub-multiplier module: a leaf for w <= kLeafWidth, otherwise a Karatsuba step.
uint64_t model_mul(uint64_t a, uint64_t b, int w) {
    if (w <= kLeafWidth) return model_leaf(a, b, w);
    return model_step(a, b, w);
}

// The top level module: its own step for N >= 2, a & b for N = 1.
uint64_t model_top(uint64_t a, uint64_t b, int n) {
    if (n == 1) return a & b;
    return model_step(a, b, n);
}

// ---------------------------------------------------------------------------
// Structure of the generated Verilog.

void check_rtl(int n) {
    CAPTURE(n);
    std::string v = generate_rtl(n);

    // collect module definitions ("module NAME (") and instances ("    NAME u_...")
    std::set<std::string> defined;
    std::vector<std::string> instantiated;
    int modules = 0;
    std::istringstream lines(v);
    std::string line;
    while (std::getline(lines, line)) {
        if (starts_with(line, "module ")) {
            std::string name = line.substr(7, line.find(' ', 7) - 7);
            defined.insert(name);
            modules = modules + 1;
        }
        std::size_t inst = line.find(" u_");
        if (starts_with(line, "    ") && inst != std::string::npos) {
            instantiated.push_back(line.substr(4, inst - 4));
        }
    }
    CHECK(modules == (int)defined.size());  // no module is defined twice
    CHECK(modules == (int)required_widths(n).size() + 1);
    CHECK(defined.count("karatsuba_mul") == 1);
    for (const std::string& name : instantiated) {
        CAPTURE(name);
        CHECK(defined.count(name) == 1);
    }

    // the top level has exactly the ports clk, a, b, p
    std::string hi = std::to_string(n - 1);
    CHECK(has(v,
              "module karatsuba_mul (\n"
              "    input  wire clk,\n"
              "    input  wire [" +
                  hi +
                  ":0] a,\n"
                  "    input  wire [" +
                  hi +
                  ":0] b,\n"
                  "    output wire [" +
                  std::to_string(2 * n - 1) +
                  ":0] p\n"
                  ");\n"));

    // three register stages, all in the top level
    std::string top = v.substr(v.find("module karatsuba_mul ("));
    CHECK(count(v, "always @(posedge clk)") == 3);
    CHECK(count(top, "always @(posedge clk)") == 3);
    if (n >= 2) {
        CHECK(has(top, "pr <= "));
        CHECK(has(top, "assign p = pr;"));
    }

    // no degenerate widths
    CHECK_FALSE(has(v, "{0{"));
    CHECK_FALSE(has(v, "[-1:0]"));
    CHECK(generate_rtl(n) == v);  // the same input gives the same text
}

// ---------------------------------------------------------------------------
// Pipeline timing of the top level module, read from the generated text.

// A signal of the top level: a register loaded on the clock edge or a
// combinational wire, and the signals its value is computed from.
struct Node {
    bool reg = false;
    std::set<std::string> deps;
};

// Signal names used in a Verilog expression, constants such as 4'b0 removed.
std::set<std::string> names_in(const std::string& expr) {
    static const std::regex literal("[0-9]+'[bdhoBDHO][0-9a-fA-FxXzZ_]+");
    static const std::regex name("[A-Za-z_][A-Za-z0-9_]*");
    std::string e = std::regex_replace(expr, literal, " ");
    std::set<std::string> names;
    for (std::sregex_iterator it(e.begin(), e.end(), name), end; it != end; ++it) {
        names.insert(it->str());
    }
    return names;
}

std::string trim(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \n") - b + 1);
}

// Signals of the module karatsuba_mul in v. Every statement of the body is
// "reg ...", "wire ... NAME;", "wire ... NAME = EXPR;", "assign NAME = EXPR;",
// "NAME <= EXPR;" inside always @(posedge clk), or a sub-multiplier instance
// "MOD u_x (.a(X), .b(Y), .p(Z));" whose output Z is combinational in X, Y.
std::map<std::string, Node> top_signals(const std::string& v) {
    std::string top = v.substr(v.find("module karatsuba_mul ("));
    top = top.substr(0, top.find("endmodule"));
    top = top.substr(top.find(");\n") + 3);  // skip the port list

    std::string body;  // statements only, comments and begin/end removed
    std::istringstream lines(top);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line.substr(0, line.find("//")));
        if (line == "always @(posedge clk) begin" || line == "end") continue;
        body += line + "\n";
    }

    static const std::regex wire_def("wire (\\[[0-9]+:0\\] )?([A-Za-z_0-9]+) = ([\\s\\S]*)");
    static const std::regex assign("assign ([A-Za-z_0-9]+) = ([\\s\\S]*)");
    static const std::regex load("([A-Za-z_0-9]+) <= ([\\s\\S]*)");
    static const std::regex port("\\.([a-z]+)\\(([A-Za-z_0-9]+)\\)");
    std::map<std::string, Node> g;
    std::istringstream statements(body);
    std::string st;
    while (std::getline(statements, st, ';')) {
        st = trim(st);
        std::smatch m;
        if (st.empty() || starts_with(st, "reg ")) continue;
        if (std::regex_match(st, m, wire_def)) {
            g[m[2]].deps = names_in(m[3]);
        } else if (starts_with(st, "wire ")) {
            continue;  // declaration of an instance output
        } else if (std::regex_match(st, m, assign)) {
            g[m[1]].deps = names_in(m[2]);
        } else if (std::regex_match(st, m, load)) {
            g[m[1]].reg = true;
            g[m[1]].deps = names_in(m[2]);
        } else if (has(st, " u_")) {
            std::string out;
            std::set<std::string> in;
            for (std::sregex_iterator it(st.begin(), st.end(), port), end; it != end; ++it) {
                if ((*it)[1] == "p") {
                    out = (*it)[2];
                } else {
                    in.insert((*it)[2]);
                }
            }
            if (out.empty()) throw std::runtime_error("instance without output: " + st);
            g[out].deps = in;
        } else {
            throw std::runtime_error("unexpected statement: " + st);
        }
    }
    return g;
}

// Number of clock edges between the ports a, b and the signal: 0 for a, b,
// plus one for every register on the way. Throws if a signal is computed
// from values of different stages, i.e. from different operand pairs.
int stage_of(const std::map<std::string, Node>& g, const std::string& name, int depth = 0) {
    if (name == "a" || name == "b") return 0;
    auto it = g.find(name);
    if (it == g.end()) throw std::runtime_error("undefined signal " + name);
    if (depth > 1000) throw std::runtime_error("combinational loop at " + name);
    std::set<int> stages;
    for (const std::string& dep : it->second.deps) stages.insert(stage_of(g, dep, depth + 1));
    if (stages.size() != 1) throw std::runtime_error(name + " mixes pipeline stages");
    return *stages.begin() + (it->second.reg ? 1 : 0);
}

}  // namespace

// ---------------------------------------------------------------------------

TEST_CASE("split_of: widths are sufficient and the recursion shrinks") {
    for (int w = 2; w <= kMaxWidth; ++w) {
        CAPTURE(w);
        Split s = split_of(w);
        REQUIRE(s.lo + s.hi == w);
        REQUIRE(s.hi <= s.lo);
        REQUIRE(s.hi >= 1);
        REQUIRE(s.mid == s.lo + 1);      // A1 + A0 < 2^(lo+1)
        REQUIRE(2 * s.mid >= w + 1);     // zm is wide enough to hold z1
        REQUIRE(w + 1 + s.lo <= 2 * w);  // z1 << lo fits into the product
        if (w >= 4) REQUIRE(s.mid < w);
    }
    CHECK(split_of(8).lo == 4);
    CHECK(split_of(9).lo == 5);
    CHECK(split_of(9).hi == 4);
}

TEST_CASE("split_of: widths below 2 are rejected") {
    CHECK_THROWS_AS(split_of(1), std::invalid_argument);
    CHECK_THROWS_AS(split_of(0), std::invalid_argument);
    CHECK_THROWS_AS(split_of(-100), std::invalid_argument);
}

TEST_CASE("required_widths: closed under the recursion and sorted") {
    std::vector<int> widths = {1, 2, 3, 4, 5, 8, 16, 17, 32, 33, 100, 128, 1000, 4096};
    for (int n : widths) {
        CAPTURE(n);
        std::vector<int> ws = required_widths(n);
        std::set<int> found(ws.begin(), ws.end());
        CHECK(found.size() == ws.size());  // no duplicates
        CHECK(ws.empty() == (n == 1));
        for (std::size_t i = 0; i < ws.size(); ++i) {
            if (i > 0) CHECK(ws[i - 1] < ws[i]);  // ascending
            CHECK(ws[i] >= 1);
            CHECK(ws[i] <= n);
            if (ws[i] > kLeafWidth) {
                Split s = split_of(ws[i]);
                CHECK(found.count(s.lo) == 1);
                CHECK(found.count(s.hi) == 1);
                CHECK(found.count(s.mid) == 1);
            }
        }
    }
    std::vector<int> expected = {2, 3, 4, 5, 6, 8, 9};
    CHECK(required_widths(16) == expected);
}

TEST_CASE("required_widths: invalid widths are rejected") {
    for (int n : kBadWidths) {
        CAPTURE(n);
        CHECK_THROWS_AS(required_widths(n), std::invalid_argument);
    }
}

TEST_CASE("validate: good widths pass, bad widths are reported") {
    CHECK_NOTHROW(validate(1));
    CHECK_NOTHROW(validate(64));
    CHECK_NOTHROW(validate(kMaxWidth));
    for (int n : kBadWidths) {
        CAPTURE(n);
        CHECK_THROWS_AS(validate(n), std::invalid_argument);
    }
    CHECK_THROWS_WITH(validate(0), "N must be 1..4096, got 0");
}

TEST_CASE("mul_module: leaves up to kLeafWidth, Karatsuba steps above") {
    CHECK(mul_module(2) == "karatsuba_mul_leaf_w2");
    CHECK(mul_module(4) == "karatsuba_mul_leaf_w4");
    CHECK(mul_module(5) == "karatsuba_mul_kara_w5");
    CHECK(mul_module(33) == "karatsuba_mul_kara_w33");
}

TEST_CASE("model: the generated arithmetic multiplies correctly (N <= 32)") {
    std::mt19937_64 rng(12345);
    for (int n = 1; n <= 32; ++n) {
        CAPTURE(n);
        uint64_t max = mask(n);
        // corner values
        std::vector<uint64_t> values = {0, 1, 2, max, max - 1, max / 2, max / 2 + 1};
        for (uint64_t a : values) {
            for (uint64_t b : values) {
                a = a & max;
                b = b & max;
                CAPTURE(a);
                CAPTURE(b);
                REQUIRE(model_top(a, b, n) == a * b);
            }
        }
        // random values
        for (int i = 0; i < 2000; ++i) {
            uint64_t a = rng() & max;
            uint64_t b = rng() & max;
            CAPTURE(a);
            CAPTURE(b);
            REQUIRE(model_top(a, b, n) == a * b);
        }
    }
}

TEST_CASE("model: every pair for small N") {
    for (int n = 1; n <= 8; ++n) {
        for (uint64_t a = 0; a < (1ULL << n); ++a) {
            for (uint64_t b = 0; b < (1ULL << n); ++b) {
                REQUIRE(model_top(a, b, n) == a * b);
            }
        }
    }
}

TEST_CASE("generate_rtl: structure of the emitted Verilog") {
    for (int n = 1; n <= 40; ++n) check_rtl(n);
    std::vector<int> wide = {63, 64, 65, 127, 128, 129, 255, 256, 1024, 4096};
    for (int n : wide) check_rtl(n);
}

TEST_CASE("generate_rtl: the leaf is long multiplication from AND and adders") {
    // the 4 bit leaf is the sum of the partial products (b[i] ? a : 0) << i
    std::string v = generate_rtl(16);
    CHECK(has(v,
              "assign p = {{4{1'b0}}, ({4{b[0]}} & a)}\n"
              "             + {{3{1'b0}}, ({4{b[1]}} & a), 1'b0}\n"
              "             + {{2{1'b0}}, ({4{b[2]}} & a), {2{1'b0}}}\n"
              "             + {1'b0, ({4{b[3]}} & a), {3{1'b0}}};\n"));
}

TEST_CASE("generate_rtl: p comes 3 clocks after a, b, a new pair every clock") {
    std::vector<int> widths;
    for (int n = 1; n <= 40; ++n) widths.push_back(n);
    std::vector<int> wide = {63, 64, 65, 127, 128, 129, 255, 256, 1024, 4096};
    widths.insert(widths.end(), wide.begin(), wide.end());
    for (int n : widths) {
        CAPTURE(n);
        std::map<std::string, Node> g = top_signals(generate_rtl(n));
        // each signal depends on one stage only, so the stages hold
        // consecutive operand pairs and p changes on every clock
        CHECK(stage_of(g, "p") == kLatency);
        CHECK_FALSE(g.at("p").reg);  // p is a wire straight from the 3rd stage
        for (const auto& [name, node] : g) {
            CAPTURE(name);
            if (node.reg) CHECK(stage_of(g, name) >= 1);
        }
        if (n >= 2) {
            for (const char* r : {"a0", "a1", "b0", "b1", "sa", "sb"}) CHECK(stage_of(g, r) == 1);
            for (const char* r : {"z0", "z2", "zm"}) CHECK(stage_of(g, r) == 2);
            CHECK(stage_of(g, "pr") == 3);
        }
    }
}

TEST_CASE("generate_rtl: the pipeline check rejects wrong timing") {
    std::string head =
        "module karatsuba_mul (\n"
        "    input  wire clk,\n"
        "    input  wire [0:0] a,\n"
        "    input  wire [0:0] b,\n"
        "    output wire [1:0] p\n"
        ");\n"
        "    reg  [0:0] s1;\n"
        "    always @(posedge clk) begin\n"
        "        s1 <= a & b;\n"
        "    end\n";
    // two stages only: the result comes one clock too early
    std::string two = head +
                      "    reg  [0:0] s2;\n"
                      "    always @(posedge clk) begin\n"
                      "        s2 <= s1;\n"
                      "    end\n"
                      "    assign p = {1'b0, s2};\n"
                      "endmodule\n";
    CHECK(stage_of(top_signals(two), "p") == 2);
    // stage 2 takes the stage 1 value and the new operands: two pairs mixed
    std::string mixed = head +
                        "    reg  [0:0] s2;\n"
                        "    always @(posedge clk) begin\n"
                        "        s2 <= s1 | a;\n"
                        "    end\n"
                        "    assign p = {1'b0, s2};\n"
                        "endmodule\n";
    CHECK_THROWS_WITH(stage_of(top_signals(mixed), "p"), "s2 mixes pipeline stages");
    // a signal that is never defined
    std::string open = head + "    assign p = {1'b0, s9};\nendmodule\n";
    CHECK_THROWS_AS(stage_of(top_signals(open), "p"), std::runtime_error);
}

TEST_CASE("generate_rtl: invalid widths are rejected") {
    for (int n : kBadWidths) {
        CAPTURE(n);
        CHECK_THROWS_AS(generate_rtl(n), std::invalid_argument);
    }
}

TEST_CASE("usage: describes every option") {
    std::string text = usage();
    CHECK(starts_with(text, "Usage: karatsuba_gen N"));
    std::vector<std::string> options = {"--output", "--help"};
    for (const std::string& option : options) CHECK(has(text, option));
}

TEST_CASE("parse_int: numbers in range") {
    CHECK(parse_int("1", 1, 10, "x") == 1);
    CHECK(parse_int("10", 1, 10, "x") == 10);
    CHECK(parse_int("0", 0, 5, "x") == 0);
    CHECK(parse_int("007", 1, 10, "x") == 7);
}

TEST_CASE("parse_int: malformed or out of range values are rejected") {
    std::vector<std::string> bad = {"",    "0",   "11", "-1", "+5",
                                    "1.5", "abc", "5x", " 5", "1234567890"};
    for (const std::string& s : bad) {
        CAPTURE(s);
        CHECK_THROWS_AS(parse_int(s, 1, 10, "x"), std::invalid_argument);
    }
    CHECK_THROWS_WITH(parse_int("99", 1, 10, "N"), "N must be 1..10, got '99'");
}

TEST_CASE("parse_args: valid command lines") {
    Options a = parse_args({"64"});
    CHECK_FALSE(a.help);
    CHECK(a.n == 64);
    CHECK(a.out_file == "karatsuba_mul.v");

    Options b = parse_args({"--output", "out/x.v", "16"});
    CHECK(b.n == 16);
    CHECK(b.out_file == "out/x.v");

    CHECK(parse_args({"1", "-o", "m.v"}).out_file == "m.v");
    CHECK(parse_args({"-h"}).help);
    CHECK(parse_args({"16", "--help", "-q"}).help);  // help wins over later errors
}

TEST_CASE("parse_args: invalid command lines are rejected") {
    std::vector<std::vector<std::string>> bad = {
        {}, {"0"}, {"-1"}, {"abc"}, {"12.5"}, {"4097"}, {"16", "32"}, {"16", "-o"}, {"16", "-q"}};
    for (const std::vector<std::string>& args : bad) {
        std::string line;
        for (const std::string& arg : args) line = line + arg + " ";
        CAPTURE(line);
        CHECK_THROWS_AS(parse_args(args), std::invalid_argument);
    }
}

TEST_CASE("write_text: the file receives exactly the text") {
    std::string path = "write_text_test.tmp";
    write_text(path, "line 1\nline 2\n");
    std::ifstream file(path);
    std::stringstream content;
    content << file.rdbuf();
    CHECK(content.str() == "line 1\nline 2\n");
    std::remove(path.c_str());
}

TEST_CASE("write_text: unwritable paths are reported") {
    CHECK_THROWS_AS(write_text("no_such_dir/x.v", "x"), std::runtime_error);
    CHECK_THROWS_AS(write_text(".", "x"), std::runtime_error);
}
