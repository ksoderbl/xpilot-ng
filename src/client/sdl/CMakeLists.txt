# src/client/sdl/CMakeLists.txt
if(NOT OPENGL_FOUND)
    message(FATAL_ERROR "XPILOT_BUILD_SDL_CLIENT is ON, but OpenGL was not found.")
endif()

file(GLOB SDL_CLIENT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

# Keep the original automake behavior: choose one gameloop source.
list(FILTER SDL_CLIENT_SOURCES EXCLUDE REGEX ".*/gameloop\\.c$")
list(FILTER SDL_CLIENT_SOURCES EXCLUDE REGEX ".*/gameloop_x\\.c$")

if(UNIX AND NOT WIN32)
    list(APPEND SDL_CLIENT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/gameloop_x.c")
else()
    list(APPEND SDL_CLIENT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/gameloop.c")
endif()

add_executable(xpilot-ng-sdl ${SDL_CLIENT_SOURCES})

target_include_directories(xpilot-ng-sdl
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/client
        ${CMAKE_BINARY_DIR}
        ${SDL_INCLUDE_DIRS}
        ${SDL_TTF_INCLUDE_DIRS}
        ${SDL_IMAGE_INCLUDE_DIRS}
)

target_compile_definitions(xpilot-ng-sdl
    PRIVATE
        HAVE_CONFIG_H=1
        CONF_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/"
)

target_link_libraries(xpilot-ng-sdl
    PRIVATE
        xpclient
        xpcommon
        ${SDL_LIBRARIES}
        ${SDL_TTF_LIBRARIES}
        ${SDL_IMAGE_LIBRARIES}
        OpenGL::GL
)

if(TARGET OpenGL::GLU)
    target_link_libraries(xpilot-ng-sdl PRIVATE OpenGL::GLU)
endif()

if(X11_FOUND)
    target_link_libraries(xpilot-ng-sdl PRIVATE ${X11_LIBRARIES})
endif()

if(XPILOT_HAVE_SOUND)
    target_link_libraries(xpilot-ng-sdl PRIVATE ${OPENAL_LIBRARIES} ${ALUT_LIBRARIES})
    target_include_directories(xpilot-ng-sdl PRIVATE ${OPENAL_INCLUDE_DIRS} ${ALUT_INCLUDE_DIRS})
endif()

install(TARGETS xpilot-ng-sdl
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
