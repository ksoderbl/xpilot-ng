# src/server/CMakeLists.txt

file(GLOB SERVER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_executable(xpilot-ng-server ${SERVER_SOURCES})

target_include_directories(xpilot-ng-server
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_BINARY_DIR}
)

target_compile_definitions(xpilot-ng-server
    PRIVATE
        HAVE_CONFIG_H=1
        CONF_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/"
)

target_link_libraries(xpilot-ng-server
    PRIVATE
        xpcommon
)

target_link_libraries(xpilot-ng-server
    PRIVATE
        xpcommon
        EXPAT::EXPAT
        ZLIB::ZLIB
        m
)

install(TARGETS xpilot-ng-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
