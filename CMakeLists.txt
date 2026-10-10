cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(OptiGD VERSION 0.1.0 LANGUAGES CXX)

set(OPTIGD_VERSION_STRING "0.1.0-beta.2")

# Collect core OptiGD source files (platform-agnostic C++20 engine)
set(OPTIGD_CORE_SOURCES
    src/core/Logger.cpp
    src/core/GeodeVersion.cpp
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

    add_library(${PROJECT_NAME} SHARED
        ${OPTIGD_CORE_SOURCES}
        src/geode/Hooks.cpp
    )

    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        GEODE_IS_COMPILING=1
        OPTIGD_VERSION="${OPTIGD_VERSION_STRING}"
    )

    add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
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
        target_compile_definitions(optigd_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(optigd_core PRIVATE /W4 /O2 /permissive-)
    else()
        target_compile_options(optigd_core PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()

    # Unit & Integration Tests
    enable_testing()
    add_executable(optigd_tests tests/UnitTests.cpp)
    target_link_libraries(optigd_tests PRIVATE optigd_core)
    add_test(NAME OptiGDUnitTests COMMAND optigd_tests)

    # optigd_tests can also emit a real diagnostics JSON report so the artefact
    # schema can be validated out-of-process by tools/validate_diagnostics_json.py
    add_test(NAME OptiGDDiagnosticsJson
        COMMAND ${CMAKE_COMMAND}
            -DOPTIGD_TESTS=$<TARGET_FILE:optigd_tests>
            -DOPTIGD_OUTDIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmitDiagnosticsReport.cmake)

    # Geode mod.json / settings-schema validation against the pinned SDK rules
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if (Python3_Interpreter_FOUND)
        add_test(NAME OptiGDGeodeMetadataValidation
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_geode_metadata.py
                ${CMAKE_CURRENT_SOURCE_DIR}/mod.json)
    else()
        message(WARNING "Python3 interpreter not found - skipping the mod.json metadata validation test")
    endif()

    # Benchmark Harness
    add_executable(optigd_benchmark benchmarks/main.cpp)
    target_link_libraries(optigd_benchmark PRIVATE optigd_core)

    if (Python3_Interpreter_FOUND)
        add_test(NAME OptiGDBenchmarkJsonSchema
            COMMAND ${CMAKE_COMMAND}
                -DOPTIGD_BENCHMARK=$<TARGET_FILE:optigd_benchmark>
                -DOPTIGD_PYTHON=${Python3_EXECUTABLE}
                -DOPTIGD_SCHEMA=${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_benchmark_json.py
                -DOPTIGD_OUTDIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBenchmarkSchemaCheck.cmake)
    endif()
endif()
