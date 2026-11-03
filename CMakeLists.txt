cmake_minimum_required(VERSION 3.22)
project(Faider VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12   # 8.0.5+ fixes Direct2D window-drag lag; 8.0.13 has reported Direct2D regressions
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(juce)

# RNNoise 0.2 (Xiph.Org / Jean-Marc Valin, BSD licence: third_party/rnnoise/COPYING), with its
# built-in model. Plain C on its SSE2 path; no run-time CPU dispatch.
set(_rnnoise "${CMAKE_SOURCE_DIR}/third_party/rnnoise")
add_library(rnnoise STATIC
    ${_rnnoise}/src/denoise.c
    ${_rnnoise}/src/rnn.c
    ${_rnnoise}/src/pitch.c
    ${_rnnoise}/src/kiss_fft.c
    ${_rnnoise}/src/celt_lpc.c
    ${_rnnoise}/src/nnet.c
    ${_rnnoise}/src/nnet_default.c
    ${_rnnoise}/src/parse_lpcnet_weights.c
    ${_rnnoise}/src/rnnoise_data.c
    ${_rnnoise}/src/rnnoise_tables.c)
target_include_directories(rnnoise PUBLIC ${_rnnoise}/include PRIVATE ${_rnnoise}/src)
target_compile_definitions(rnnoise PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS RNNOISE_BUILD
    OPUS_X86_MAY_HAVE_SSE OPUS_X86_MAY_HAVE_SSE2)   # every x64 CPU has SSE2
if(MSVC)
    target_compile_options(rnnoise PRIVATE /W0 /O2 /fp:fast)
endif()

# Steinberg ASIO SDK (free download, GPLv3 or proprietary licence). Optional: without it the engine runs on a Windows device or its built-in clock.
set(_bundled_asio "${CMAKE_SOURCE_DIR}/third_party/asiosdk_2.3.3_2019-06-14")
if(NOT EXISTS "${_bundled_asio}/common/iasiodrv.h")
    set(_bundled_asio "")
endif()
set(ASIO_SDK_DIR "${_bundled_asio}" CACHE PATH "Path to the extracted Steinberg ASIO SDK (folder containing 'common')")

# Built into the program: the headphone correction profiles (assets/autoeq, MIT: see assets/LICENSES.md).
juce_add_binary_data(FaiderAssets HEADER_NAME FaiderAssets.h NAMESPACE FaiderAssets SOURCES
    assets/autoeq/profiles.txt)

# Test plugin for insert chains / delay compensation. Installed to the per-user VST3 folder
# (%LOCALAPPDATA%\Programs\Common\VST3), which Faider scans and which needs no admin rights.
file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Programs/Common/VST3" USER_VST3_DIR)

juce_add_plugin(FaiderTestDelay
    PRODUCT_NAME "Faider Test Delay"
    COMPANY_NAME "Faider"
    PLUGIN_MANUFACTURER_CODE Ahub
    PLUGIN_CODE Tdl1
    FORMATS VST3
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "${USER_VST3_DIR}")

target_sources(FaiderTestDelay PRIVATE tests/TestDelayPlugin.cpp)

target_compile_definitions(FaiderTestDelay PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(FaiderTestDelay
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Meter standards check: build and run MeterTest (exit code = number of failures).
juce_add_console_app(MeterTest PRODUCT_NAME "MeterTest")
target_sources(MeterTest PRIVATE tests/MeterTest.cpp)
target_compile_definitions(MeterTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(MeterTest
    PRIVATE
        juce::juce_audio_basics
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags)

# Button box check (see tests/PadsTest.cpp).
juce_add_console_app(PadsTest PRODUCT_NAME "PadsTest")
target_sources(PadsTest PRIVATE tests/PadsTest.cpp src/engine/Pads.cpp)
target_compile_definitions(PadsTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(PadsTest
    PRIVATE
        juce::juce_audio_formats
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags)

# Tape deck check (see tests/TapeTest.cpp).
juce_add_console_app(TapeTest PRODUCT_NAME "TapeTest")
target_sources(TapeTest PRIVATE tests/TapeTest.cpp src/engine/Tape.cpp)
target_compile_definitions(TapeTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(TapeTest
    PRIVATE
        juce::juce_audio_formats
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags)

# Noise reducer check (see tests/NoiseReducerTest.cpp).
juce_add_console_app(NoiseReducerTest PRODUCT_NAME "NoiseReducerTest")
target_sources(NoiseReducerTest PRIVATE tests/NoiseReducerTest.cpp src/engine/NeuralNoise.cpp)
target_compile_definitions(NoiseReducerTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(NoiseReducerTest
    PRIVATE
        juce::juce_dsp
        rnnoise
    PUBLIC
        juce::juce_recommended_config_flags)

# Channel processing check: auto-level, gate (see tests/VoiceChainTest.cpp).
juce_add_console_app(VoiceChainTest PRODUCT_NAME "VoiceChainTest")
target_sources(VoiceChainTest PRIVATE tests/VoiceChainTest.cpp src/engine/VoiceChain.cpp src/engine/NeuralNoise.cpp)
target_compile_definitions(VoiceChainTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(VoiceChainTest
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
        rnnoise
    PUBLIC
        juce::juce_recommended_config_flags)

# Windows default device check, read only (see tests/DefaultDeviceTest.cpp).
juce_add_console_app(DefaultDeviceTest PRODUCT_NAME "DefaultDeviceTest")
target_sources(DefaultDeviceTest PRIVATE tests/DefaultDeviceTest.cpp src/devices/WindowsDefault.cpp)
target_compile_definitions(DefaultDeviceTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(DefaultDeviceTest
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags)

# QR code check (see tests/QrTest.cpp).
juce_add_console_app(QrTest PRODUCT_NAME "QrTest")
target_sources(QrTest PRIVATE tests/QrTest.cpp)
target_compile_definitions(QrTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(QrTest
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags)

# Macro triggers check (see tests/TriggerTest.cpp).
juce_add_console_app(TriggerTest PRODUCT_NAME "TriggerTest")
target_sources(TriggerTest PRIVATE tests/TriggerTest.cpp)
target_compile_definitions(TriggerTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(TriggerTest
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags)

# Diagnostic: how Windows answers opening a playback device (see tests/WasapiProbe.cpp).
add_executable(WasapiProbe tests/WasapiProbe.cpp)
target_link_libraries(WasapiProbe PRIVATE ole32)

# Diagnostic: how JUCE opens the virtual cables (see tests/JuceWasapiProbe.cpp).
juce_add_console_app(JuceWasapiProbe PRODUCT_NAME "JuceWasapiProbe")
target_sources(JuceWasapiProbe PRIVATE tests/JuceWasapiProbe.cpp)
target_compile_definitions(JuceWasapiProbe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_WASAPI=1)
target_link_libraries(JuceWasapiProbe
    PRIVATE
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags)

# Plugin testing in a separate copy (see tests/PluginScanTest.cpp).
juce_add_console_app(PluginScanTest PRODUCT_NAME "PluginScanTest")
target_sources(PluginScanTest PRIVATE tests/PluginScanTest.cpp src/engine/PluginScan.cpp)
target_compile_definitions(PluginScanTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(PluginScanTest
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags)
add_dependencies(PluginScanTest FaiderTestDelay_VST3)

# Recorder check (see tests/RecorderTest.cpp).
juce_add_console_app(RecorderTest PRODUCT_NAME "RecorderTest")
target_sources(RecorderTest PRIVATE tests/RecorderTest.cpp src/engine/Recorder.cpp)
target_compile_definitions(RecorderTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(RecorderTest
    PRIVATE
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags)

# App capture as its program closes (see tests/AppCaptureTest.cpp).
juce_add_console_app(AppCaptureTest PRODUCT_NAME "AppCaptureTest")
target_sources(AppCaptureTest PRIVATE tests/AppCaptureTest.cpp src/engine/AppCapture.cpp src/engine/WindowDiscovery.cpp src/engine/DriftFifo.cpp)
target_compile_definitions(AppCaptureTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(AppCaptureTest
    PRIVATE
        juce::juce_audio_formats
        juce::juce_gui_basics
        ole32
    PUBLIC
        juce::juce_recommended_config_flags)

# Drift fifo and retro buffer check (see tests/BuffersTest.cpp).
juce_add_console_app(BuffersTest PRODUCT_NAME "BuffersTest")
target_sources(BuffersTest PRIVATE tests/BuffersTest.cpp src/engine/DriftFifo.cpp)
target_compile_definitions(BuffersTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(BuffersTest
    PRIVATE
        juce::juce_audio_basics
    PUBLIC
        juce::juce_recommended_config_flags)

# The application: Faider (console/README.md).
add_subdirectory(console)
