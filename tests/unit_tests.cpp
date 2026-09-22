// Unit tests of the generator (doctest). The generated Verilog itself is
// simulated with the testbench tests/tb.v (cmake --build build --target demo).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
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

Options make_options(int n, int base, bool star) {
    Options opt;
    opt.n = n;
    opt.base = base;
    opt.leaf_star = star;
    opt.top = "kmul";
    return opt;
}

// Options that validate() must reject.
std::vector<Options> bad_options() {
    std::vector<Options> list;
    list.push_back(make_options(0, 4, false));
    list.push_back(make_options(-3, 4, false));
    list.push_back(make_options(kMaxWidth + 1, 4, false));
    list.push_back(make_options(16, kMinBase - 1, false));
    list.push_back(make_options(16, kMaxBase + 1, false));
    Options keyword = make_options(16, 4, false);
    keyword.top = "wire";
    list.push_back(keyword);
    Options empty = make_options(16, 4, false);
    empty.top = "";
    list.push_back(empty);
    return list;
}

// ---------------------------------------------------------------------------
// Model of the generated hardware in C++ for N <= 32 (the product fits into
// 64 bits). It uses the same splits and the same bit widths as the Verilog.

uint64_t mask(int bits) {
    if (bits >= 64) return ~0ULL;
    return (1ULL << bits) - 1;
}

uint64_t model_mul(uint64_t a, uint64_t b, int w, int base);

// One Karatsuba step on w bit operands, as emitted by emit_step().
uint64_t model_step(uint64_t a, uint64_t b, int w, int base) {
    Split s = split_of(w);
    uint64_t a0 = a & mask(s.lo);
    uint64_t a1 = a >> s.lo;
    uint64_t b0 = b & mask(s.lo);
    uint64_t b1 = b >> s.lo;
    uint64_t sa = a0 + a1;
    uint64_t sb = b0 + b1;
    REQUIRE(sa <= mask(s.mid));  // the sums fit into mid bits
    REQUIRE(sb <= mask(s.mid));

    uint64_t z0 = model_mul(a0, b0, s.lo, base);
    uint64_t z2 = model_mul(a1, b1, s.hi, base);
    uint64_t zm = model_mul(sa, sb, s.mid, base);

    uint64_t zd = (zm - z2 - z0) & mask(2 * s.mid);  // wire [2*mid-1:0] zd
    uint64_t z1 = zd & mask(w + 1);                  // wire [w:0] z1 = zd[w:0]
    return ((z2 << (2 * s.lo)) + (z1 << s.lo) + z0) & mask(2 * w);
}

// A sub-multiplier module: a leaf for w <= base, otherwise a Karatsuba step.
uint64_t model_mul(uint64_t a, uint64_t b, int w, int base) {
    if (w <= base) return a * b;
    return model_step(a, b, w, base);
}

// The top level module: its own step for N >= 2, a & b for N = 1.
uint64_t model_top(uint64_t a, uint64_t b, int n, int base) {
    if (n == 1) return a & b;
    return model_step(a, b, n, base);
}

// ---------------------------------------------------------------------------
// Structure of the generated Verilog.

