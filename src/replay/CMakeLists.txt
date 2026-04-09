# src/mapedit/CMakeLists.txt
if(NOT X11_FOUND)
    message(FATAL_ERROR "XPILOT_BUILD_MAPEDIT is ON, but X11 was not found.")
endif()

file(GLOB MAPEDIT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_executable(xpilot-ng-xp-mapedit ${MAPEDIT_SOURCES})

target_include_directories(xpilot-ng-xp-mapedit
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_BINARY_DIR}
)

target_compile_definitions(xpilot-ng-xp-mapedit
    PRIVATE
        HAVE_CONFIG_H=1
        CONF_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/"
)

target_link_libraries(xpilot-ng-xp-mapedit
    PRIVATE
        xpcommon
        ${X11_LIBRARIES}
)

install(TARGETS xpilot-ng-xp-mapedit
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
