# Configure an LLVM/Clang development build for the experimental OS400MI target.
#
# Use with:
#   CCACHE_REMOTE_STORAGE='http://buildcache.cyber.gent/|layout=bazel' \
#   cmake -C clang/cmake/caches/OS400MI.cmake -G Ninja -S llvm -B build-os400mi

set(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD "OS400MI" CACHE STRING "")
set(LLVM_TARGETS_TO_BUILD "X86" CACHE STRING "")
set(LLVM_ENABLE_PROJECTS "clang" CACHE STRING "")
set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "")

set(CMAKE_C_COMPILER_LAUNCHER "ccache" CACHE STRING "")
set(CMAKE_CXX_COMPILER_LAUNCHER "ccache" CACHE STRING "")

set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "")
set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "")
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "")
