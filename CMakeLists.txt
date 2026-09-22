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

# ------------------------------------------------ simulation of the generated RTL
# cmake --build build --target demo: generate a DEMO_N bit multiplier and simulate
# it with tests/tb.v. Needs Icarus Verilog; other paths:
# cmake -B build -DIVERILOG=/opt/bin/iverilog -DVVP=/opt/bin/vvp
find_program(IVERILOG iverilog)
find_program(VVP vvp)
set(DEMO_N 32 CACHE STRING "Operand width for the demo target")
if(IVERILOG AND VVP)
    set(DEMO_DIR ${CMAKE_BINARY_DIR}/demo)
    add_custom_target(demo
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEMO_DIR}
        COMMAND $<TARGET_FILE:karatsuba_gen> ${DEMO_N} -o ${DEMO_DIR}/karatsuba_mul.v
        COMMAND ${IVERILOG} -g2005 -Wall -P tb.N=${DEMO_N} -o ${DEMO_DIR}/tb.vvp
                ${DEMO_DIR}/karatsuba_mul.v ${CMAKE_SOURCE_DIR}/tests/tb.v
        COMMAND ${VVP} -n ${DEMO_DIR}/tb.vvp
        DEPENDS karatsuba_gen
        USES_TERMINAL)
endif()

# ---------------------------------------------------------------- docs, format
find_package(Doxygen)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating docs/html with Doxygen")
endif()

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB FORMAT_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.h
                             ${CMAKE_SOURCE_DIR}/tests/*.cpp)
    add_custom_target(format COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES})
endif()
