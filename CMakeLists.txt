# ============================================================================
# StarGen -- a random solar-system generator
#
# Modernized build (PLAN.MD Phase A) for the StarGenPlus fork:
#   * stargen_core  -- static library holding the simulation
#                      (display.c is still part of it: the legacy
#                       orchestration in stargen.c calls it directly;
#                       it will be separated as exporters are refactored)
#   * stargen       -- the CLI executable (StarGen.exe on Windows)
#   * tests/        -- CTest-driven checks (grows from Phase B on)
#
# Language: C17, extensions off. Warnings are strict but not yet
# errors-as-warnings: the codebase is still being modernized (Phase B).
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

# ----------------------------------------------------------------------------
# Simulation core
# ----------------------------------------------------------------------------
add_library(stargen_core STATIC
	accrete.c
	display.c
	Dumas.c
	enviro.c
	sg_escape.c
	sg_numfmt.c
	sg_value_status.c
	stargen.c
	utils.c
)
target_include_directories(stargen_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Some platforms require an explicit libm; MSVC does not.
if(UNIX)
	target_link_libraries(stargen_core PUBLIC m)
endif()

# ----------------------------------------------------------------------------
# Command-line interface
# ----------------------------------------------------------------------------
add_executable(stargen main.c)
target_link_libraries(stargen PRIVATE stargen_core)
set_target_properties(stargen PROPERTIES OUTPUT_NAME StarGen)

# ----------------------------------------------------------------------------
# Warnings: strict. /WX once the compatibility cleanup is complete
# (PLAN.MD §6) -- Phase B requires project-owned code to be warning-clean.
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
