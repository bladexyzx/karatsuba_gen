/**
 * @file cli.h
 * @brief Command line interface of karatsuba_gen: argument parsing and file
 *        output. Errors are reported with exceptions.
 */
#ifndef KARATSUBA_CLI_H_
#define KARATSUBA_CLI_H_

#include <string>
#include <vector>

#include "generator.h"

namespace karatsuba {

/// @brief All settings of one program run.
struct Options {
    int n = 0;             ///< Operand width N, 1..kMaxWidth.
    std::string out_file;  ///< Output file.
    bool help = false;     ///< --help was given, nothing is generated.
};

/**
 * @brief Text printed by --help.
 * @return Multi-line usage description.
 */
std::string usage();

/**
 * @brief Parses a decimal integer.
 * @param s Text to parse, digits only.
 * @param lo Smallest allowed value.
 * @param hi Largest allowed value.
 * @param what Name of the value for the error message.
 * @return The parsed value.
 * @throws std::invalid_argument if s is not a number in [lo, hi].
 */
int parse_int(const std::string& s, int lo, int hi, const std::string& what);

/**
 * @brief Parses the command line arguments.
 * @param args Arguments without the program name.
 * @return Settings; out_file defaults to `karatsuba_mul.v`. If help is set,
 *         the other fields are not checked.
 * @throws std::invalid_argument for a missing N, an unknown option or a bad value.
 */
Options parse_args(const std::vector<std::string>& args);

/**
 * @brief Writes text to a file.
 * @param path Destination path (any relative or absolute path).
 * @param text Content to write.
 * @throws std::runtime_error if the file cannot be opened or written.
 */
void write_text(const std::string& path, const std::string& text);

}  // namespace karatsuba

#endif  // KARATSUBA_CLI_H_
