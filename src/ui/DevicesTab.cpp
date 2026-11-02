#include "DevicesTab.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

bool DevicesTab::runElevated (const juce::String& command)
{
    const juce::String params ("/c " + command);
    const auto result = (INT_PTR) ShellExecuteW (nullptr, L"runas", L"cmd.exe", params.toWideCharPointer(), nullptr, SW_HIDE);
    return result > 32;   // ShellExecute: <= 32 is an error, including "the user declined"
}
