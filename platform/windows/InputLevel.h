#pragma once
#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#endif

namespace canto
{
inline juce::String windowsInputLevels()
{
#if JUCE_WINDOWS
    using Microsoft::WRL::ComPtr;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct Cleanup
    {
        bool uninit;
        ~Cleanup()
        {
            if (uninit)
                CoUninitialize();
        }
    } cleanup{SUCCEEDED(init)};
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(enumerator.GetAddressOf()))))
        return "Windows input levels unavailable\n";
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, collection.GetAddressOf())))
        return "Cannot enumerate Windows inputs\n";
    UINT count = 0;
    collection->GetCount(&count);
    juce::String result = "Windows endpoint input levels (read-only):\n";
    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, device.GetAddressOf())))
            continue;
        ComPtr<IPropertyStore> properties;
        juce::String name = "Input";
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf())))
        {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
                name = juce::String(value.pwszVal);
            PropVariantClear(&value);
        }
        ComPtr<IAudioEndpointVolume> volume;
        if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void**>(volume.GetAddressOf()))))
        {
            result += name + ": unavailable\n";
            continue;
        }
        float level = 0;
        BOOL muted = FALSE;
        if (FAILED(volume->GetMasterVolumeLevelScalar(&level)) || FAILED(volume->GetMute(&muted)))
            result += name + ": unavailable\n";
        else
            result +=
                name + ": " + juce::String(level * 100, 1) + "%, muted=" + (muted ? "yes" : "no") + "\n";
    }
    return result;
#else
    return "Windows input levels unavailable on this platform\n";
#endif
}
} // namespace canto
