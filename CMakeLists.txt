cmake_minimum_required(VERSION 3.21)

project(OptiGD VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# Core library source files (platform and engine independent)
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

# Core static library
add_library(optigd_core STATIC ${OPTIGD_CORE_SOURCES})
target_include_directories(optigd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Standalone Unit Test Executable
add_executable(optigd_tests tests/UnitTests.cpp)
target_link_libraries(optigd_tests PRIVATE optigd_core)
target_include_directories(optigd_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Standalone Benchmark Executable
add_executable(optigd_benchmark benchmarks/main.cpp)
target_link_libraries(optigd_benchmark PRIVATE optigd_core)
target_include_directories(optigd_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Geode Mod Target (only when GEODE_SDK is available)
if (DEFINED ENV{GEODE_SDK} OR DEFINED GEODE_SDK)
    if (NOT DEFINED GEODE_SDK)
        set(GEODE_SDK $ENV{GEODE_SDK})
    endif()

    message(STATUS "Geode SDK detected: ${GEODE_SDK}")
    if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
        set(CMAKE_OSX_ARCHITECTURES "arm64")
    else()
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()

    # Create mod library with geode hooks
    add_library(${PROJECT_NAME} SHARED
        ${OPTIGD_CORE_SOURCES}
        src/geode/Hooks.cpp
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE GEODE_MOD_ID="coderunknow.optigd")
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_subdirectory(${GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
    setup_geode_mod(${PROJECT_NAME})
else()
    message(STATUS "GEODE_SDK environment variable not set; skipping Geode package target. Building native core library, test suite, and benchmark harness.")
endif()
