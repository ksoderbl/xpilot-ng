# src/CMakeLists.txt
add_subdirectory(common)
add_subdirectory(server)

if(XPILOT_BUILD_X11_CLIENT OR XPILOT_BUILD_SDL_CLIENT)
    add_subdirectory(client)
endif()

if(XPILOT_BUILD_REPLAY)
    add_subdirectory(replay)
endif()

if(XPILOT_BUILD_MAPEDIT)
    add_subdirectory(mapedit)
endif()
