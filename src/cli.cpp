/**
 * @file cli.cpp
 * @brief Command line parsing and file output of karatsuba_gen.
 */
#include "cli.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace karatsuba {

std::string usage() {
    return "Usage: karatsuba_gen N [options]\n"
           "Writes a Verilog-2001 N x N -> 2N unsigned multiplier (Karatsuba algorithm).\n"
           "The product appears on the 3rd clock edge, one new result every clock.\n"
           "\n"
           "  N                  operand width, 1..4096\n"
           "  -o, --output FILE  output file, '-' for stdout (default <module>.v)\n"
           "  -m, --module NAME  top level module name (default karatsuba_mul)\n"
           "  -b, --base W       recursion stops at W bits, 3..32 (default 4)\n"
           "      --leaf-star    leaf multipliers use '*' (e.g. for FPGA DSP blocks)\n"
           "  -h, --help         show this text\n";
}

int parse_int(const std::string& s, int lo, int hi, const std::string& what) {
    std::string error =
        what + " must be " + std::to_string(lo) + ".." + std::to_string(hi) + ", got '" + s + "'";
    // at most 9 digits, so std::stoi cannot overflow
    if (s.empty() || s.size() > 9) throw std::invalid_argument(error);
    for (char c : s) {
        if (c < '0' || c > '9') throw std::invalid_argument(error);
    }
    int v = std::stoi(s);
    if (v < lo || v > hi) throw std::invalid_argument(error);
    return v;
}

Options parse_args(const std::vector<std::string>& args) {
    Options opt;
    bool have_n = false;

    std::size_t i = 0;
    while (i < args.size()) {
        std::string a = args[i];
        bool needs_value = a == "-o" || a == "--output" || a == "-m" || a == "--module" ||
                           a == "-b" || a == "--base";
        std::string value;
        if (needs_value) {
            if (i + 1 >= args.size()) throw std::invalid_argument("option " + a + " needs a value");
            value = args[i + 1];
            i = i + 1;  // the value is consumed together with the option
        }

        if (a == "-h" || a == "--help") {
            opt.help = true;
            return opt;
        } else if (a == "--leaf-star") {
            opt.leaf_star = true;
        } else if (a == "-o" || a == "--output") {
            opt.out_file = value;
        } else if (a == "-m" || a == "--module") {
            if (!valid_module_name(value)) {
                throw std::invalid_argument("'" + value + "' is not a usable Verilog module name");
            }
            opt.top = value;
        } else if (a == "-b" || a == "--base") {
            opt.base = parse_int(value, kMinBase, kMaxBase, "leaf width");
        } else if (a.size() > 1 && a[0] == '-') {
            throw std::invalid_argument("unknown option '" + a + "' (see --help)");
        } else if (have_n) {
            throw std::invalid_argument("unexpected argument '" + a + "'");
        } else {
            opt.n = parse_int(a, 1, kMaxWidth, "N");
            have_n = true;
        }
        i = i + 1;
    }

    if (!have_n) throw std::invalid_argument("operand width N is missing (see --help)");
    if (opt.out_file.empty()) opt.out_file = opt.top + ".v";
    return opt;
}

void write_text(const std::string& path, const std::string& text) {
    if (path == "-") {
        std::cout << text;
        std::cout.flush();
        if (!std::cout) throw std::runtime_error("cannot write to stdout");
        return;
    }
    std::ofstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);
    file << text;
    file.flush();
    if (!file) throw std::runtime_error("cannot write " + path);
}

}  // namespace karatsuba
