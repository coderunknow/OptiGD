cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(OptiGD VERSION 0.1.0 LANGUAGES CXX)

set(OPTIGD_VERSION_STRING "0.1.0-beta1")

# Collect core OptiGD source files (platform-agnostic C++20 engine)
set(OPTIGD_CORE_SOURCES
    src/core/Logger.cpp
    src/core/MemoryArena.cpp
    src/core/SpatialHash.cpp
    src/core/OptiGDManager.cpp
    src/optimization/VisibilityCuller.cpp
    src/optimization/ParticleGovernor.cpp
    src/optimization/TriggerOptimizer.cpp
    src/optimization/MatrixCache.cpp
    src/adaptive/AdaptiveGovernor.cpp
    src/adaptive/FallbackManager.cpp
    src/profiling/Profiler.cpp
    src/diagnostics/DiagnosticsEngine.cpp
    src/compatibility/CompatibilityLayer.cpp
    src/config/ConfigManager.cpp
    src/platform/Platform.cpp
    src/ui/DiagnosticsOverlay.cpp
    src/benchmark/BenchmarkSuite.cpp
)

# ---------------------------------------------------------------------------
# Option A: Geode Mod Target (Activated when GEODE_SDK environment var exists)
# ---------------------------------------------------------------------------
if (DEFINED ENV{GEODE_SDK} AND NOT OPTIGD_STANDALONE_ONLY)
    message(STATUS "Building OptiGD v${OPTIGD_VERSION_STRING} as a Geode SDK Mod using SDK at: $ENV{GEODE_SDK}")

    if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
        set(CMAKE_OSX_ARCHITECTURES "arm64")
    endif()
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)

    add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

    add_library(${PROJECT_NAME} SHARED
        ${OPTIGD_CORE_SOURCES}
        src/geode/Hooks.cpp
    )

    target_include_directories(${PROJECT_NAME} PRIVATE src)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        GEODE_IS_COMPILING=1
        OPTIGD_VERSION="${OPTIGD_VERSION_STRING}"
    )

    setup_geode_mod(${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Option B: Standalone Core, Unit Test Suite, & Benchmark Harness
# ---------------------------------------------------------------------------
else()
    message(STATUS "Building OptiGD v${OPTIGD_VERSION_STRING} Standalone Engine, Unit Tests, and Benchmark Harness")

    add_library(optigd_core STATIC ${OPTIGD_CORE_SOURCES})
    target_include_directories(optigd_core PUBLIC src)
    target_compile_definitions(optigd_core PUBLIC OPTIGD_VERSION="${OPTIGD_VERSION_STRING}")

    if (MSVC)
        target_compile_options(optigd_core PRIVATE /W4 /O2 /permissive-)
    else()
        target_compile_options(optigd_core PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()

    # Unit & Integration Tests
    enable_testing()
    add_executable(optigd_tests tests/UnitTests.cpp)
    target_link_libraries(optigd_tests PRIVATE optigd_core)
    add_test(NAME OptiGDUnitTests COMMAND optigd_tests)

    # Benchmark Harness
    add_executable(optigd_benchmark benchmarks/main.cpp)
    target_link_libraries(optigd_benchmark PRIVATE optigd_core)
endif()
