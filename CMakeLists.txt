cmake_minimum_required(VERSION 3.15)
project(c3helper C)

# 64-bit Windows helper for the Condor 3 mod. Built with MinGW-w64 (x86_64) in the c3dev container: scripts/build.sh
add_executable(c3helper src/helper.c)
target_include_directories(c3helper PRIVATE include)
target_compile_options(c3helper PRIVATE -O2 -Wall -Wno-misleading-indentation)
target_link_options(c3helper PRIVATE -static -static-libgcc)
