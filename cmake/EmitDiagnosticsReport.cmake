# Emits a real diagnostics JSON report via optigd_tests and validates it with
# tools/validate_diagnostics_json.py. Used by the OptiGDDiagnosticsJson ctest.
set(REPORT "${OPTIGD_OUTDIR}/optigd_diagnostics.json")

execute_process(
    COMMAND "${OPTIGD_TESTS}" --emit-report "${REPORT}"
    RESULT_VARIABLE EMIT_RESULT
    OUTPUT_VARIABLE EMIT_OUTPUT
    ERROR_VARIABLE EMIT_ERROR
)
if (NOT EMIT_RESULT EQUAL 0)
    message(FATAL_ERROR "optigd_tests --emit-report failed (${EMIT_RESULT}):\n${EMIT_ERROR}")
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if (NOT Python3_Interpreter_FOUND)
    message(WARNING "Python3 not found - skipping diagnostics JSON schema validation")
    return()
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/validate_diagnostics_json.py" "${REPORT}"
    RESULT_VARIABLE SCHEMA_RESULT
    OUTPUT_VARIABLE SCHEMA_OUTPUT
    ERROR_VARIABLE SCHEMA_ERROR
)
message(STATUS "${SCHEMA_OUTPUT}")
if (NOT SCHEMA_RESULT EQUAL 0)
    message(FATAL_ERROR "Diagnostics JSON schema validation failed:\n${SCHEMA_ERROR}")
endif()
