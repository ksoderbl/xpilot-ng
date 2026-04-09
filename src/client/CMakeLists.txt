# src/client/CMakeLists.txt
file(GLOB CLIENT_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

set(_client_core_sources ${CLIENT_CORE_SOURCES})

if(NOT XPILOT_HAVE_SOUND)
    list(FILTER _client_core_sources EXCLUDE REGEX ".*/caudio\\.c$")
    list(FILTER _client_core_sources EXCLUDE REGEX ".*/oalaudio\\.c$")
endif()

add_library(xpclient STATIC ${_client_core_sources})

target_include_directories(xpclient
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_BINARY_DIR}
)

target_compile_definitions(xpclient
    PUBLIC
        HAVE_CONFIG_H=1
        CONF_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/"
)

target_link_libraries(xpclient
    PUBLIC
        xpcommon
        ZLIB::ZLIB
)

if(XPILOT_BUILD_X11_CLIENT)
    add_subdirectory(x11)
endif()

if(XPILOT_BUILD_SDL_CLIENT)
    add_subdirectory(sdl)
endif()
