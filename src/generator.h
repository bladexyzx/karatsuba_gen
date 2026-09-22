/**
 * @file generator.h
 * @brief Generator of a pipelined N x N -> 2N unsigned Karatsuba multiplier
 *        in Verilog-2001.
 */
#ifndef KARATSUBA_GENERATOR_H_
#define KARATSUBA_GENERATOR_H_

#include <string>
#include <vector>

/// @brief Everything related to the Karatsuba multiplier generator.
namespace karatsuba {

const int kMaxWidth = 4096;  ///< Largest supported operand width N.
const int kMinBase = 3;      ///< Smallest leaf width; smaller leaves would not end the recursion.
const int kMaxBase = 32;     ///< Largest leaf width.
const int kLatency = 3;      ///< Pipeline depth in clocks, fixed by the specification.

/// @brief All settings of one program run.
struct Options {
    int n = 0;               ///< Operand width N, 1..kMaxWidth.
    int base = 4;            ///< Operands of at most this many bits use a leaf multiplier.
    bool leaf_star = false;  ///< Leaf multipliers use the '*' operator instead of shift-add.
    std::string top = "karatsuba_mul";  ///< Name of the top level module.
    std::string out_file;               ///< Output file, "-" means stdout.
    bool help = false;                  ///< --help was given, nothing is generated.
};

/**
 * @brief Widths of one Karatsuba step on a w bit operand:
 *        A = A1 * 2^lo + A0, where A0 has lo bits, A1 has hi bits and
 *        A1 + A0 has mid bits.
 */
struct Split {
    int lo;   ///< Width of the low part A0, lo = ceil(w / 2).
    int hi;   ///< Width of the high part A1, hi = w - lo <= lo.
    int mid;  ///< Width of the sum A1 + A0, mid = lo + 1.
};

/**
 * @brief Splits a w bit operand into halves for one Karatsuba step.
 * @param w Operand width, at least 2.
 * @return Widths of the parts; for w >= 4 all of them are smaller than w.
 * @throws std::invalid_argument if w < 2.
 */
Split split_of(int w);

/**
 * @brief Lists the widths of all sub-multiplier modules needed for opt.n.
 * @param opt Generator settings (n and base are used).
 * @return Distinct widths in ascending order, so every module can be defined
 *         before it is instantiated. The top level itself is not included,
 *         the list is empty for N = 1.
 * @throws std::invalid_argument if the options are invalid.
 */
std::vector<int> required_widths(const Options& opt);

/**
 * @brief Checks that s can name the top level module.
 * @param s Candidate name.
 * @return true for a plain Verilog identifier that is neither a reserved word
 *         nor a signal name used inside the generated module.
 */
bool valid_module_name(const std::string& s);

/**
 * @brief Checks the fields used by the generator.
 * @param opt Settings to check.
 * @throws std::invalid_argument naming the first invalid field.
 */
void validate(const Options& opt);

/**
 * @brief Name of the w bit sub-multiplier module.
 * @param opt Generator settings (top and base are used).
 * @param w Operand width of the sub-multiplier.
 * @return `TOP_leaf_wW` for w <= base, otherwise `TOP_kara_wW`,
 *         where TOP is opt.top and W is w.
 */
std::string mul_module(const Options& opt, int w);

/**
 * @brief Generates the synthesisable multiplier.
 * @param opt Generator settings.
 * @return Verilog text: the leaf and Karatsuba sub-modules followed by the
 *         pipelined top level module with latency kLatency.
 * @throws std::invalid_argument if the options are invalid.
 */
std::string generate_rtl(const Options& opt);

}  // namespace karatsuba

#endif  // KARATSUBA_GENERATOR_H_
