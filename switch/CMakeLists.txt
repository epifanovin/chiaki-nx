set(CHIAKI_SWITCH_RENDERER "DEKO3D" CACHE STRING "Switch video renderer backend (DEKO3D or OPENGL)")
set_property(CACHE CHIAKI_SWITCH_RENDERER PROPERTY STRINGS DEKO3D OPENGL)
string(TOUPPER "${CHIAKI_SWITCH_RENDERER}" CHIAKI_SWITCH_RENDERER)
if(NOT CHIAKI_SWITCH_RENDERER STREQUAL "DEKO3D" AND NOT CHIAKI_SWITCH_RENDERER STREQUAL "OPENGL")
	message(FATAL_ERROR "CHIAKI_SWITCH_RENDERER must be DEKO3D or OPENGL")
endif()

set(_chiaki_switch_deko_default OFF)
if(CHIAKI_SWITCH_RENDERER STREQUAL "DEKO3D")
	set(_chiaki_switch_deko_default ON)
endif()
option(CHIAKI_SWITCH_DEKO3D "Enable Deko3D renderer backend for Switch" ${_chiaki_switch_deko_default})
if(CHIAKI_SWITCH_DEKO3D)
	set(CHIAKI_SWITCH_RENDERER "DEKO3D")
else()
	set(CHIAKI_SWITCH_RENDERER "OPENGL")
endif()

##################
# borealis dependency
##################

if(CHIAKI_IS_SWITCH)
	set(PLATFORM_SWITCH ON CACHE BOOL "" FORCE)
	set(PLATFORM_DESKTOP OFF CACHE BOOL "" FORCE)
	set(USE_DEKO3D ${CHIAKI_SWITCH_DEKO3D} CACHE BOOL "" FORCE)
	set(BOREALIS_USE_DEKO3D ${CHIAKI_SWITCH_DEKO3D} CACHE BOOL "" FORCE)
	set(USE_GLFW ON CACHE BOOL "" FORCE)
	set(USE_SDL2 OFF CACHE BOOL "" FORCE)
else()
	set(PLATFORM_SWITCH OFF CACHE BOOL "" FORCE)
	set(PLATFORM_DESKTOP ON CACHE BOOL "" FORCE)
	set(USE_DEKO3D OFF CACHE BOOL "" FORCE)
	set(BOREALIS_USE_DEKO3D OFF CACHE BOOL "" FORCE)
	set(USE_GLFW ON CACHE BOOL "" FORCE)
endif()
set(USE_LIBROMFS OFF CACHE BOOL "" FORCE)

add_subdirectory(borealis/library)

##################
# chiaki with borealis
##################

find_package(FFMPEG REQUIRED COMPONENTS avcodec avutil swscale)
find_library(SDL2 SDL2)
find_library(SWRESAMPLE swresample)
find_library(DAV1D dav1d)

add_executable(chiaki-borealis WIN32
	src/views/enter_pin_view.cpp
	src/views/ps_remote_play.cpp
	src/discoverymanager.cpp
	src/settings.cpp
	src/io.cpp
	src/deko_video_renderer.cpp
	src/host.cpp
	src/main.cpp
	src/gui.cpp)

set_target_properties(chiaki-borealis PROPERTIES
	CXX_STANDARD 17
	OUTPUT_NAME chiaki)

target_include_directories(chiaki-borealis PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/borealis/library/include/borealis/extern/glad)
if(CHIAKI_SWITCH_DEKO3D)
	target_compile_definitions(chiaki-borealis PRIVATE CHIAKI_SWITCH_RENDERER_DEKO3D=1)
else()
	target_compile_definitions(chiaki-borealis PRIVATE CHIAKI_SWITCH_RENDERER_OPENGL=1)
endif()

target_link_libraries(chiaki-borealis
	chiaki-lib
	borealis
	${SDL2}
	dav1d
	FFMPEG::avcodec
	FFMPEG::avutil
	FFMPEG::swscale
	${SWRESAMPLE})

