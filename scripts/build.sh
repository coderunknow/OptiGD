#!/usr/bin/env bash
set -e

BUILD_DIR="build"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "Configuring OptiGD CMake project..."
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release

echo "Compiling OptiGD test suite and benchmark harness..."
ninja

echo "Build completed successfully."
