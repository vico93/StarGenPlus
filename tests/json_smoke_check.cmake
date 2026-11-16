# json_smoke_check.cmake -- headless JSON smoke tests (PLAN.MD Phase D)
#
# Verifies, using the rebuilt CLI:
#   1. --json produces exit 0, a single document starting with '{',
#      containing schema_version 1 and no non-finite JSON tokens
#      (NaN / Infinity would appear verbatim if sentinels leaked).
#   2. Two identical runs are byte-identical (reproducibility).
#   3. Invalid invocations exit 2 (strict argument validation).

if(NOT DEFINED exe)
	message(FATAL_ERROR "exe not defined")
endif()

# --- 1. happy path, twice --------------------------------------------------

foreach(n 1 2)
	execute_process(
		COMMAND "${exe}" --json --seed 42 --mass-solar 1.0
		OUTPUT_VARIABLE json_out_${n}
		ERROR_VARIABLE json_err_${n}
		RESULT_VARIABLE rc_${n}
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	if(NOT rc_${n} EQUAL 0)
		message(FATAL_ERROR "--json run ${n} exited with ${rc_${n}}")
	endif()

	if(NOT json_err_${n} STREQUAL "")
		message(FATAL_ERROR "--json run ${n} wrote to stderr: '${json_err_${n}}'")
	endif()

	string(SUBSTRING "${json_out_${n}}" 0 1 first_char)
	if(NOT first_char STREQUAL "{")
		message(FATAL_ERROR "output does not start with '{'")
	endif()

	string(FIND "${json_out_${n}}" "\"schema_version\":1" pos)
	if(pos EQUAL -1)
		message(FATAL_ERROR "schema_version 1 not found")
	endif()

	foreach(bad_token IN ITEMS "NaN" "Infinity" "-Infinity")
		string(FIND "${json_out_${n}}" "${bad_token}" pos)
		if(NOT pos EQUAL -1)
			message(FATAL_ERROR "non-finite token '${bad_token}' leaked into JSON output")
		endif()
	endforeach()
endforeach()

if(NOT json_out_1 STREQUAL json_out_2)
	message(FATAL_ERROR "two identical --json runs produced different output")
endif()

# --- 2. error handling ------------------------------------------------------

set(error_cases
	"--json --catalog dole"
	"--json --star-index 3"
	"--json --catalog dole --star-index 1 --mass-solar 1.0"
	"--json --mass-solar 2.0"
	"--json --seed abc"
	"--json --bogus"
	"--list-stars-json")

foreach(case_args IN LISTS error_cases)
	separate_arguments(case_args NATIVE_COMMAND "${case_args}")
	execute_process(
		COMMAND "${exe}" ${case_args}
		OUTPUT_VARIABLE err_out
		ERROR_VARIABLE err_text
		RESULT_VARIABLE err_rc
	)

	if(NOT err_rc EQUAL 2)
		message(FATAL_ERROR "arguments '${case_args}' exited with ${err_rc}, expected 2")
	endif()
endforeach()

message(STATUS "json_smoke ok")
