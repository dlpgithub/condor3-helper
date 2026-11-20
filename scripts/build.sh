#!/usr/bin/env bash
# Builds build/c3helper.exe (64-bit Windows) inside the dev container image "c3dev" of condor3-mod (MinGW-w64 has both 32 and 64 bit).
set -euo pipefail
cd "$(dirname "$0")/.."
docker image inspect c3dev >/dev/null 2>&1 || docker build -t c3dev ../condor3-mod/.devcontainer
docker run --rm -v "$PWD":/workspace c3dev bash -lc "cd /workspace && cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw64-toolchain.cmake -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j4 && i686-w64-mingw32-gcc -O2 -Wall -Iinclude -Isrc tools/c3htest.c src/c3h_client.c -o build/c3htest.exe -static"
[ -d ../condor3-mod/build ] && cp build/c3helper.exe ../condor3-mod/build/ && echo "copied to ../condor3-mod/build/c3helper.exe (installed by its postfx.sh / postfx.bat)"
