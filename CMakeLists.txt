# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

project(xpilot-ng VERSION 4.7.3 LANGUAGES C CXX)

include(GNUInstallDirs)

option(XPILOT_BUILD_X11_CLIENT "Build the X11 client" ON)
option(XPILOT_BUILD_SDL_CLIENT "Build the SDL/OpenGL client" ON)
option(XPILOT_BUILD_REPLAY "Build the replay tool" ON)
option(XPILOT_BUILD_MAPEDIT "Build xp-mapedit" ON)
option(XPILOT_BUILD_SOUND "Enable OpenAL sound where supported" ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT WIN32)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Common dependencies
find_package(X11)
find_package(OpenGL)

# SDL 1.2-era project: use pkg-config to stay close to the original build.
find_package(PkgConfig REQUIRED)

if(XPILOT_BUILD_SDL_CLIENT)
    pkg_check_modules(SDL REQUIRED sdl)
    pkg_check_modules(SDL_TTF REQUIRED SDL_ttf)
    pkg_check_modules(SDL_IMAGE REQUIRED SDL_image)
endif()

if(XPILOT_BUILD_SOUND)
    pkg_check_modules(OPENAL openal)
    pkg_check_modules(ALUT freealut)
endif()

set(XPILOT_HAVE_SOUND OFF)
if(XPILOT_BUILD_SOUND AND OPENAL_FOUND AND ALUT_FOUND)
    set(XPILOT_HAVE_SOUND ON)
endif()

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/config.h.in
    ${CMAKE_BINARY_DIR}/config.h
    @ONLY
)

add_subdirectory(src)
add_subdirectory(lib)

install(FILES
    ${CMAKE_SOURCE_DIR}/mapconvert.py
    DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}
)

install(FILES
    ${CMAKE_SOURCE_DIR}/lib/defaults.txt
    ${CMAKE_SOURCE_DIR}/lib/password.txt
    ${CMAKE_SOURCE_DIR}/lib/robots.txt
    ${CMAKE_SOURCE_DIR}/lib/shipshapes.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}
)
