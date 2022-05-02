include_directories(../runtime/include)

add_executable(workload_gen workload_gen.cpp workload_gen.h)

set(BENCHMARK_FILES main.cpp bench.cpp)

add_executable(benchmark ${BENCHMARK_FILES})
add_dependencies(benchmark crpm-opt)
set(CRPM_OPT_PATH "${CMAKE_BINARY_DIR}/instrumentation/libcrpm-opt.so")
set(CRPM_OPT_FLAGS "-Xclang -load -Xclang ${CRPM_OPT_PATH} -fno-unroll-loops")
set_target_properties(benchmark PROPERTIES COMPILE_FLAGS ${CRPM_OPT_FLAGS})
target_link_libraries(benchmark PUBLIC crpm numa)

if (FULL_BUILD)
    add_executable(benchmark_lmc ${BENCHMARK_FILES})
    add_dependencies(benchmark_lmc crpm-opt)
    set_target_properties(benchmark_lmc PROPERTIES COMPILE_FLAGS ${CRPM_OPT_FLAGS})
    target_link_libraries(benchmark_lmc PUBLIC crpm_lmc numa)

    add_executable(benchmark_undolog ${BENCHMARK_FILES})
    add_dependencies(benchmark_undolog crpm-opt)
    set_target_properties(benchmark_undolog PROPERTIES COMPILE_FLAGS ${CRPM_OPT_FLAGS})
    target_link_libraries(benchmark_undolog PUBLIC crpm_undo numa)

    add_executable(benchmark_no_persistence ${BENCHMARK_FILES})
    target_link_libraries(benchmark_no_persistence PUBLIC crpm numa)
endif()
