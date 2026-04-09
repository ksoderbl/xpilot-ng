# src/client/x11/CMakeLists.txt
if(NOT X11_FOUND)
    message(FATAL_ERROR "XPILOT_BUILD_X11_CLIENT is ON, but X11 was not found.")
endif()

file(GLOB X11_CLIENT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_executable(xpilot-ng-x11 ${X11_CLIENT_SOURCES})

target_include_directories(xpilot-ng-x11
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/client
        ${CMAKE_BINARY_DIR}
)

target_compile_definitions(xpilot-ng-x11
    PRIVATE
        HAVE_CONFIG_H=1
        CONF_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/"
)

target_link_libraries(xpilot-ng-x11
    PRIVATE
        xpclient
        xpcommon
        ${X11_LIBRARIES}
)

if(XPILOT_HAVE_SOUND)
    target_link_libraries(xpilot-ng-x11 PRIVATE ${OPENAL_LIBRARIES} ${ALUT_LIBRARIES})
    target_include_directories(xpilot-ng-x11 PRIVATE ${OPENAL_INCLUDE_DIRS} ${ALUT_INCLUDE_DIRS})
endif()

install(TARGETS xpilot-ng-x11
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
