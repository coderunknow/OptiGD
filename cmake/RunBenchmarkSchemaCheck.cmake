# Runs the synthetic benchmark harness and schema-validates its JSON output.
set(BENCH_JSON "${OPTIGD_OUTDIR}/benchmark_results.json")

execute_process(
    COMMAND "${OPTIGD_BENCHMARK}" "${BENCH_JSON}"
    RESULT_VARIABLE BENCH_RESULT
    OUTPUT_VARIABLE BENCH_OUTPUT
    ERROR_VARIABLE BENCH_ERROR
)
if (NOT BENCH_RESULT EQUAL 0)
    message(FATAL_ERROR "optigd_benchmark failed (${BENCH_RESULT}):\n${BENCH_ERROR}")
endif()

execute_process(
    COMMAND "${OPTIGD_PYTHON}" "${OPTIGD_SCHEMA}" "${BENCH_JSON}"
    RESULT_VARIABLE SCHEMA_RESULT
    OUTPUT_VARIABLE SCHEMA_OUTPUT
    ERROR_VARIABLE SCHEMA_ERROR
)
message(STATUS "${SCHEMA_OUTPUT}")
if (NOT SCHEMA_RESULT EQUAL 0)
    message(FATAL_ERROR "Benchmark JSON schema validation failed:\n${SCHEMA_ERROR}")
endif()
