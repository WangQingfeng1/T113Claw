#!/bin/bash
# T113Claw Build Script
# Usage: ./build.sh -linux    (x86 simulator UI)
#        ./build.sh -t113     (cross-compile for T113)
#        ./build.sh -clean    (remove build directory)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLCHAIN_PATH="yours/toolchain-sunxi-glibc-gcc-830/toolchain/bin/"
BUILD_DIR="${SCRIPT_DIR}/build"
BUILD_STAMP="${BUILD_DIR}/.platform-stamp"

function prepare_build_dir() {
    local target_platform="$1"

    mkdir -p "${BUILD_DIR}"

    if [ -f "${BUILD_STAMP}" ]; then
        local last_platform
        last_platform="$(cat "${BUILD_STAMP}")"
        if [ "${last_platform}" != "${target_platform}" ]; then
            echo "Switching build platform: ${last_platform} -> ${target_platform}"
            echo "Cleaning cached CMake state to avoid cross-platform configure errors."
            rm -rf "${BUILD_DIR:?}"/* "${BUILD_DIR}"/.[!.]* "${BUILD_DIR}"/..?* 2>/dev/null || true
            mkdir -p "${BUILD_DIR}"
        fi
    fi

    printf '%s' "${target_platform}" > "${BUILD_STAMP}"
}

function usage() {
    echo
    echo "T113Claw Build System"
    echo "Usage:"
    echo "  ./build.sh -linux   Build for x86 Linux (SDL simulator UI)"
    echo "  ./build.sh -t113    Cross-compile for Allwinner T113-S3"
    echo "  ./build.sh -clean   Clean build directory"
    echo
}

platform=""

while test $# -gt 0; do
    case "$1" in
        -linux)  platform="linux" ;;
        -t113)   platform="t113" ;;
        -clean)
            rm -rf "${BUILD_DIR}"
            echo "Build directory cleaned."
            exit 0
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
    shift
done

if [ -z "${platform}" ]; then
    usage
    exit 0
fi

prepare_build_dir "${platform}"
cd "${BUILD_DIR}"

if [ "${platform}" = "linux" ]; then
    echo "=== Building T113Claw for x86 Linux ==="
    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE=platform/x86linux/linux.cmake \
        -DSIMULATOR_LINUX=ON
    make -j$(nproc)
elif [ "${platform}" = "t113" ]; then
    export STAGING_DIR="${TOOLCHAIN_PATH}:${STAGING_DIR}"
    echo "=== Building T113Claw for T113 ==="
    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE=platform/t113/t113.cmake \
        -DSIMULATOR_LINUX=OFF \
        -DTOOLCHAIN_PATH="${TOOLCHAIN_PATH}"
    make -j$(nproc)
fi
