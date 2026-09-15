#!/usr/bin/env bash
set -e

if [ ! -f "build/optigd_benchmark" ]; then
    echo "Executable build/optigd_benchmark not found. Running scripts/build.sh first..."
    bash scripts/build.sh
fi

./build/optigd_benchmark
