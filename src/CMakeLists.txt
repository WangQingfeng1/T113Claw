cmake_minimum_required(VERSION 3.15)
project(t113claw)

# ── Collect all source files ──────────────────────────────────
set(T113CLAW_SOURCES
    main.c
    bus/message_bus.c
    config/config.c
    memory/memory_store.c
    memory/session_mgr.c
    llm/llm_client.c
    llm/provider_openai.c
    llm/provider_claude.c
    tools/tool_registry.c
    tools/tool_time.c
    tools/tool_file.c
    tools/tool_system.c
    tools/tool_cron.c
    tools/tool_exec.c
    tools/tool_remote_exec.c
    tools/tool_web_search.c
    agent/context_builder.c
    agent/agent_loop.c
    channels/channel_manager.c
    channels/cli/cli_channel.c
    channels/feishu/feishu_bot.c
    channels/feishu/feishu_proto.c
    channels/voice/voice_channel.c
    services/wifi_service.c
    services/cron_service.c
    services/heartbeat.c
    services/remote_client.c
    services/audio_service.c
    skills/skill_loader.c
    ui/ui_manager.c
    ui/page_chat.c
    ui/page_settings.c
)

if(NOT SIMULATOR_LINUX)
    list(APPEND T113CLAW_SOURCES
        voice/voice_manager.c
        voice/xfyun_auth.c
        voice/xfyun_stt.c
        voice/xfyun_tts.c
        voice/gpio_button.c
        voice/vad.c
        voice/wake_detector.c
    )
endif()

add_executable(t113claw ${T113CLAW_SOURCES})

target_include_directories(t113claw PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/bus
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/llm
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/agent
    ${CMAKE_CURRENT_SOURCE_DIR}/channels
    ${CMAKE_CURRENT_SOURCE_DIR}/channels/cli
    ${CMAKE_CURRENT_SOURCE_DIR}/channels/feishu
    ${CMAKE_CURRENT_SOURCE_DIR}/channels/voice
    ${CMAKE_CURRENT_SOURCE_DIR}/services
    ${CMAKE_CURRENT_SOURCE_DIR}/skills
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/voice
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${CMAKE_SOURCE_DIR}/component/cjson
    ${CMAKE_SOURCE_DIR}/component/net
    ${CMAKE_SOURCE_DIR}/audio
)

if(NOT SIMULATOR_LINUX)
    target_include_directories(t113claw PRIVATE
        ${CMAKE_SOURCE_DIR}/platform/t113/src/porting
        ${CMAKE_SOURCE_DIR}/platform/t113/src/porting/g2d
        ${CMAKE_SOURCE_DIR}/platform/t113/src/utils
        ${CMAKE_SOURCE_DIR}/platform/t113/include
        ${CMAKE_SOURCE_DIR}/platform/t113/include/freetype
    )
else()
    target_include_directories(t113claw PRIVATE
        ${CMAKE_SOURCE_DIR}/platform/x86linux/include
    )
endif()

target_link_libraries(t113claw PRIVATE
    cjson
    t113claw_net
    pthread
    curl
    ssl
    crypto
    m
    lvgl
    lvgl_porting
)

if(NOT SIMULATOR_LINUX)
    target_link_libraries(t113claw PRIVATE
        speexdsp
        sherpa-onnx-c-api
    )
endif()

if(SIMULATOR_LINUX)
    target_compile_definitions(t113claw PRIVATE SIMULATOR_LINUX)
endif()
