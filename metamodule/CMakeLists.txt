cmake_minimum_required(VERSION 3.22)

set(METAMODULE_SDK_DIR ${CMAKE_CURRENT_LIST_DIR}/../metamodule-plugin-sdk)

message("METAMODULE_SDK_DIR set to ${METAMODULE_SDK_DIR}")

include(${METAMODULE_SDK_DIR}/plugin.cmake)

project(WildergardenMetaModulePlugin
    VERSION 0.1
    DESCRIPTION "Wildergarden Plugin for Metamodule"
    LANGUAGES   C CXX ASM
)

add_library(wildergarden STATIC)

set (SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../src)

target_sources(wildergarden
    PRIVATE
        ${SOURCE_DIR}/dimit.cpp
        ${SOURCE_DIR}/Pascal.cpp
        ${SOURCE_DIR}/plugin.cpp
)

target_include_directories(wildergarden
    PRIVATE
    ${SOURCE_DIR}
)

set_property(TARGET wildergarden CXX_STANDARD 20)

create_plugin(
        SOURCE_LIB wildergarden
        PLUGIN_NAME wildergarden
        PLUGIN_JSON ${CMAKE_CURRENT_LIST_DIR}/../plugin.json
        SOURCE_ASSETS ${CMAKE_CURRENT_LIST_DIR}/assets
        DESTINATION ${INSTALL_DIR}
)