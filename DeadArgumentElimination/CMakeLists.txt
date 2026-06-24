cmake_minimum_required(VERSION 3.20)
project(DeadArgumentEliminationPass)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

add_library(DeadArgumentEliminationPass MODULE DeadArgumentEliminationPass.cpp)
target_compile_features(DeadArgumentEliminationPass PRIVATE cxx_std_17)
