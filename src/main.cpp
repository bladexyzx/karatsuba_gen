/**
 * @file main.cpp
 * @brief Entry point: karatsuba_gen N [options] writes the Verilog description
 *        of a pipelined N x N -> 2N unsigned Karatsuba multiplier.
 */
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cli.h"
#include "generator.h"

/**
 * @brief Runs the generator.
 * @param argc Number of arguments.
 * @param argv Arguments, see karatsuba::usage().
 * @return 0 on success, 1 if the file cannot be written, 2 for bad arguments.
 */
int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

    try {
        karatsuba::Options opt = karatsuba::parse_args(args);
        if (opt.help) {
            std::cout << karatsuba::usage();
            return 0;
        }
        karatsuba::write_text(opt.out_file, karatsuba::generate_rtl(opt));
        std::cerr << "karatsuba_gen: N = " << opt.n << ", latency " << karatsuba::kLatency
                  << " clocks -> " << opt.out_file << "\n";
    } catch (const std::invalid_argument& e) {
        std::cerr << "karatsuba_gen: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "karatsuba_gen: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
