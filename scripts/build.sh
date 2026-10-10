#!/usr/bin/env bash
set -e

BUILD_DIR="build"

echo "Configuring OptiGD CMake project..."
if command -v ninja >/dev/null 2>&1; then
    cmake -B "${BUILD_DIR}" -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DOPTIGD_STANDALONE_ONLY=ON
else
    cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DOPTIGD_STANDALONE_ONLY=ON
fi

echo "Compiling OptiGD test suite and benchmark harness..."
cmake --build "${BUILD_DIR}" --config Release --parallel

echo "Build completed successfully."
