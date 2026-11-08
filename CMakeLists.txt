cmake_minimum_required(VERSION 3.22)
project(Faider VERSION 0.1.6 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warnings as errors for Faider and its checks (CI turns it on; off by default, so a newer compiler's
# new warning doesn't stop a local build).
option(FAIDER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in Faider and its checks" OFF)

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

# faider_core: the engine and the session (src/engine, src/devices, src/Session.cpp), built once for
# Faider and SessionTest. JUCE's modules are compiled into each program that links it (that's how
# JUCE builds), so faider_core takes only their headers and Faider's JUCE settings: a program linking
# it must use the same settings. The programs have different data folders, so faider_core may not use
# one (FAIDER_NO_DATA_FOLDER: ProductName.h); the code that does (WindowDiscovery.cpp, PluginScan.cpp)
# is built in each program. Each program also has its own VoiceChain::createEditor.
set(FAIDER_JUCE_SETTINGS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_WASAPI=1 JUCE_PLUGINHOST_VST3=1 JUCE_DIRECTSOUND=0
    JUCE_USE_MP3AUDIOFORMAT=1)
set(_juce_modules juce_audio_utils juce_dsp juce_osc)
add_library(faider_core STATIC
    src/Session.cpp
    src/engine/AudioEngine.cpp
    src/engine/DriftFifo.cpp
    src/engine/BuiltInClock.cpp
    src/engine/HeadphoneEq.cpp
    src/engine/AppCapture.cpp
    src/engine/Pads.cpp
    src/engine/Tape.cpp
    src/engine/Recorder.cpp
    src/engine/Macros.cpp
    src/engine/Scenes.cpp
    src/engine/RemoteControl.cpp
    src/engine/VoiceChain.cpp
    src/engine/NeuralNoise.cpp
    src/devices/EndpointWatcher.cpp
    src/devices/WindowsDefault.cpp)
foreach(_module IN LISTS _juce_modules)
    target_include_directories(faider_core PRIVATE $<TARGET_PROPERTY:${_module},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(faider_core PRIVATE $<TARGET_PROPERTY:${_module},INTERFACE_COMPILE_DEFINITIONS>)
    target_compile_options(faider_core PRIVATE $<TARGET_PROPERTY:${_module},INTERFACE_COMPILE_OPTIONS>)
endforeach()
target_compile_definitions(faider_core PRIVATE FAIDER_NO_DATA_FOLDER JUCE_STANDALONE_APPLICATION=1 ${FAIDER_JUCE_SETTINGS})
target_link_libraries(faider_core
    PUBLIC
        FaiderAssets
        rnnoise
        ole32
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(faider_core PRIVATE $<$<CONFIG:Release>:/Zi>)   # (Faider's release PDB: see console/CMakeLists.txt)
endif()

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

# Macro triggers check (see tests/TriggerTest.cpp).
juce_add_console_app(TriggerTest PRODUCT_NAME "TriggerTest")
target_sources(TriggerTest PRIVATE tests/TriggerTest.cpp)
target_compile_definitions(TriggerTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(TriggerTest
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags)

# Update check: GitHub's answer read, without the network (see tests/UpdateCheckTest.cpp).
juce_add_console_app(UpdateCheckTest PRODUCT_NAME "UpdateCheckTest")
target_sources(UpdateCheckTest PRIVATE tests/UpdateCheckTest.cpp)
target_compile_definitions(UpdateCheckTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(UpdateCheckTest
    PRIVATE
        juce::juce_events
    PUBLIC
        juce::juce_recommended_config_flags)

# BOOST WINDOWS AUDIO: the helper's command line, PRE-FLIGHT's verdict and the watch, without
# touching Windows audio (see tests/AudioBoostTest.cpp).
juce_add_console_app(AudioBoostTest PRODUCT_NAME "AudioBoostTest")
target_sources(AudioBoostTest PRIVATE tests/AudioBoostTest.cpp)
target_compile_definitions(AudioBoostTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(AudioBoostTest
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
target_compile_definitions(PluginScanTest PRIVATE FAIDER_DATA_FOLDER="FaiderPluginScanTest" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
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

# Saved mixes load: a session's save/restore round trip, headless (see tests/SessionTest.cpp).
# Its own data folder (FaiderSessionTest), so it never touches %APPDATA%\Faider; no audio
# device is opened, and the ASIO SDK isn't needed.
juce_add_console_app(SessionTest PRODUCT_NAME "SessionTest")
target_sources(SessionTest PRIVATE tests/SessionTest.cpp src/engine/WindowDiscovery.cpp)
target_compile_definitions(SessionTest PRIVATE FAIDER_DATA_FOLDER="FaiderSessionTest" ${FAIDER_JUCE_SETTINGS})
target_link_libraries(SessionTest
    PRIVATE
        faider_core
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags)

# App capture as its program closes (see tests/AppCaptureTest.cpp).
juce_add_console_app(AppCaptureTest PRODUCT_NAME "AppCaptureTest")
target_sources(AppCaptureTest PRIVATE tests/AppCaptureTest.cpp src/engine/AppCapture.cpp src/engine/WindowDiscovery.cpp src/engine/DriftFifo.cpp)
target_compile_definitions(AppCaptureTest PRIVATE FAIDER_DATA_FOLDER="FaiderAppCaptureTest" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
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

# The look a theme file describes check (see tests/ThemeTest.cpp).
juce_add_console_app(ThemeTest PRODUCT_NAME "ThemeTest")
target_sources(ThemeTest PRIVATE tests/ThemeTest.cpp src/ui/Theme.cpp)
target_compile_definitions(ThemeTest PRIVATE FAIDER_DATA_FOLDER="FaiderThemeTest" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ThemeTest
    PRIVATE
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags)

# The desk paints from the look, not from colours written in by hand (see tests/LookRenderTest.cpp).
juce_add_console_app(LookRenderTest PRODUCT_NAME "LookRenderTest")
target_sources(LookRenderTest PRIVATE tests/LookRenderTest.cpp src/ui/Theme.cpp)
# Its own data folder (FaiderLookTest): the looks it writes never go in %APPDATA%\Faider\looks.
target_compile_definitions(LookRenderTest PRIVATE FAIDER_DATA_FOLDER="FaiderLookTest" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(LookRenderTest
    PRIVATE
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags)

# The application: Faider (console/README.md).
add_subdirectory(console)

if(MSVC AND FAIDER_WARNINGS_AS_ERRORS)
    foreach(_target Faider faider_core FaiderTestDelay MeterTest PadsTest TapeTest NoiseReducerTest VoiceChainTest
                    DefaultDeviceTest TriggerTest UpdateCheckTest AudioBoostTest WasapiProbe JuceWasapiProbe
                    PluginScanTest RecorderTest SessionTest AppCaptureTest BuffersTest ThemeTest LookRenderTest)
        target_compile_options(${_target} PRIVATE /WX)
    endforeach()
endif()
