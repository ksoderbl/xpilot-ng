# src/common/CMakeLists.txt
file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(xpcommon STATIC ${COMMON_SOURCES})

target_include_directories(xpcommon
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}
)

target_compile_definitions(xpcommon
    PUBLIC
        HAVE_CONFIG_H=1
        CONF_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(xpcommon PUBLIC m)
endif()
