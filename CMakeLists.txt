cmake_minimum_required(VERSION 3.15)
project(T113Claw VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# _GNU_SOURCE for pthread_setname_np, etc.
add_compile_definitions(_GNU_SOURCE)

set(T113CLAW_LVGL_DIR "${CMAKE_SOURCE_DIR}/lvgl")
if(NOT EXISTS "${T113CLAW_LVGL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "LVGL reference not found: ${T113CLAW_LVGL_DIR}")
endif()

# ── Platform detection ─────────────────────────────────────────
if(SIMULATOR_LINUX)
    message(STATUS "Platform: x86 Linux simulator")
    add_compile_definitions(SIMULATOR_LINUX)
    add_compile_definitions(T113CLAW_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FREETYPE2 REQUIRED freetype2)
else()
    message(STATUS "Platform: Allwinner T113-S3")
    add_compile_definitions(T113CLAW_DATA_DIR="/usr/share/t113claw/data")

    # sherpa-onnx KWS library (pre-built for ARM)
    set(SHERPA_ONNX_DIR "${CMAKE_SOURCE_DIR}/platform/t113/sherpa-onnx")
    if(EXISTS "${SHERPA_ONNX_DIR}/lib/libsherpa-onnx-c-api.so")
        message(STATUS "sherpa-onnx found at: ${SHERPA_ONNX_DIR}")
        link_directories(${SHERPA_ONNX_DIR}/lib)
        include_directories(${SHERPA_ONNX_DIR}/include)
    else()
        message(WARNING "sherpa-onnx not found, wake word detection will be disabled")
    endif()
endif()

# ── Global include paths ──────────────────────────────────────
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/component/cjson
)

# ── Components ────────────────────────────────────────────────
add_subdirectory(component/cjson)
add_subdirectory(component/net)

# ── LVGL / Porting ────────────────────────────────────────────
if(SIMULATOR_LINUX)
    set(T113CLAW_X86_PORT_DIR "${CMAKE_SOURCE_DIR}/../reference_linux/app_sdk/platform/x86linux/src/porting")

    add_library(lvgl_porting STATIC
        ${T113CLAW_X86_PORT_DIR}/monitor.c
        ${T113CLAW_X86_PORT_DIR}/simulator_init.c
        ${T113CLAW_X86_PORT_DIR}/device/mouse.c
        ${T113CLAW_X86_PORT_DIR}/device/keyboard.c
        ${T113CLAW_X86_PORT_DIR}/device/mousewheel.c
        ${T113CLAW_X86_PORT_DIR}/device/mouse_cursor_icon.c
    )

    target_include_directories(lvgl_porting PUBLIC
        ${T113CLAW_LVGL_DIR}/..
        ${T113CLAW_LVGL_DIR}
        ${T113CLAW_LVGL_DIR}/src
        ${T113CLAW_X86_PORT_DIR}
        ${T113CLAW_X86_PORT_DIR}/device
        ${CMAKE_SOURCE_DIR}/platform/x86linux/include
        ${FREETYPE2_INCLUDE_DIRS}
    )

    target_compile_definitions(lvgl_porting PRIVATE
        LV_LVGL_H_INCLUDE_SIMPLE
        LV_CONF_INCLUDE_SIMPLE
    )

    target_link_libraries(lvgl_porting PUBLIC SDL2 m pthread dl)
else()
    add_library(lvgl_porting STATIC
        ${CMAKE_SOURCE_DIR}/platform/t113/src/porting/lv_port_disp.c
        ${CMAKE_SOURCE_DIR}/platform/t113/src/porting/lv_port_indev.c
        ${CMAKE_SOURCE_DIR}/platform/t113/src/porting/lv_port_tick.c
        ${CMAKE_SOURCE_DIR}/platform/t113/src/porting/g2d/sunxifb.c
        ${CMAKE_SOURCE_DIR}/platform/t113/src/porting/g2d/sunxig2d.c
        ${CMAKE_SOURCE_DIR}/platform/t113/src/porting/g2d/sunximem.c
        ${CMAKE_SOURCE_DIR}/platform/t113/src/utils/base_utils.c
    )

    target_include_directories(lvgl_porting PUBLIC
        ${T113CLAW_LVGL_DIR}
        ${T113CLAW_LVGL_DIR}/src
        ${CMAKE_SOURCE_DIR}/platform/t113/src/porting
        ${CMAKE_SOURCE_DIR}/platform/t113/src/porting/g2d
        ${CMAKE_SOURCE_DIR}/platform/t113/src/utils
        ${CMAKE_SOURCE_DIR}/platform/t113/include
        ${CMAKE_SOURCE_DIR}/platform/t113/include/freetype
    )

    target_compile_definitions(lvgl_porting PRIVATE
        LV_LVGL_H_INCLUDE_SIMPLE
        LV_CONF_INCLUDE_SIMPLE
        USE_SUNXIFB
        USE_SUNXIFB_DOUBLE_BUFFER
        USE_SUNXIFB_CACHE
        SUNXIFB_G2D
        USE_SUNXIFB_G2D
        USE_SUNXIFB_G2D_ROTATE
        CONF_G2D_VERSION_NEW
        LV_USE_SUNXIFB_G2D_FILL
        LV_USE_SUNXIFB_G2D_BLEND
        LV_USE_SUNXIFB_G2D_BLIT
        LV_USE_SUNXIFB_G2D_SCALE
    )

    target_link_libraries(lvgl_porting PUBLIC uapi)

endif()

add_subdirectory(${T113CLAW_LVGL_DIR} ${CMAKE_BINARY_DIR}/vendor/lvgl EXCLUDE_FROM_ALL)

if(SIMULATOR_LINUX)
    target_include_directories(lvgl PUBLIC
        ${T113CLAW_LVGL_DIR}/..
        ${CMAKE_SOURCE_DIR}/../reference_linux/app_sdk/platform/x86linux/src/porting
        ${CMAKE_SOURCE_DIR}/platform/x86linux/include
        ${FREETYPE2_INCLUDE_DIRS}
    )
    target_link_directories(lvgl PUBLIC ${FREETYPE2_LIBRARY_DIRS})
    target_link_libraries(lvgl PUBLIC ${FREETYPE2_LINK_LIBRARIES} m dl pthread)
else()
    target_include_directories(lvgl PUBLIC
        ${CMAKE_SOURCE_DIR}/platform/t113/src/porting
        ${CMAKE_SOURCE_DIR}/platform/t113/include
        ${CMAKE_SOURCE_DIR}/platform/t113/include/freetype
    )
    target_link_libraries(lvgl PUBLIC freetype bz2 z m dl pthread)
endif()

# ── Audio process (separate executable) ───────────────────────
if(NOT SIMULATOR_LINUX)
    add_subdirectory(audio)
endif()

# ── Main application ─────────────────────────────────────────
add_subdirectory(src)
