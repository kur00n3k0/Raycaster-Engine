cmake_minimum_required(VERSION 3.20)
project(Raycaster LANGUAGES CXX)

option(RC_USE_ASM "Use the NASM inner loops instead of the C++ reference routines" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenAL REQUIRED)
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth>=2.0)

set(RC_WARNINGS -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)

# ---------------------------------------------------------------------------
# rc_core: the inner loops. Always contains the C++ reference routines; with
# RC_USE_ASM it also contains the NASM versions.
# ---------------------------------------------------------------------------
add_library(rc_core STATIC src/Reference.cpp)
target_include_directories(rc_core PUBLIC include)
target_compile_options(rc_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${RC_WARNINGS}>)

if(RC_USE_ASM)
	enable_language(ASM_NASM)
	set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)
	set(CMAKE_ASM_NASM_FLAGS_DEBUG "-g -F dwarf")
	target_sources(rc_core PRIVATE
		src/asm/fb_clear.asm
		src/asm/draw_column.asm
		src/asm/draw_span.asm
		src/asm/draw_sprite_col.asm
	)
	target_compile_definitions(rc_core PUBLIC RC_USE_ASM=1)
else()
	target_compile_definitions(rc_core PUBLIC RC_USE_ASM=0)
endif()

# ---------------------------------------------------------------------------
# The game
# ---------------------------------------------------------------------------
add_executable(raycaster
	src/main.cpp
	src/Window.cpp
	src/GLCheck.cpp
	src/Video.cpp
	src/Palette.cpp
	src/Map.cpp
	src/Game.cpp
	src/Raycaster.cpp
	src/Textures.cpp
	src/Sprites.cpp
	src/Audio.cpp
	src/Midi.cpp
	src/Synth.cpp
	src/Hud.cpp
	src/Config.cpp
)
target_compile_options(raycaster PRIVATE ${RC_WARNINGS})
target_compile_definitions(raycaster PRIVATE $<$<CONFIG:Debug>:RC_DEBUG=1>)
target_link_libraries(raycaster PRIVATE
	rc_core
	glfw
	GLEW::GLEW
	OpenGL::GL
	OpenAL::OpenAL
	glm::glm
	PkgConfig::FLUIDSYNTH
)

# ---------------------------------------------------------------------------
# Asset tools (run from the repo root, output is checked in)
# ---------------------------------------------------------------------------
add_executable(gen_textures tools/gen_textures.cpp src/Palette.cpp)
target_include_directories(gen_textures PRIVATE include)
target_compile_options(gen_textures PRIVATE ${RC_WARNINGS})

add_executable(gen_sounds tools/gen_sounds.cpp)
target_compile_options(gen_sounds PRIVATE ${RC_WARNINGS})

# Renders a .mid through the engine's music path (FluidSynth) to a .wav, to check music offline.
add_executable(render_midi tools/render_midi.cpp src/Midi.cpp src/Synth.cpp)
target_include_directories(render_midi PRIVATE include)
target_compile_options(render_midi PRIVATE ${RC_WARNINGS})
target_link_libraries(render_midi PRIVATE PkgConfig::FLUIDSYNTH)

# ---------------------------------------------------------------------------
# ASM vs C++ equivalence tests
# ---------------------------------------------------------------------------
if(RC_USE_ASM)
	enable_testing()
	add_executable(asm_equivalence tests/asm_equivalence.cpp)
	target_compile_options(asm_equivalence PRIVATE ${RC_WARNINGS})
	target_link_libraries(asm_equivalence PRIVATE rc_core)
	add_test(NAME asm_equivalence COMMAND asm_equivalence)

	# ASM vs C++ timing; meaningful in a Release build.
	add_executable(bench_routines tools/bench_routines.cpp)
	target_compile_options(bench_routines PRIVATE ${RC_WARNINGS})
	target_link_libraries(bench_routines PRIVATE rc_core)
endif()
