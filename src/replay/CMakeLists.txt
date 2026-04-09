if(NOT X11_FOUND)
    message(FATAL_ERROR "XPILOT_BUILD_REPLAY is ON, but X11 was not found.")
endif()

file(GLOB REPLAY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_executable(xpilot-ng-replay ${REPLAY_SOURCES})

target_include_directories(xpilot-ng-replay
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/client
        ${CMAKE_SOURCE_DIR}/src/client/items
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}
        ${X11_INCLUDE_DIR}
)

target_compile_definitions(xpilot-ng-replay
    PRIVATE
        HAVE_CONFIG_H=1
        CONF_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/"
)

target_link_libraries(xpilot-ng-replay
    PRIVATE
        xpcommon
        ZLIB::ZLIB
        ${X11_LIBRARIES}
)

install(TARGETS xpilot-ng-replay
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
