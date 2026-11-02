# A change to JUCE's own source, made at every configure (once: it looks for its marker first), so it
# also reaches a JUCE that was fetched before the change was added.
#
# Popup menus with a parent component: JUCE closes any popup menu within ~50 ms when this app isn't
# the foreground one and the real mouse isn't over the menu (juce_PopupMenu.cpp,
# doesAnyJuceCompHaveFocus / checkButtonState). The SteamVR dashboard's page (src/vr) opens its menus
# and combo-box lists inside itself, worked by the laser pointer as a pen while a game has the focus,
# so they closed as they opened. A menu with a parent component lives inside one of our own windows
# and still closes on a click outside it (inputAttemptWhenModal), so for those the check is skipped.
# (Patchbay gives menus a parent only in the headset: Console::Look::menuParent.)

function(patchbay_patch_juce juceSourceDir)
    set(popupMenu "${juceSourceDir}/modules/juce_gui_basics/menus/juce_PopupMenu.cpp")
    set(marker "PATCHBAY: parented menus")
    set(anchor "if (! detail::WindowingHelpers::isForegroundOrEmbeddedProcess (componentAttachedTo))")
    set(patched "if (options.getParentComponent() != nullptr) return true; /* ${marker}: in one of our windows, worked by any pointer */ ${anchor}")

    file(READ "${popupMenu}" source)
    string(FIND "${source}" "${marker}" alreadyPatched)
    if(NOT alreadyPatched EQUAL -1)
        return()
    endif()
    string(FIND "${source}" "${anchor}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "PatchJuce: juce_PopupMenu.cpp has changed (JUCE updated?): see cmake/PatchJuce.cmake")
    endif()
    string(REPLACE "${anchor}" "${patched}" source "${source}")
    file(WRITE "${popupMenu}" "${source}")
    message(STATUS "PatchJuce: popup menus with a parent component stay open when Patchbay isn't in front")
endfunction()
