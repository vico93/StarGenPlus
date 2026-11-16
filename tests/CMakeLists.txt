# ============================================================================
# StarGen test suite (grows from Phase B on; Phase A ships the build-identity
# probe that records compiler, CRT, and long-double precision facts).
# ============================================================================

add_executable(buildinfo buildinfo.c)
target_link_libraries(buildinfo PRIVATE stargen_core)
add_test(NAME buildinfo COMMAND buildinfo)

add_executable(test_fixtures test_fixtures.c)
target_link_libraries(test_fixtures PRIVATE stargen_core)
add_test(NAME fixtures COMMAND test_fixtures)

add_executable(test_repeat_call test_repeat_call.c)
target_link_libraries(test_repeat_call PRIVATE stargen_core)
add_test(NAME repeat_call COMMAND test_repeat_call)

add_executable(test_numfmt test_numfmt.c)
target_link_libraries(test_numfmt PRIVATE stargen_core)
add_test(NAME numfmt COMMAND test_numfmt)

add_executable(test_escape test_escape.c)
target_link_libraries(test_escape PRIVATE stargen_core)
add_test(NAME escape COMMAND test_escape)

# CSV layout regression: planet and moon rows must match the header
# width (BUG-05). Runs the CLI in stdout-CSV mode, once plain and once
# with the Solar System template + moons.
add_test(NAME csv_columns
		 COMMAND ${CMAKE_COMMAND}
				 -Dexe=$<TARGET_FILE:stargen>
				 -Dargs=-C\ -s42\ -m1\ -n1\ --
				 -P ${CMAKE_CURRENT_SOURCE_DIR}/csv_columns_check.cmake)

add_test(NAME csv_columns_moons
		 COMMAND ${CMAKE_COMMAND}
				 -Dexe=$<TARGET_FILE:stargen>
				 -Dargs=-C\ -x\ -M\ -g\ -s42\ --
				 -P ${CMAKE_CURRENT_SOURCE_DIR}/csv_columns_check.cmake)

# Headless JSON smoke tests (Phase D).
add_test(NAME json_smoke
		 COMMAND ${CMAKE_COMMAND}
				 -Dexe=$<TARGET_FILE:stargen>
				 -P ${CMAKE_CURRENT_SOURCE_DIR}/json_smoke_check.cmake)

# MSVC: run-time DLL discovery for test processes. The toolset bin
# directory holds the AddressSanitizer runtime (clang_rt.asan_dynamic-*.dll,
# needed when STARGEN_ENABLE_ASAN is on), and the debug CRT redist
# (vcruntime140d.dll etc.) is needed by any /MDd Debug binary but is not
# installed to System32. Prepending both to the test processes' PATH keeps
# ctest runs self-contained; Release runs simply ignore the extra entries.
if(MSVC)
	get_filename_component(_msvc_bin_dir "${CMAKE_C_COMPILER}" DIRECTORY)
	get_filename_component(_toolset_dir "${_msvc_bin_dir}/../../.." ABSOLUTE)	# .../MSVC/<version>
	get_filename_component(_vc_dir "${_toolset_dir}/../../.." ABSOLUTE)		# .../VC

	set(_msvc_test_path "${_msvc_bin_dir}")

	file(GLOB _msvc_dbg_crt_dirs
			 "${_vc_dir}/Redist/MSVC/*/debug_nonredist/x64/Microsoft.VC*.DebugCRT")

	foreach(_d IN LISTS _msvc_dbg_crt_dirs)
		list(APPEND _msvc_test_path "${_d}")
	endforeach()

	set_tests_properties(buildinfo fixtures repeat_call numfmt escape PROPERTIES
						 ENVIRONMENT "PATH=${_msvc_test_path};$ENV{PATH}")
endif()
