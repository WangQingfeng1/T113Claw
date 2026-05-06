cmake_minimum_required(VERSION 3.15)
project(t113claw_net)

set(NET_SOURCES
    http_client.c
    ws_client.c
)

add_library(t113claw_net STATIC ${NET_SOURCES})

target_include_directories(t113claw_net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/component/cjson
    ${CMAKE_SOURCE_DIR}/src/utils
    ${CMAKE_SOURCE_DIR}/src/config
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(t113claw_net PUBLIC curl ssl crypto)
