cmake_minimum_required(VERSION 3.22)

project(htdemucs_gpu_fx_ipc VERSION 0.1.0 LANGUAGES CXX)

if(MSVC)
    # Keep the standalone executable independent of a separately installed VC runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# macOS: build a universal binary so one .app runs on both Intel and Apple
# Silicon. Set before any target is declared so every target inherits it.
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING
        "macOS architectures (universal by default)" FORCE)
endif()
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING
        "Minimum macOS version" FORCE)
endif()

option(HTFX_BUILD_PLUGIN "Build the JUCE VST3 prototype" ON)
option(HTFX_STANDALONE_ONLY "Build only the portable Standalone application" OFF)
option(HTFX_PORTABLE_STANDALONE_SETTINGS
    "Store Standalone settings beside the executable in PortableData" ON)
if(WIN32)
    set(HTFX_DEFAULT_PYTHON "python.exe")
    set(HTFX_DEFAULT_FFMPEG "ffmpeg.exe")
else()
    set(HTFX_DEFAULT_PYTHON "python3")
    set(HTFX_DEFAULT_FFMPEG "ffmpeg")
endif()
set(HTFX_PYTHON_EXECUTABLE "${HTFX_DEFAULT_PYTHON}" CACHE FILEPATH
    "Python executable containing PyTorch")
set(HTFX_GPU_WORKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/worker/gpu_ipc_worker.py" CACHE FILEPATH
    "HTDemucs GPU IPC worker script")
set(HTFX_CHECKPOINT_FILE "" CACHE FILEPATH
    "Full HTDemucs checkpoint")
set(HTFX_FFMPEG_EXECUTABLE "${HTFX_DEFAULT_FFMPEG}" CACHE FILEPATH
    "Optional external FFmpeg executable used for media import/export")

if(WIN32)
    set(HTFX_GPU_WORKER_CLIENT_SOURCE cpp/GpuWorkerClient.cpp)
else()
    set(HTFX_GPU_WORKER_CLIENT_SOURCE cpp/GpuWorkerClientPosix.cpp)
endif()

add_library(htfx_gpu_worker_client STATIC
    ${HTFX_GPU_WORKER_CLIENT_SOURCE}
    cpp/GpuWorkerClient.h
    cpp/ipc_protocol.h
)
target_include_directories(htfx_gpu_worker_client PUBLIC cpp)
target_compile_features(htfx_gpu_worker_client PUBLIC cxx_std_20)
if(WIN32)
    target_compile_definitions(htfx_gpu_worker_client PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    )
endif()
if(MSVC)
    target_compile_options(htfx_gpu_worker_client PRIVATE /W4 /permissive- /EHsc)
endif()

