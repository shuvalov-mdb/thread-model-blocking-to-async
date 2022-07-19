cmake_minimum_required(VERSION 3.11)
project(thread-model-blocking-to-async)

include_directories(src)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(benchmark REQUIRED)

add_subdirectory(src/benchmarks build)

enable_testing()

add_test(NAME benchmarks COMMAND benchmarks)