void check_rtl(int n, int base, bool star) {
    CAPTURE(n);
    CAPTURE(base);
    CAPTURE(star);
    Options opt = make_options(n, base, star);
    std::string v = generate_rtl(opt);

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
    CHECK(modules == (int)required_widths(opt).size() + 1);
    CHECK(defined.count("kmul") == 1);
    for (const std::string& name : instantiated) {
        CAPTURE(name);
        CHECK(defined.count(name) == 1);
    }

    CHECK(has(v, "input  wire [" + std::to_string(n - 1) + ":0] a,"));
    CHECK(has(v, "output wire [" + std::to_string(2 * n - 1) + ":0] p\n"));

    // three register stages plus the valid chain, all in the top level
    std::string top = v.substr(v.find("module kmul ("));
    CHECK(count(v, "always @(posedge clk)") == 4);
    CHECK(count(top, "always @(posedge clk)") == 4);
    CHECK(has(top, "vld <= rst_n ? {vld[1:0], in_valid} : 3'b000;"));
    CHECK(has(top, "assign out_valid = vld[2];"));
    if (n >= 2) {
        CHECK(has(top, "pr <= "));
        CHECK(has(top, "assign p = pr;"));
    }

    // no degenerate widths, balanced lint pragmas, leaf style follows the option
    CHECK_FALSE(has(v, "{0{"));
    CHECK_FALSE(has(v, "[-1:0]"));
    CHECK(count(v, "lint_off") == count(v, "lint_on"));
    CHECK(has(v, "assign p = a * b;") == (star && n >= 2));
    CHECK(generate_rtl(opt) == v);  // the same input gives the same text
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
    std::vector<int> bases = {3, 4, 5, 8, 16, 32};
    std::vector<int> widths = {1, 2, 3, 4, 5, 8, 16, 17, 32, 33, 100, 128, 1000, 4096};
    for (int base : bases) {
        for (int n : widths) {
            CAPTURE(n);
            CAPTURE(base);
            std::vector<int> ws = required_widths(make_options(n, base, false));
            std::set<int> found(ws.begin(), ws.end());
            CHECK(found.size() == ws.size());  // no duplicates
            CHECK(ws.empty() == (n == 1));
            for (std::size_t i = 0; i < ws.size(); ++i) {
                if (i > 0) CHECK(ws[i - 1] < ws[i]);  // ascending
                CHECK(ws[i] >= 1);
                CHECK(ws[i] <= n);
                if (ws[i] > base) {
                    Split s = split_of(ws[i]);
                    CHECK(found.count(s.lo) == 1);
                    CHECK(found.count(s.hi) == 1);
                    CHECK(found.count(s.mid) == 1);
                }
            }
        }
    }
    std::vector<int> expected = {2, 3, 4, 5, 6, 8, 9};
    CHECK(required_widths(make_options(16, 4, false)) == expected);
}

TEST_CASE("required_widths: invalid options are rejected") {
    for (const Options& bad : bad_options()) {
        CHECK_THROWS_AS(required_widths(bad), std::invalid_argument);
    }
}

TEST_CASE("valid_module_name: identifiers are accepted") {
    std::vector<std::string> good = {"karatsuba_mul", "mul64", "_x", "M", "wire_", "zz0", "Pr"};
    for (const std::string& name : good) {
        CAPTURE(name);
        CHECK(valid_module_name(name));
    }
}

TEST_CASE("valid_module_name: non-identifiers, keywords and internal names are rejected") {
    std::vector<std::string> bad = {"",       "9x",    "a-b", "a b", "a$",   "wire",
                                    "module", "logic", "reg", "a",   "p",    "clk",
                                    "mm",     "z1",    "pr",  "vld", "u_zm", "мул"};
    for (const std::string& name : bad) {
        CAPTURE(name);
        CHECK_FALSE(valid_module_name(name));
    }
}

TEST_CASE("validate: good options pass, bad options are reported") {
    CHECK_NOTHROW(validate(make_options(1, 4, false)));
    CHECK_NOTHROW(validate(make_options(kMaxWidth, kMaxBase, false)));
    CHECK_NOTHROW(validate(make_options(8, kMinBase, true)));
    for (const Options& bad : bad_options()) {
        CHECK_THROWS_AS(validate(bad), std::invalid_argument);
    }
    CHECK_THROWS_WITH(validate(make_options(0, 4, false)), "N must be 1..4096, got 0");
    CHECK_THROWS_WITH(validate(make_options(8, 2, false)), "leaf width must be 3..32, got 2");
}

TEST_CASE("mul_module: leaves up to base, Karatsuba steps above") {
    Options opt = make_options(64, 8, false);
    CHECK(mul_module(opt, 3) == "kmul_leaf_w3");
    CHECK(mul_module(opt, 8) == "kmul_leaf_w8");
    CHECK(mul_module(opt, 9) == "kmul_kara_w9");
    CHECK(mul_module(opt, 33) != "kmul_leaf_w33");
}

