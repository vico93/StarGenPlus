# ============================================================================
# StarGen -- a random solar-system generator
#
# Modernized build for the StarGenPlus fork:
#   * stargen_core  -- static library holding the simulation
#                      (src/export/display.c is still part of it: the
#                       legacy orchestration in stargen.c calls it
#                       directly; it will be separated as exporters are
#                       refactored)
#   * stargen       -- the CLI executable (StarGen.exe on Windows)
#   * tests/        -- CTest-driven checks
#
# Source layout (all quote-includes stay unqualified; every subfolder is
# on the target's include path):
#     src/engine/  simulation core (accretion, environment, fixtures)
#     src/export/  output formats (text/CSV/HTML/SSC/SVG, JSON export)
#     src/mcp/     MCP stdio server + worker subprocess plumbing
#     src/app/     CLI entry point + headless one-job driver
#     third_party/jsmn/  vendored MIT JSON tokenizer (include dir)
#
# Language: C17, extensions off. Warnings are errors-as-warnings:
# project-owned code is required to be warning-clean.
# No global CRT-suppression defines are set on purpose.
#
# Configure (from any shell, using the CMake bundled with VS 2026
# Build Tools or any CMake >= 3.24):
#     cmake --preset msvc2026
#     cmake --build --preset msvc2026-release     (or msvc2026-debug)
# ============================================================================

cmake_minimum_required(VERSION 3.24)

project(StarGen VERSION 1.43.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(SG_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ----------------------------------------------------------------------------
# Simulation core
# ----------------------------------------------------------------------------
add_library(stargen_core STATIC
	${SG_SRC_DIR}/engine/accrete.c
	${SG_SRC_DIR}/export/display.c
	${SG_SRC_DIR}/engine/Dumas.c
	${SG_SRC_DIR}/engine/enviro.c
	${SG_SRC_DIR}/export/sg_escape.c
	${SG_SRC_DIR}/export/sg_export.c
	${SG_SRC_DIR}/app/sg_headless.c
	${SG_SRC_DIR}/mcp/sg_json_dom.c
	${SG_SRC_DIR}/export/sg_jsonw.c
	${SG_SRC_DIR}/mcp/sg_mcp.c
	${SG_SRC_DIR}/export/sg_numfmt.c
	${SG_SRC_DIR}/export/sg_value_status.c
	${SG_SRC_DIR}/mcp/sg_worker.c
	${SG_SRC_DIR}/engine/stargen.c
	${SG_SRC_DIR}/engine/utils.c
)

target_include_directories(stargen_core PUBLIC
	${SG_SRC_DIR}/engine
	${SG_SRC_DIR}/export
	${SG_SRC_DIR}/mcp
	${SG_SRC_DIR}/app
)

# jsmn is header-only and compiled into sg_json_dom.c (JSMN_STATIC).
target_include_directories(stargen_core PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsmn
)

# Some platforms require an explicit libm; MSVC does not.
if(UNIX)
	target_link_libraries(stargen_core PUBLIC m)
endif()

# ----------------------------------------------------------------------------
# Command-line interface
# ----------------------------------------------------------------------------
add_executable(stargen ${SG_SRC_DIR}/app/main.c)
target_link_libraries(stargen PRIVATE stargen_core)
set_target_properties(stargen PROPERTIES OUTPUT_NAME StarGen)

# ----------------------------------------------------------------------------
# Warnings
# ----------------------------------------------------------------------------
if(MSVC)
	add_compile_options(/W4 /WX)
else()
	add_compile_options(-Wall -Wextra -Werror)
endif()

# ----------------------------------------------------------------------------
# AddressSanitizer (opt-in)
# ----------------------------------------------------------------------------
option(STARGEN_ENABLE_ASAN
	   "Enable AddressSanitizer (MSVC /fsanitize=address or -fsanitize=address)"
	   OFF)

if(STARGEN_ENABLE_ASAN)
	if(MSVC)
		# MSVC's ASan requires the DYNAMIC CRT (the compiler emits import
		# thunks __imp_* for intercepted CRT functions), so the runtime
		# policy stays at its default here. The ASan runtime DLL
		# (clang_rt.asan_dynamic-x86_64.dll) is not on the system PATH;
		# tests/CMakeLists.txt adds the toolset bin directory to the test
		# processes' PATH so the loader finds it.
		add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
	else()
		add_compile_options($<$<CONFIG:Debug>:-fsanitize=address>)
		add_link_options($<$<CONFIG:Debug>:-fsanitize=address>)
	endif()
endif()

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
