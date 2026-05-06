cmake_minimum_required(VERSION 3.15)
project(t113claw_audio LANGUAGES C)

# ── Source files ──────────────────────────────────────────────
set(AUDIO_SOURCES
    audio_main.c
    audio_codec.c
    audio_capture.c
    audio_playback.c
)

add_executable(t113claw_audio ${AUDIO_SOURCES})

target_include_directories(t113claw_audio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── Link libraries ────────────────────────────────────────────
# asound = ALSA library (on board: /usr/lib/libasound.so.2)
# pthread = POSIX threads
target_link_libraries(t113claw_audio PRIVATE
    asound
    pthread
)

if(SIMULATOR_LINUX)
    target_compile_definitions(t113claw_audio PRIVATE SIMULATOR_LINUX)
endif()
