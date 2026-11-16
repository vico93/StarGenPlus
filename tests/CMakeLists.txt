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
