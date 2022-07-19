
add_executable(
    blocking_to_async_bm
    blocking_to_async_bm.cpp
    blocking_to_async_suite.cpp
    continuous_workload.cpp
    workload.cpp
)

target_link_libraries(
    blocking_to_async_bm
    benchmark
    pthread
)

target_include_directories(blocking_to_async_bm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

include(GoogleTest)
gtest_discover_tests(blocking_to_async_bm)