TEST_CASE("model: the generated arithmetic multiplies correctly (N <= 32)") {
    std::mt19937_64 rng(12345);
    std::vector<int> bases = {3, 4, 5, 8, 16, 32};
    for (int base : bases) {
        for (int n = 1; n <= 32; ++n) {
            CAPTURE(n);
            CAPTURE(base);
            uint64_t max = mask(n);
            // corner values
            std::vector<uint64_t> values = {0, 1, 2, max, max - 1, max / 2, max / 2 + 1};
            for (uint64_t a : values) {
                for (uint64_t b : values) {
                    a = a & max;
                    b = b & max;
                    CAPTURE(a);
                    CAPTURE(b);
                    REQUIRE(model_top(a, b, n, base) == a * b);
                }
            }
            // random values
            for (int i = 0; i < 300; ++i) {
                uint64_t a = rng() & max;
                uint64_t b = rng() & max;
                CAPTURE(a);
                CAPTURE(b);
                REQUIRE(model_top(a, b, n, base) == a * b);
            }
        }
    }
}

TEST_CASE("model: every pair for small N") {
    for (int n = 1; n <= 8; ++n) {
        for (uint64_t a = 0; a < (1ULL << n); ++a) {
            for (uint64_t b = 0; b < (1ULL << n); ++b) {
                REQUIRE(model_top(a, b, n, 3) == a * b);
            }
        }
    }
}

TEST_CASE("generate_rtl: structure of the emitted Verilog") {
    for (int n = 1; n <= 40; ++n) check_rtl(n, 4, false);
    std::vector<int> wide = {63, 64, 65, 127, 128, 129, 255, 256, 1024, 4096};
    for (int n : wide) check_rtl(n, 4, false);
    std::vector<int> bases = {3, 5, 8, 16, 32};
    for (int base : bases) {
        check_rtl(17, base, false);
        check_rtl(64, base, true);
    }
    check_rtl(32, 32, true);  // N equal to the leaf width
}

TEST_CASE("generate_rtl: invalid options are rejected") {
    for (const Options& bad : bad_options()) {
        CHECK_THROWS_AS(generate_rtl(bad), std::invalid_argument);
    }
}

TEST_CASE("usage: describes every option") {
    std::string text = usage();
    CHECK(starts_with(text, "Usage: karatsuba_gen N"));
    std::vector<std::string> options = {"--output", "--module", "--base", "--leaf-star", "--help"};
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
    Options a = parse_args({"64", "-b", "8", "--leaf-star", "-m", "m"});
    CHECK_FALSE(a.help);
    CHECK(a.n == 64);
    CHECK(a.base == 8);
    CHECK(a.leaf_star);
    CHECK(a.top == "m");
    CHECK(a.out_file == "m.v");  // the default follows the module name

    Options b = parse_args({"--output", "out/x.v", "16"});
    CHECK(b.n == 16);
    CHECK(b.out_file == "out/x.v");
    CHECK(b.top == "karatsuba_mul");
    CHECK(b.base == 4);
    CHECK_FALSE(b.leaf_star);

    CHECK(parse_args({"-h"}).help);
    CHECK(parse_args({"16", "--help", "-q"}).help);  // help wins over later errors
    CHECK(parse_args({"8", "-o", "-"}).out_file == "-");
    CHECK(parse_args({"--base", "32", "--module", "x_1", "1"}).top == "x_1");
}

TEST_CASE("parse_args: invalid command lines are rejected") {
    std::vector<std::vector<std::string>> bad = {{},
                                                 {"0"},
                                                 {"-1"},
                                                 {"abc"},
                                                 {"12.5"},
                                                 {"4097"},
                                                 {"16", "32"},
                                                 {"16", "-b", "2"},
                                                 {"16", "-b", "33"},
                                                 {"16", "-b"},
                                                 {"16", "-o"},
                                                 {"16", "-q"},
                                                 {"16", "-m", "9bad"},
                                                 {"16", "-m", "wire"}};
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
