# csv_columns_check.cmake -- CSV layout regression (PLAN.MD §9)
#
# Runs the CLI in stdout-CSV mode (-C ... --), splits the result into
# rows, and requires every planet/moon row to have exactly the same
# number of comma-separated fields as the planet header row.
#
# Usage: cmake -Dexe=<path to StarGen executable> -Dargs=<CLI args>
#             -P csv_columns_check.cmake

if(NOT DEFINED exe)
	message(FATAL_ERROR "exe not defined")
endif()

separate_arguments(cli_args NATIVE_COMMAND "${args}")

execute_process(
	COMMAND "${exe}" ${cli_args}
	OUTPUT_VARIABLE csv_out
	RESULT_VARIABLE run_result
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT run_result EQUAL 0)
	message(FATAL_ERROR "StarGen exited with ${run_result}")
endif()

string(REPLACE "\r\n" "\n" csv_out "${csv_out}")

# The atmosphere description strings contain semicolons ("H2O 12% ...; "),
# which would fragment CMake lists during row splitting. Semicolons carry
# no meaning for field counting, so neutralize them first.
string(REPLACE ";" " " csv_out "${csv_out}")
string(REPLACE "\n" ";" rows "${csv_out}")

set(header_fields 0)
set(row_count 0)

foreach(row ${rows})
	if(row STREQUAL "")
		continue()
	endif()

	string(REPLACE "," ";" fields "${row}")
	list(LENGTH fields count)

	if(header_fields EQUAL 0)
		# First row is the star header (7 fields); the planet header
		# follows. Track the widest header row.
		if(count GREATER header_fields)
			set(header_fields ${count})
		endif()
		continue()
	endif()

	if(count GREATER header_fields)
		# This must be the planet header row itself.
		set(header_fields ${count})
		continue()
	endif()

	math(EXPR row_count "${row_count} + 1")

	if(NOT count EQUAL header_fields)
		message(FATAL_ERROR "CSV row width mismatch: header has ${header_fields} fields, row has ${count}: '${row}'")
	endif()
endforeach()

if(row_count LESS 1)
	message(FATAL_ERROR "no data rows in CSV output")
endif()

message(STATUS "csv_columns ok: ${row_count} rows, width ${header_fields}")
