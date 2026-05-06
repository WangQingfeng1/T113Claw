set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER ${TOOLCHAIN_PATH}/arm-openwrt-linux-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PATH}/arm-openwrt-linux-g++)

# ARM Cortex-A7 optimizations
add_compile_options(
    -march=armv7-a -mtune=cortex-a7
    -mfpu=neon -mfloat-abi=hard
    -O2 -g
    -ffunction-sections -fdata-sections
    -Wall -Wextra -Wno-unused-parameter
)

add_link_options(
    -L${CMAKE_CURRENT_LIST_DIR}/lib
    -lpthread -lrt -ldl -lm -lz -lnghttp2
    -Wl,-gc-sections
    -Wl,-rpath,/usr/lib
)

add_compile_options(
    -I${CMAKE_CURRENT_LIST_DIR}/include
    -I${CMAKE_CURRENT_LIST_DIR}/include/cJSON
)

# ALSA headers are at include/alsa/asoundlib.h, so parent dir is already included
