# FAIDER (see console/README.md): the engine in src/ with the console as its Mixer and Routing
# pages (console/src/Console.cpp), its files in %APPDATA%\Faider.
juce_add_gui_app(Faider PRODUCT_NAME "Faider"
    ICON_BIG "${CMAKE_SOURCE_DIR}/assets/icons/faider.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/assets/icons/faider-small.png")
set(_src "${CMAKE_SOURCE_DIR}/src")
target_sources(Faider PRIVATE
    src/Console.cpp
    src/Simple.cpp
    ${_src}/Main.cpp
    ${_src}/MainComponent.cpp
    ${_src}/AudioBoost.cpp
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
    ${_src}/engine/VoiceChain.cpp
    ${_src}/engine/NeuralNoise.cpp
    ${_src}/ui/VoiceChainEditor.cpp
    ${_src}/engine/AudioEngine.cpp
    ${_src}/ui/MixerComponent.cpp
    ${_src}/ui/DevicesTab.cpp
    ${_src}/ui/PadsPanel.cpp
    ${_src}/ui/TapePanel.cpp
    ${_src}/ui/Hotkeys.cpp
    ${_src}/ui/Overlay.cpp
    ${_src}/devices/EndpointWatcher.cpp
    ${_src}/devices/WindowsDefault.cpp
    ${_src}/ui/WindowBackground.cpp
    ${_src}/ui/WindowSnap.cpp
    ${_src}/ui/Theme.cpp)
target_compile_definitions(Faider PRIVATE
    FAIDER_DATA_FOLDER="Faider"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_WASAPI=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_DIRECTSOUND=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Faider,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Faider,JUCE_VERSION>")
if(ASIO_SDK_DIR)
    target_compile_definitions(Faider PRIVATE JUCE_ASIO=1)
    target_include_directories(Faider PRIVATE "${ASIO_SDK_DIR}/common")
endif()
target_link_libraries(Faider
    PRIVATE
        FaiderAssets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_osc
        rnnoise
        ole32
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
# A PDB for the release build too (the code is the same): crash.log's module+offset lines and
# crash.dmp (Main.cpp, CrashLog) can then be read back to the function. The exe names its PDB without
# the build folder's path.
if(MSVC)
    target_compile_options(Faider PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(Faider PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL /OPT:REF /OPT:ICF /PDBALTPATH:%_PDB%>)
endif()
# The layout files (layout/*.json) go next to the exe. Each is copied again when it changes, without
# Faider relinking; a file added to layout/ is picked up at the next build.
get_target_property(_out Faider RUNTIME_OUTPUT_DIRECTORY)   # (Faider_artefacts/<config>)
file(GLOB _layouts CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/layout/*.json")
set(_copied "")
foreach(_layout IN LISTS _layouts)
    get_filename_component(_name "${_layout}" NAME)
    add_custom_command(OUTPUT "${_out}/${_name}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_out}"
        COMMAND ${CMAKE_COMMAND} -E copy "${_layout}" "${_out}/${_name}"
        DEPENDS "${_layout}"
        COMMENT "Copying layout/${_name}"
        VERBATIM)
    list(APPEND _copied "${_out}/${_name}")
endforeach()
add_custom_target(FaiderLayouts DEPENDS ${_copied})
add_dependencies(Faider FaiderLayouts)
