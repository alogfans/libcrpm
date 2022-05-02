include_directories(include)

set(CRPM_RUNTIME_FILES
        include/crpm.h
        include/internal/pptr.h
        include/internal/common.h
        include/internal/allocator.h
        include/internal/filesystem.h
        include/internal/engine.h
        include/internal/metadata.h
        include/internal/allocators/lrmalloc_util.h
        include/internal/engines/noop_engine.h
        include/internal/engines/nvm_engine.h
        include/internal/allocators/lrmalloc_allocator.h
        include/internal/allocators/trivial_allocator.h
        include/internal/engines/undolog_engine.h
        include/internal/engines/lmc_engine.h
        include/internal/engines/mprotect_engine.h
        include/internal/engines/dirtybit_engine.h
        include/internal/engines/nvm_inst_engine.h
        include/internal/engines/hybrid_inst_engine.h
        include/internal/allocators/hook_lrmalloc_allocator.h
        include/internal/checkpoint.h
        src/checkpoint.cpp
        src/crpm.cpp
        src/common.cpp
        src/allocator.cpp
        src/filesystem.cpp
        src/engine.cpp
        src/lrmalloc_util.cpp
        src/engines/nvm_engine.cpp
        src/engines/undolog_engine.cpp
        src/engines/lmc_engine.cpp
        src/engines/mprotect_engine.cpp
        src/engines/dirtybit_engine.cpp
        src/engines/nvm_inst_engine.cpp
        src/engines/hybrid_inst_engine.cpp
        src/allocators/lrmalloc_allocator.cpp
        src/allocators/hook_lrmalloc_allocator.cpp
        src/metadata.cpp)

set(CRPM_MPI_RUNTIME_FILES
        ${CRPM_RUNTIME_FILES}
        include/crpm_mpi.h
        src/crpm_mpi.cpp)

if (DEFINED ENV{BLOCK_SHIFT})
    add_definitions(-DBLOCK_SHIFT=$ENV{BLOCK_SHIFT})
endif ()

if (DEFINED ENV{SEGMENT_SHIFT})
    add_definitions(-DSEGMENT_SHIFT=$ENV{SEGMENT_SHIFT})
endif ()

set(CRPM_OPT_PATH "${CMAKE_BINARY_DIR}/instrumentation/libcrpm-opt.so")
set(CRPM_OPT_FLAGS "-Xclang -load -Xclang ${CRPM_OPT_PATH} -fno-unroll-loops")
set_source_files_properties(src/allocators/hook_lrmalloc_allocator.cpp PROPERTIES COMPILE_FLAGS ${CRPM_OPT_FLAGS})

add_library(crpm ${CRPM_RUNTIME_FILES})
target_compile_definitions(crpm PUBLIC -DUSE_NVM_INST_ENGINE)
add_dependencies(crpm crpm-opt)
target_link_libraries(crpm PUBLIC pthread)

if (FULL_BUILD)
    add_library(crpm_lmc ${CRPM_RUNTIME_FILES})
    target_compile_definitions(crpm_lmc PUBLIC -DUSE_LMC_ENGINE)
    add_dependencies(crpm_lmc crpm-opt)
    target_link_libraries(crpm_lmc PUBLIC pthread)

    add_library(crpm_undo ${CRPM_RUNTIME_FILES})
    target_compile_definitions(crpm_undo PUBLIC -DUSE_UNDOLOG_ENGINE)
    add_dependencies(crpm_undo crpm-opt)
    target_link_libraries(crpm_undo PUBLIC pthread)
endif()

add_library(crpm_mpi ${CRPM_MPI_RUNTIME_FILES})
target_compile_definitions(crpm_mpi PUBLIC -DUSE_HYBRID_INST_ENGINE -DUSE_MPI_EXTENSION)
add_dependencies(crpm_mpi crpm-opt)
target_link_libraries(crpm_mpi PUBLIC pthread)

execute_process(COMMAND mpicc --showme:compile OUTPUT_VARIABLE MPI_COMPILE_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND mpicc --showme:link OUTPUT_VARIABLE MPI_LINK_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
separate_arguments(MPI_COMPILE_FLAGS)
separate_arguments(MPI_LINK_FLAGS)
target_compile_options(crpm_mpi PUBLIC ${MPI_COMPILE_FLAGS})
target_link_libraries(crpm_mpi PUBLIC ${MPI_LINK_FLAGS})
