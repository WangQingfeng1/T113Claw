# cJSON library — https://github.com/DaveGamble/cJSON (MIT license)
cmake_minimum_required(VERSION 3.15)
project(cjson)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cJSON.c)
    add_library(cjson STATIC cJSON.c)
    target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
else()
    # Fallback: try system cJSON
    find_library(CJSON_LIB cjson)
    if(CJSON_LIB)
        add_library(cjson INTERFACE)
        target_link_libraries(cjson INTERFACE ${CJSON_LIB})
        find_path(CJSON_INC cJSON.h PATH_SUFFIXES cjson)
        if(CJSON_INC)
            target_include_directories(cjson INTERFACE ${CJSON_INC})
        endif()
    else()
        message(FATAL_ERROR
            "cJSON not found. Either:\n"
            "  1. Place cJSON.c and cJSON.h in component/cjson/\n"
            "  2. Install system package: sudo apt-get install libcjson-dev"
        )
    endif()
endif()
