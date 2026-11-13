#include "DevicesTab.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#pragma comment (lib, "setupapi.lib")
#pragma comment (lib, "cfgmgr32.lib")

bool DevicesTab::runElevated (const juce::String& command)
{
    const juce::String params ("/c " + command);
    const auto result = (INT_PTR) ShellExecuteW (nullptr, L"runas", L"cmd.exe", params.toWideCharPointer(), nullptr, SW_HIDE);
    return result > 32;   // ShellExecute: <= 32 is an error, including "the user declined"
}

juce::String DevicesTab::findSoundDevice (const juce::String& description)
{
    juce::String found;
    const HDEVINFO devices = SetupDiGetClassDevsW (&GUID_DEVCLASS_MEDIA, nullptr, nullptr, 0);   // (every sound device, connected or not)
    if (devices == INVALID_HANDLE_VALUE)
        return found;

    SP_DEVINFO_DATA info {};
    info.cbSize = sizeof (info);
    for (DWORD i = 0; found.isEmpty() && SetupDiEnumDeviceInfo (devices, i, &info); ++i)
    {
        // Its description, or the name it shows under (a USB device's own name is there).
        auto holds = [&] (DWORD property)
        {
            wchar_t text[512] {};
            return SetupDiGetDeviceRegistryPropertyW (devices, &info, property, nullptr, (PBYTE) text, sizeof (text) - sizeof (wchar_t), nullptr)
                && juce::String (text).contains (description);
        };
        wchar_t id[MAX_DEVICE_ID_LEN + 1] {};
        if ((holds (SPDRP_DEVICEDESC) || holds (SPDRP_FRIENDLYNAME))
            && SetupDiGetDeviceInstanceIdW (devices, &info, id, MAX_DEVICE_ID_LEN, nullptr))
            found = juce::String (id);
    }
    SetupDiDestroyDeviceInfoList (devices);
    return found;
}

bool DevicesTab::nodeDisabled (const juce::String& instanceId)
{
    DEVINST node = 0;
    if (CM_Locate_DevNodeW (&node, const_cast<DEVINSTID_W> (instanceId.toWideCharPointer()), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return false;
    ULONG status = 0, problem = 0;
    return CM_Get_DevNode_Status (&status, &problem, node, 0) == CR_SUCCESS
        && (status & DN_HAS_PROBLEM) != 0 && problem == CM_PROB_DISABLED;
}