if(CHIAKI_IS_SWITCH)
	# libnx is forced by the switch toolchain
	find_library(Z z)
	find_library(EGL_LIB EGL)
	find_library(GLESV2_LIB GLESv2)
	find_library(GLAPI glapi) # TODO: make it transitive from borealis
	find_library(DRM_NOUVEAU drm_nouveau) # TODO: make it transitive from borealis
	target_link_libraries(chiaki-borealis ${Z} ${EGL_LIB} ${GLESV2_LIB} ${GLAPI} ${DRM_NOUVEAU})
endif()

install(TARGETS chiaki-borealis
	RUNTIME DESTINATION bin
	BUNDLE DESTINATION bin)

if(CHIAKI_IS_SWITCH)
	set(CHIAKI_SWITCH_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/switch_romfs")
	set(CHIAKI_SWITCH_SHADER_DIR "${CHIAKI_SWITCH_ROMFS_DIR}/shaders")
	set(CHIAKI_BOREALIS_DEKO_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/borealis/library/lib/extern/nanovg/deko3d/shaders")

	if(CHIAKI_SWITCH_DEKO3D)
		if(NOT NX_UAM_EXE)
			find_program(NX_UAM_EXE uam HINTS "${DEVKITPRO}/tools/bin")
		endif()
		if(NOT NX_UAM_EXE)
			message(FATAL_ERROR "NX_UAM_EXE/uam is required to compile deko shaders")
		endif()
		add_custom_target(chiaki_switch_romfs_assets ALL
			COMMAND ${CMAKE_COMMAND} -E rm -rf "${CHIAKI_SWITCH_ROMFS_DIR}"
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/res" "${CHIAKI_SWITCH_ROMFS_DIR}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CHIAKI_SWITCH_ROMFS_DIR}/font"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CHIAKI_SWITCH_ROMFS_DIR}/inter/switch_font.ttf" "${CHIAKI_SWITCH_ROMFS_DIR}/font/font.ttf"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CHIAKI_SWITCH_ROMFS_DIR}/inter/switch_icons.ttf" "${CHIAKI_SWITCH_ROMFS_DIR}/font/icon.ttf"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CHIAKI_SWITCH_SHADER_DIR}"
			COMMAND ${NX_UAM_EXE} -s vert -o "${CHIAKI_SWITCH_SHADER_DIR}/basic_vsh.dksh" "${CMAKE_CURRENT_SOURCE_DIR}/src/deko3d/basic_vsh.glsl"
			COMMAND ${NX_UAM_EXE} -s frag -o "${CHIAKI_SWITCH_SHADER_DIR}/texture_fsh.dksh" "${CMAKE_CURRENT_SOURCE_DIR}/src/deko3d/texture_fsh.glsl"
			COMMAND ${NX_UAM_EXE} -s vert -o "${CHIAKI_SWITCH_SHADER_DIR}/fill_vsh.dksh" "${CHIAKI_BOREALIS_DEKO_SHADER_DIR}/fill_vsh.glsl"
			COMMAND ${NX_UAM_EXE} -s frag -o "${CHIAKI_SWITCH_SHADER_DIR}/fill_fsh.dksh" "${CHIAKI_BOREALIS_DEKO_SHADER_DIR}/fill_fsh.glsl"
			COMMAND ${NX_UAM_EXE} -s frag -o "${CHIAKI_SWITCH_SHADER_DIR}/fill_aa_fsh.dksh" "${CHIAKI_BOREALIS_DEKO_SHADER_DIR}/fill_aa_fsh.glsl"
			VERBATIM)
	else()
		add_custom_target(chiaki_switch_romfs_assets ALL
			COMMAND ${CMAKE_COMMAND} -E rm -rf "${CHIAKI_SWITCH_ROMFS_DIR}"
			COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/res" "${CHIAKI_SWITCH_ROMFS_DIR}"
			VERBATIM)
	endif()

	add_dependencies(chiaki-borealis chiaki_switch_romfs_assets)

	add_nro_target(chiaki
		chiaki-borealis
		"Chiaki-ng"
		"StreetPea, kkWong, xlanor, H0neyBadger and thestr4ng3r"
		"${CHIAKI_VERSION}"
		"${CMAKE_CURRENT_SOURCE_DIR}/nro_icon.jpg"
		"${CHIAKI_SWITCH_ROMFS_DIR}")
	add_dependencies(chiaki_nro chiaki_switch_romfs_assets)
endif()
