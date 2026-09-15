#!/usr/bin/env bash
set -e

if [ ! -f "build/optigd_tests" ]; then
    echo "Executable build/optigd_tests not found. Running scripts/build.sh first..."
    bash scripts/build.sh
fi

./build/optigd_tests
