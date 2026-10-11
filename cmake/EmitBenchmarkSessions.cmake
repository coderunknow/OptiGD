# Emits real benchmark session records via `optigd_tests --emit-sessions` and
# validates them out of process with tools/validate_benchmark_session_json.py.
# Used by the OptiGDBenchmarkSessionJson ctest.
#
# The sessions are produced by the actual BenchmarkController, BenchmarkSession
# and BenchmarkStore code with a synthetic clock and a temporary directory: no
# Geometry Dash process is involved, so every record is stamped [SIMULATED] and
# the validator enforces that. This test proves the record format and its
# invariants; it is never evidence about frame rates.
execute_process(
    COMMAND "${OPTIGD_TESTS}" --emit-sessions "${OPTIGD_OUTDIR}"
    RESULT_VARIABLE EMIT_RESULT
    OUTPUT_VARIABLE EMIT_OUTPUT
    ERROR_VARIABLE EMIT_ERROR
)
message(STATUS "${EMIT_OUTPUT}")
if (NOT EMIT_RESULT EQUAL 0)
    message(FATAL_ERROR "optigd_tests --emit-sessions failed (${EMIT_RESULT}):\n${EMIT_ERROR}")
endif()

if (NOT EXISTS "${OPTIGD_PYTHON}")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if (NOT Python3_Interpreter_FOUND)
        message(WARNING "Python3 not found - skipping benchmark session JSON validation")
        return()
    endif()
    set(OPTIGD_PYTHON "${Python3_EXECUTABLE}")
endif()

execute_process(
    COMMAND "${OPTIGD_PYTHON}" "${OPTIGD_SCHEMA}" "${OPTIGD_OUTDIR}"
    RESULT_VARIABLE SCHEMA_RESULT
    OUTPUT_VARIABLE SCHEMA_OUTPUT
    ERROR_VARIABLE SCHEMA_ERROR
)
message(STATUS "${SCHEMA_OUTPUT}")
if (NOT SCHEMA_RESULT EQUAL 0)
    message(FATAL_ERROR "Benchmark session JSON schema validation failed:\n${SCHEMA_OUTPUT}\n${SCHEMA_ERROR}")
endif()
