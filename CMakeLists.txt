cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source build is not supported, run: cmake -B build")
endif()
project(karatsuba_gen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The generator itself; linked into the program and into the unit tests.
add_library(karatsuba_core STATIC src/generator.cpp src/cli.cpp)
target_include_directories(karatsuba_core PUBLIC src)
target_compile_options(karatsuba_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(karatsuba_gen src/main.cpp)
target_link_libraries(karatsuba_gen PRIVATE karatsuba_core)

# ---------------------------------------------------------------- tests
option(KARATSUBA_TESTS "Build the unit tests (downloads doctest)" ON)
if(KARATSUBA_TESTS)
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)  # extracted files get the extraction time
    endif()
    FetchContent_Declare(doctest
        URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
        URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186)
    FetchContent_MakeAvailable(doctest)

    add_executable(unit_tests tests/unit_tests.cpp)
    target_link_libraries(unit_tests PRIVATE karatsuba_core doctest::doctest)

    enable_testing()
    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
    doctest_discover_tests(unit_tests TEST_PREFIX "unit: ")
endif()

