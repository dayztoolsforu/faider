# PATCHBAY CONSOLE (see console/README.md): the engine in src/ with the console as its Mixer and Routing
# pages (console/src/Console.cpp), its files in %APPDATA%\PatchbayConsole.
juce_add_gui_app(PatchbayConsole PRODUCT_NAME "Patchbay Console"
    ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icons/patchbay-console.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/assets/icons/patchbay-console-small.png")
set(_src "${CMAKE_SOURCE_DIR}/src")
target_sources(PatchbayConsole PRIVATE
    src/Console.cpp
    src/Simple.cpp
    ${_src}/Main.cpp
    ${_src}/MainComponent.cpp
    ${_src}/Session.cpp
    ${_src}/engine/DriftFifo.cpp
    ${_src}/engine/BuiltInClock.cpp
    ${_src}/engine/HeadphoneEq.cpp
    ${_src}/engine/AppCapture.cpp
    ${_src}/engine/WindowDiscovery.cpp
    ${_src}/engine/Pads.cpp
    ${_src}/engine/Tape.cpp
    ${_src}/engine/Recorder.cpp
    ${_src}/engine/PluginScan.cpp
    ${_src}/engine/Macros.cpp
    ${_src}/engine/Scenes.cpp
    ${_src}/engine/RemoteControl.cpp
    ${_src}/engine/ControlServer.cpp
    ${_src}/engine/VoiceChain.cpp
    ${_src}/engine/NeuralNoise.cpp
    ${_src}/ui/VoiceChainEditor.cpp
    ${_src}/ui/ControllerHelper.cpp
    ${_src}/engine/AudioEngine.cpp
    ${_src}/ui/MixerComponent.cpp
    ${_src}/ui/DevicesTab.cpp
    ${_src}/ui/PadsPanel.cpp
    ${_src}/ui/TapePanel.cpp
    ${_src}/ui/Hotkeys.cpp
    ${_src}/ui/Overlay.cpp
    ${_src}/vr/VrDashboard.cpp
    ${_src}/devices/EndpointWatcher.cpp
    ${_src}/devices/WindowsDefault.cpp
    ${_src}/ui/WindowBackground.cpp
    ${_src}/ui/WindowSnap.cpp)
target_compile_definitions(PatchbayConsole PRIVATE
    PATCHBAY_DATA_FOLDER="PatchbayConsole"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_WASAPI=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_DIRECTSOUND=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PatchbayConsole,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PatchbayConsole,JUCE_VERSION>")
if(ASIO_SDK_DIR)
    target_compile_definitions(PatchbayConsole PRIVATE JUCE_ASIO=1)
    target_include_directories(PatchbayConsole PRIVATE "${ASIO_SDK_DIR}/common")
endif()
target_include_directories(PatchbayConsole PRIVATE "${CMAKE_SOURCE_DIR}/third_party/openvr/headers")
target_link_libraries(PatchbayConsole
    PRIVATE
        PatchbayAssets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_osc
        rnnoise
        "${CMAKE_SOURCE_DIR}/third_party/openvr/lib/win64/openvr_api.lib"
        ole32
        delayimp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
target_link_options(PatchbayConsole PRIVATE "/DELAYLOAD:openvr_api.dll")
# A PDB for the release build too (the code is the same): crash.log's module+offset lines and
# crash.dmp (Main.cpp, CrashLog) can then be read back to the function. The exe names its PDB without
# the build folder's path.
if(MSVC)
    target_compile_options(PatchbayConsole PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(PatchbayConsole PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL /OPT:REF /OPT:ICF /PDBALTPATH:%_PDB%>)
endif()
# The deck plugins beside the program (the controller helper installs them from there). The LoupixDeck one
# is built with the .NET 10 SDK (dotnet build plugins/loupixdeck/LoupixDeck.Plugin.Patchbay -c Release); its
# build is copied if it's there at configure time.
add_custom_command(TARGET PatchbayConsole POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/plugins/streamdeck/com.patchbay.desk.sdPlugin"
            "$<TARGET_FILE_DIR:PatchbayConsole>/plugins/streamdeck/com.patchbay.desk.sdPlugin"
    VERBATIM)
set(_lpx_console "${CMAKE_SOURCE_DIR}/plugins/loupixdeck/LoupixDeck.Plugin.Patchbay/bin/Release")
if(EXISTS "${_lpx_console}/LoupixDeck.Plugin.Patchbay.dll")
    add_custom_command(TARGET PatchbayConsole POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PatchbayConsole>/plugins/loupixdeck"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_lpx_console}/LoupixDeck.Plugin.Patchbay.dll" "${_lpx_console}/LoupixDeck.Plugin.Patchbay.deps.json"
                "${_lpx_console}/plugin.json" "${_lpx_console}/icon.png"
                "$<TARGET_FILE_DIR:PatchbayConsole>/plugins/loupixdeck"
        VERBATIM)
endif()
add_custom_command(TARGET PatchbayConsole POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/openvr/bin/win64/openvr_api.dll" "$<TARGET_FILE_DIR:PatchbayConsole>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/layout/mixer.json" "${CMAKE_CURRENT_SOURCE_DIR}/layout/mixer-dark.json" "${CMAKE_CURRENT_SOURCE_DIR}/layout/master.json" "${CMAKE_CURRENT_SOURCE_DIR}/layout/app.json" "${CMAKE_CURRENT_SOURCE_DIR}/layout/pads.json" "${CMAKE_CURRENT_SOURCE_DIR}/layout/tape.json" "${CMAKE_CURRENT_SOURCE_DIR}/layout/combinator.json" "$<TARGET_FILE_DIR:PatchbayConsole>")
