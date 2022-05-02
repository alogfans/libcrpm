find_package(LLVM 10.0.0 REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
link_directories(${LLVM_LIBRARY_DIRS})

add_library(crpm-opt MODULE main.cpp context.h context.cpp preprocessor.cpp module.cpp optimization.cpp)
