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
const int kLeafWidth = 4;    ///< Operands of at most this many bits use a leaf: long
                             ///< multiplication built from bitwise AND and adders. At least 3,
                             ///< smaller leaves would not end the recursion.
const int kLatency = 3;      ///< Pipeline depth in clocks, fixed by the specification.
const char kTop[] = "karatsuba_mul";  ///< Name of the top level module.

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
 * @brief Lists the widths of all sub-multiplier modules needed for N = n.
 * @param n Operand width of the top level.
 * @return Distinct widths in ascending order, so every module can be defined
 *         before it is instantiated. The top level itself is not included,
 *         the list is empty for N = 1.
 * @throws std::invalid_argument if n is out of range.
 */
std::vector<int> required_widths(int n);

/**
 * @brief Checks the operand width.
 * @param n Operand width N.
 * @throws std::invalid_argument if n is not in 1..kMaxWidth.
 */
void validate(int n);

/**
 * @brief Name of the w bit sub-multiplier module.
 * @param w Operand width of the sub-multiplier.
 * @return `karatsuba_mul_leaf_wW` for w <= kLeafWidth, otherwise
 *         `karatsuba_mul_kara_wW`, where W is w.
 */
std::string mul_module(int w);

/**
 * @brief Generates the synthesisable multiplier.
 * @param n Operand width N.
 * @return Verilog text: the leaf and Karatsuba sub-modules followed by the
 *         pipelined top level module with latency kLatency.
 * @throws std::invalid_argument if n is out of range.
 */
std::string generate_rtl(int n);

}  // namespace karatsuba

#endif  // KARATSUBA_GENERATOR_H_
