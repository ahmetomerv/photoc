#!/bin/sh
set -eu

cmake -S . -B build-san -DPHOTOC_SANITIZERS=ON
cmake --build build-san
ctest --test-dir build-san --output-on-failure