add_executable(htdemucs_gpu_worker_smoke tests/gpu_worker_smoke.cpp)
target_compile_features(htdemucs_gpu_worker_smoke PRIVATE cxx_std_20)
target_compile_definitions(htdemucs_gpu_worker_smoke PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_libraries(htdemucs_gpu_worker_smoke PRIVATE htfx_gpu_worker_client)
if(MSVC)
    target_compile_options(htdemucs_gpu_worker_smoke PRIVATE /W4 /permissive- /EHsc)
endif()

if(WIN32)
    set(HTFX_REGISTRY_SMOKE_SOURCE tests/worker_registry_smoke.cpp)
else()
    set(HTFX_REGISTRY_SMOKE_SOURCE tests/worker_registry_smoke_posix.cpp)
endif()
add_executable(htdemucs_worker_registry_smoke ${HTFX_REGISTRY_SMOKE_SOURCE})
target_compile_features(htdemucs_worker_registry_smoke PRIVATE cxx_std_20)
target_compile_definitions(htdemucs_worker_registry_smoke PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_libraries(htdemucs_worker_registry_smoke PRIVATE htfx_gpu_worker_client)
if(MSVC)
    target_compile_options(
        htdemucs_worker_registry_smoke PRIVATE /W4 /permissive- /EHsc
    )
endif()

if(WIN32)
    add_executable(htfx_hardware_probe tools/windows/htfx_hardware_probe.cpp)
    target_compile_features(htfx_hardware_probe PRIVATE cxx_std_20)
    target_compile_definitions(htfx_hardware_probe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(htfx_hardware_probe PRIVATE dxgi)
    if(MSVC)
        target_compile_options(htfx_hardware_probe PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()

    add_executable(htdemucs_ipc_client
        cpp/ipc_client.cpp
        cpp/ipc_protocol.h
    )
    target_compile_features(htdemucs_ipc_client PRIVATE cxx_std_20)
    target_compile_definitions(htdemucs_ipc_client PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(htdemucs_ipc_client PRIVATE /W4 /permissive- /EHsc)
    endif()
endif()

if(HTFX_BUILD_PLUGIN)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
        message(FATAL_ERROR
            "JUCE submodule is missing. Run: git submodule update --init --recursive")
    endif()
    add_subdirectory(third_party/JUCE)

    if(HTFX_STANDALONE_ONLY)
        set(HTFX_PLUGIN_FORMATS Standalone)
    else()
        set(HTFX_PLUGIN_FORMATS VST3 Standalone)
    endif()
    juce_add_plugin(HTDemucsGpuFX
        COMPANY_NAME "HTFX Prototype"
        COMPANY_WEBSITE "https://localhost.invalid"
        BUNDLE_ID "local.htfx.htdemucsgpufx"
        PLUGIN_MANUFACTURER_CODE Htfx
        PLUGIN_CODE Htdm
        FORMATS ${HTFX_PLUGIN_FORMATS}
        PRODUCT_NAME "HTDemucs GPU FX"
        DESCRIPTION "Full HTDemucs GPU streaming FX prototype"
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/htdemucs-gpu-fx-icon.png"
        ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/htdemucs-gpu-fx-icon.png"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx
    )

    target_sources(HTDemucsGpuFX PRIVATE
        plugin/Localization.cpp
        plugin/Localization.h
        plugin/PluginProcessor.cpp
        plugin/PluginProcessor.h
        plugin/SpscRing.h
    )
    if(WIN32)
        target_sources(HTDemucsGpuFX PRIVATE
            plugin/MmeAudioIODevice.cpp
            plugin/MmeAudioIODevice.h
        )
    endif()
    target_include_directories(HTDemucsGpuFX PRIVATE plugin cpp)
    target_compile_features(HTDemucsGpuFX PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(HTDemucsGpuFX PRIVATE /utf-8)
    endif()
    target_compile_definitions(HTDemucsGpuFX PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )
    target_compile_definitions(HTDemucsGpuFX PRIVATE
        HTFX_DEFAULT_PYTHON_PATH="${HTFX_DEFAULT_PYTHON}"
        HTFX_DEFAULT_GPU_WORKER_PATH=""
        HTFX_DEFAULT_CHECKPOINT_PATH=""
        HTFX_DEFAULT_MODELS_PATH=""
        HTFX_DEFAULT_FFMPEG_PATH="${HTFX_DEFAULT_FFMPEG}"
    )
    if(TARGET HTDemucsGpuFX_Standalone AND HTFX_PORTABLE_STANDALONE_SETTINGS)
        target_compile_definitions(HTDemucsGpuFX_Standalone PRIVATE
            HTFX_PORTABLE_STANDALONE_SETTINGS=1
        )
    endif()
    if(TARGET HTDemucsGpuFX_Standalone)
        if(WIN32)
            target_compile_definitions(HTDemucsGpuFX_Standalone PRIVATE
                HTFX_ENABLE_MME_AUDIO=1)
        endif()
    endif()
    target_link_libraries(HTDemucsGpuFX
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            htfx_gpu_worker_client
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
    if(WIN32)
        target_link_libraries(HTDemucsGpuFX PRIVATE winmm)
    endif()

    add_executable(htdemucs_plugin_smoke tests/plugin_smoke.cpp)
    target_include_directories(htdemucs_plugin_smoke PRIVATE plugin)
    target_compile_features(htdemucs_plugin_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(htdemucs_plugin_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_plugin_smoke PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )

    add_executable(htdemucs_plugin_gpu_smoke tests/plugin_gpu_smoke.cpp)
    target_include_directories(htdemucs_plugin_gpu_smoke PRIVATE plugin)
    target_compile_features(htdemucs_plugin_gpu_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(htdemucs_plugin_gpu_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_plugin_gpu_smoke PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )

    add_executable(
        htdemucs_standalone_portable_gpu_smoke
        tests/standalone_portable_gpu_smoke.cpp
    )
    target_include_directories(
        htdemucs_standalone_portable_gpu_smoke
        PRIVATE plugin
    )
    target_compile_features(
        htdemucs_standalone_portable_gpu_smoke
        PRIVATE cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            htdemucs_standalone_portable_gpu_smoke
            PRIVATE /utf-8
        )
    endif()
    target_link_libraries(htdemucs_standalone_portable_gpu_smoke PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )

    add_executable(htdemucs_record_mode_smoke tests/record_mode_smoke.cpp)
    target_include_directories(htdemucs_record_mode_smoke PRIVATE plugin)
    target_compile_features(htdemucs_record_mode_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(htdemucs_record_mode_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_record_mode_smoke PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )

    add_executable(htdemucs_media_io_smoke tests/media_io_smoke.cpp)
    target_include_directories(htdemucs_media_io_smoke PRIVATE plugin)
    target_compile_features(htdemucs_media_io_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(htdemucs_media_io_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_media_io_smoke PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )

    add_executable(
        htdemucs_audio_backend_smoke
        tests/audio_backend_smoke.cpp
        plugin/MmeAudioIODevice.cpp
        plugin/MmeAudioIODevice.h
    )
    target_include_directories(htdemucs_audio_backend_smoke PRIVATE plugin)
    target_compile_features(htdemucs_audio_backend_smoke PRIVATE cxx_std_20)
    target_compile_definitions(htdemucs_audio_backend_smoke PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    if(MSVC)
        target_compile_options(htdemucs_audio_backend_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_audio_backend_smoke PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    if(WIN32)
        target_link_libraries(htdemucs_audio_backend_smoke PRIVATE winmm)
    endif()

    add_executable(htdemucs_ui_configuration_smoke tests/ui_configuration_smoke.cpp)
    target_include_directories(htdemucs_ui_configuration_smoke PRIVATE plugin)
    target_compile_features(htdemucs_ui_configuration_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(htdemucs_ui_configuration_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_ui_configuration_smoke PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )

    add_executable(htdemucs_roformer_smoke tests/roformer_smoke.cpp)
    target_include_directories(htdemucs_roformer_smoke PRIVATE plugin)
    target_compile_features(htdemucs_roformer_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(htdemucs_roformer_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_roformer_smoke PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )

    add_executable(htdemucs_goal_check tests/goal_check.cpp)
    target_include_directories(htdemucs_goal_check PRIVATE plugin)
    target_compile_features(htdemucs_goal_check PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(htdemucs_goal_check PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_goal_check PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )

    add_executable(htdemucs_transport_epoch_smoke tests/transport_epoch_smoke.cpp)
    target_include_directories(htdemucs_transport_epoch_smoke PRIVATE plugin)
    target_compile_features(htdemucs_transport_epoch_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(htdemucs_transport_epoch_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_transport_epoch_smoke PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )

    if(WIN32)
    add_executable(htdemucs_worker_recovery_smoke tests/worker_recovery_smoke.cpp)
    target_include_directories(htdemucs_worker_recovery_smoke PRIVATE plugin)
    target_compile_features(htdemucs_worker_recovery_smoke PRIVATE cxx_std_20)
    target_compile_definitions(htdemucs_worker_recovery_smoke PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX
    )
    if(MSVC)
        target_compile_options(htdemucs_worker_recovery_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_worker_recovery_smoke PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )
    endif()

    add_executable(htdemucs_plugin_soak tests/plugin_soak.cpp)
    target_include_directories(htdemucs_plugin_soak PRIVATE plugin)
    target_compile_features(htdemucs_plugin_soak PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(htdemucs_plugin_soak PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_plugin_soak PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )

    add_executable(htdemucs_fallback_smoke tests/fallback_smoke.cpp)
    target_include_directories(htdemucs_fallback_smoke PRIVATE plugin)
    target_compile_features(htdemucs_fallback_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(htdemucs_fallback_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(htdemucs_fallback_smoke PRIVATE
        HTDemucsGpuFX
        juce::juce_audio_utils
    )

    if(NOT HTFX_STANDALONE_ONLY)
    add_executable(vst3_scan_smoke tests/vst3_scan_smoke.cpp)
    target_compile_features(vst3_scan_smoke PRIVATE cxx_std_20)
    target_compile_definitions(vst3_scan_smoke PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_VST=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    if(MSVC)
        target_compile_options(vst3_scan_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(vst3_scan_smoke PRIVATE juce::juce_audio_utils)

    add_executable(vst3_gpu_smoke tests/vst3_gpu_smoke.cpp)
    target_compile_features(vst3_gpu_smoke PRIVATE cxx_std_20)
    target_compile_definitions(vst3_gpu_smoke PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_VST=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    if(MSVC)
        target_compile_options(vst3_gpu_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(vst3_gpu_smoke PRIVATE juce::juce_audio_utils)
    endif()

    enable_testing()
    add_test(NAME htdemucs_plugin_smoke COMMAND htdemucs_plugin_smoke)
endif()
