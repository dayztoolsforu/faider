#pragma once

#include <windows.h>
#include <mmdeviceapi.h>

// Windows' own interface behind the Sound settings: choosing the default device, and a device's
// Enable/Disable. It isn't in the SDK's headers (sound utilities use it widely); its layout has
// been the same since Windows 7. Made with CoCreateInstance (__uuidof (CPolicyConfigClient)).
struct DECLSPEC_UUID ("f8679f50-850a-41cf-9c72-430f290290c8") IPolicyConfig : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat (PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat (PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat (PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat (PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod (PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod (PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode (PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode (PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue (PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue (PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint (PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility (PCWSTR deviceId, INT visible) = 0;
};
class DECLSPEC_UUID ("870af99c-171d-4f9e-af0d-e63df40c2bc9") CPolicyConfigClient;
