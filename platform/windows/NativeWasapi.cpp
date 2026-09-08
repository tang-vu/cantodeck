#include "NativeWasapi.h"
#include "engine/Core.h"
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <avrt.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <future>
#include <thread>

namespace canto
{
namespace
{
using Microsoft::WRL::ComPtr;
struct ComScope
{
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope()
    {
        if (SUCCEEDED(status))
            CoUninitialize();
    }
};
struct Handle
{
    HANDLE value = nullptr;
    ~Handle()
    {
        if (value)
            CloseHandle(value);
    }
    void event(bool manual = false)
    {
        value = CreateEventW(nullptr, manual, FALSE, nullptr);
        if (!value)
            throw HRESULT_FROM_WIN32(GetLastError());
    }
};
void require(HRESULT hr)
{
    if (FAILED(hr))
        throw hr;
}
std::string utf8(const wchar_t* value)
{
    if (!value)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, value, -1, s.data(), n, nullptr, nullptr);
    if (!s.empty())
        s.pop_back();
    return s;
}
std::string errorText(HRESULT hr)
{
    char buf[64]{};
    sprintf_s(buf, "Native WASAPI failed (HRESULT 0x%08lX)", static_cast<unsigned long>(hr));
    return buf;
}
ComPtr<IMMDeviceEnumerator> enumerator()
{
    ComPtr<IMMDeviceEnumerator> result;
    require(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                             IID_PPV_ARGS(result.GetAddressOf())));
    return result;
}
std::vector<EndpointDescription> list(IMMDeviceEnumerator* e)
{
    std::vector<EndpointDescription> result;
    for (auto flow : {eCapture, eRender})
    {
        ComPtr<IMMDeviceCollection> collection;
        require(e->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.GetAddressOf()));
        UINT count = 0;
        require(collection->GetCount(&count));
        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IMMDevice> device;
            require(collection->Item(i, device.GetAddressOf()));
            LPWSTR id = nullptr;
            require(device->GetId(&id));
            std::string identifier = utf8(id);
            CoTaskMemFree(id);
            ComPtr<IPropertyStore> properties;
            require(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf()));
            PROPVARIANT name;
            PropVariantInit(&name);
            auto hr = properties->GetValue(PKEY_Device_FriendlyName, &name);
            std::string friendly = SUCCEEDED(hr) && name.vt == VT_LPWSTR ? utf8(name.pwszVal) : identifier;
            PropVariantClear(&name);
            result.push_back({identifier, friendly, flow == eCapture});
        }
    }
    // Match JUCE's disambiguated display-name convention; IDs remain the identity.
    for (size_t i = 0; i < result.size(); ++i)
    {
        const auto original = result[i].name;
        int count = 0;
        for (const auto& d : result)
            if (d.input == result[i].input && d.name == original)
                ++count;
        if (count > 1)
        {
            int number = 1;
            for (size_t j = i; j < result.size(); ++j)
                if (result[j].input == result[i].input && result[j].name == original)
                    result[j].name += " (" + std::to_string(number++) + ")";
        }
    }
    return result;
}
ComPtr<IMMDevice> findDevice(IMMDeviceEnumerator* e, const std::vector<EndpointDescription>& devices,
                             const std::string& selected, bool input)
{
    auto it = std::find_if(devices.begin(), devices.end(),
                           [&](const auto& d)
                           { return d.input == input && (d.id == selected || d.name == selected); });
    if (it == devices.end())
        throw HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    const int n = MultiByteToWideChar(CP_UTF8, 0, it->id.c_str(), -1, nullptr, 0);
    std::wstring id(size_t(n), 0);
    MultiByteToWideChar(CP_UTF8, 0, it->id.c_str(), -1, id.data(), n);
    ComPtr<IMMDevice> device;
    require(e->GetDevice(id.c_str(), device.GetAddressOf()));
    return device;
}
struct Stream
{
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IAudioRenderClient> render;
    Handle signal;
    WAVEFORMATEXTENSIBLE wave{};
    UINT32 capacity = 0, period = 0;
    REFERENCE_TIME latency = 0;
    bool floating = false, low = false, raw = false;
    void initialise(IMMDevice* device, const BackendConfiguration& config, bool input)
    {
        signal.event();
        auto attempt = [&](bool wantRaw, bool wantLow)
        {
            client.Reset();
            require(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void**>(client.GetAddressOf())));
            raw = false;
            low = false;
            ComPtr<IAudioClient2> client2;
            if (wantRaw && SUCCEEDED(client.As(&client2)))
            {
                AudioClientProperties properties{};
                properties.cbSize = sizeof(properties);
                properties.eCategory = AudioCategory_Media;
                properties.Options = AUDCLNT_STREAMOPTIONS_RAW;
                raw = SUCCEEDED(client2->SetClientProperties(&properties));
            }
            WAVEFORMATEX* mix = nullptr;
            require(client->GetMixFormat(&mix));
            wave = {};
            std::memcpy(&wave, mix, std::min(sizeof(wave), sizeof(WAVEFORMATEX) + size_t(mix->cbSize)));
            CoTaskMemFree(mix);
            auto proposed = wave;
            proposed.Format.nSamplesPerSec = DWORD(config.sampleRate);
            proposed.Format.nAvgBytesPerSec = proposed.Format.nSamplesPerSec * proposed.Format.nBlockAlign;
            WAVEFORMATEX* closest = nullptr;
            auto supported = client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &proposed.Format, &closest);
            if (closest)
                CoTaskMemFree(closest);
            if (supported == S_OK)
                wave = proposed;
            floating = wave.Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                       (wave.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                        IsEqualGUID(wave.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
            const bool pcm = wave.Format.wFormatTag == WAVE_FORMAT_PCM ||
                             (wave.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                              IsEqualGUID(wave.SubFormat, KSDATAFORMAT_SUBTYPE_PCM));
            if ((floating && wave.Format.wBitsPerSample != 32) ||
                (!floating &&
                 (!pcm || (wave.Format.wBitsPerSample != 16 && wave.Format.wBitsPerSample != 24 &&
                           wave.Format.wBitsPerSample != 32))))
                throw AUDCLNT_E_UNSUPPORTED_FORMAT;
            ComPtr<IAudioClient3> client3;
            UINT32 def = 0, fundamental = 0, minimum = 0, maximum = 0;
            if (wantLow && SUCCEEDED(client.As(&client3)) &&
                SUCCEEDED(client3->GetSharedModeEnginePeriod(&wave.Format, &def, &fundamental, &minimum,
                                                             &maximum)) &&
                fundamental > 0)
            {
                period = std::clamp(
                    UINT32((std::max(1, config.periodFrames) + fundamental - 1) / fundamental * fundamental),
                    minimum, maximum);
                require(client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period,
                                                             &wave.Format, nullptr));
                low = true;
            }
            else
            {
                REFERENCE_TIME defTime = 0, minTime = 0;
                require(client->GetDevicePeriod(&defTime, &minTime));
                period = UINT32(std::ceil(defTime * wave.Format.nSamplesPerSec / 10000000.0));
                require(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0,
                                           &wave.Format, nullptr));
            }
        };
        try
        {
            attempt(input, config.lowLatency);
        }
        catch (HRESULT)
        {
            try
            {
                attempt(false, config.lowLatency);
            }
            catch (HRESULT)
            {
                attempt(false, false);
            }
        }
        require(client->GetBufferSize(&capacity));
        require(client->GetStreamLatency(&latency));
        require(client->SetEventHandle(signal.value));
        if (capacity == 0 || capacity > 32768 || wave.Format.nChannels == 0 || wave.Format.nChannels > 64)
            throw E_INVALIDARG;
        if (input)
            require(client->GetService(IID_PPV_ARGS(capture.GetAddressOf())));
        else
            require(client->GetService(IID_PPV_ARGS(render.GetAddressOf())));
    }
    float read(const BYTE* data, UINT32 frame, UINT32 channel) const noexcept
    {
        const auto* p =
            data + size_t(frame) * wave.Format.nBlockAlign + channel * (wave.Format.wBitsPerSample / 8);
        if (floating)
        {
            float v;
            std::memcpy(&v, p, 4);
            return clean(v);
        }
        if (wave.Format.wBitsPerSample == 16)
        {
            int16_t v;
            std::memcpy(&v, p, 2);
            return v / 32768.f;
        }
        if (wave.Format.wBitsPerSample == 24)
        {
            int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
            if (v & 0x800000)
                v |= int32_t(0xff000000);
            return v / 8388608.f;
        }
        int32_t v;
        std::memcpy(&v, p, 4);
        return float(v / 2147483648.0);
    }
    void write(BYTE* data, UINT32 frame, UINT32 channel, float sample) const noexcept
    {
        auto* p = data + size_t(frame) * wave.Format.nBlockAlign + channel * (wave.Format.wBitsPerSample / 8);
        sample = std::clamp(clean(sample), -1.f, 1.f);
        if (floating)
        {
            std::memcpy(p, &sample, 4);
            return;
        }
        if (wave.Format.wBitsPerSample == 16)
        {
            const int16_t v = int16_t(sample * 32767);
            std::memcpy(p, &v, 2);
            return;
        }
        if (wave.Format.wBitsPerSample == 24)
        {
            const int32_t v = int32_t(sample * 8388607);
            p[0] = BYTE(v);
            p[1] = BYTE(v >> 8);
            p[2] = BYTE(v >> 16);
            return;
        }
        const int32_t v = int32_t(double(sample) * 2147483647.0);
        std::memcpy(p, &v, 4);
    }
};
} // namespace
struct NativeWasapi::Impl
{
    std::thread thread;
    Handle stop;
    std::atomic<bool> running{false}, paused{false}, inside{false};
    std::atomic<uint64_t> captured{0}, rendered{0}, discontinuities{0}, starvations{0};
    std::atomic<uint32_t> error{0};
    BackendFormat actual;
    BackendCallbacks callbacks;
    Impl() { stop.event(true); }
    void worker(BackendConfiguration config, std::promise<std::string> ready) noexcept
    {
        bool published = false;
        ComScope com;
        Stream input, output;
        HANDLE mmcss = nullptr;
        try
        {
            require(com.status);
            auto e = enumerator();
            auto devices = list(e.Get());
            auto in = findDevice(e.Get(), devices, config.input, true),
                 out = findDevice(e.Get(), devices, config.output, false);
            input.initialise(in.Get(), config, true);
            output.initialise(out.Get(), config, false);
            const UINT32 queueTarget =
                std::min(output.capacity,
                         output.period + std::max<UINT32>(32, output.wave.Format.nSamplesPerSec / 1000));
            actual = {double(input.wave.Format.nSamplesPerSec),
                      double(output.wave.Format.nSamplesPerSec),
                      int(input.period),
                      int(output.period),
                      int(input.capacity),
                      int(output.capacity),
                      int(queueTarget),
                      input.latency / 10000000.0,
                      output.latency / 10000000.0,
                      input.low,
                      output.low,
                      input.raw};
            std::array<std::vector<float>, 2> inBuffer, outBuffer;
            for (auto& b : inBuffer)
                b.resize(32768);
            for (auto& b : outBuffer)
                b.resize(output.capacity);
            const float* ins[] = {inBuffer[0].data(), inBuffer[1].data()};
            float* outs[] = {outBuffer[0].data(), outBuffer[1].data()};
            callbacks.prepare(callbacks.context, actual);
            BYTE* initial = nullptr;
            require(output.render->GetBuffer(queueTarget, &initial));
            require(output.render->ReleaseBuffer(queueTarget, AUDCLNT_BUFFERFLAGS_SILENT));
            DWORD taskIndex = 0;
            mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
            if (mmcss)
                AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);
            require(input.client->Start());
            require(output.client->Start());
            running = true;
            ready.set_value({});
            published = true;
            HANDLE events[] = {stop.value, output.signal.value, input.signal.value};
            while (true)
            {
                const DWORD event = WaitForMultipleObjects(3, events, FALSE, 1000);
                if (event == WAIT_OBJECT_0)
                    break;
                if (event == WAIT_TIMEOUT)
                    throw HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                if (event == WAIT_FAILED)
                    throw HRESULT_FROM_WIN32(GetLastError());
                // The two endpoints are serviced by one MMCSS thread. Drain
                // capture before each render; no cross-thread callback lock.
                inside.store(true, std::memory_order_seq_cst);
                const bool silenced = paused.load(std::memory_order_seq_cst);
                UINT32 packet = 0;
                require(input.capture->GetNextPacketSize(&packet));
                for (int batch = 0; packet > 0 && batch < 64; ++batch)
                {
                    BYTE* data = nullptr;
                    UINT32 n = 0;
                    DWORD flags = 0;
                    require(input.capture->GetBuffer(&data, &n, &flags, nullptr, nullptr));
                    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                        ++discontinuities;
                    const int channels = std::min<int>(2, input.wave.Format.nChannels);
                    for (UINT32 offset = 0; offset < n; offset += 32768)
                    {
                        const int frames = int(std::min<UINT32>(32768, n - offset));
                        if (!silenced)
                        {
                            for (int c = 0; c < channels; ++c)
                                for (int k = 0; k < frames; ++k)
                                    inBuffer[c][size_t(k)] =
                                        (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                                            ? 0.f
                                            : input.read(data, offset + UINT32(k), UINT32(c));
                            callbacks.process(callbacks.context, ins, channels, nullptr, 0, frames, true);
                        }
                    }
                    require(input.capture->ReleaseBuffer(n));
                    ++captured;
                    require(input.capture->GetNextPacketSize(&packet));
                }
                if (event == WAIT_OBJECT_0 + 1)
                {
                    UINT32 padding = 0;
                    require(output.client->GetCurrentPadding(&padding));
                    if (padding > output.capacity)
                        throw E_UNEXPECTED;
                    if (padding == 0 && rendered.load() > 0)
                        ++starvations;
                    // The WASAPI allocation can exceed two periods. Keeping it
                    // full adds avoidable queueing; maintain one engine period
                    // plus a 1 ms scheduling guard, within the allocated capacity.
                    const UINT32 n = padding < queueTarget ? queueTarget - padding : 0;
                    if (n > 0)
                    {
                        BYTE* data = nullptr;
                        require(output.render->GetBuffer(n, &data));
                        if (!silenced)
                        {
                            callbacks.process(callbacks.context, nullptr, 0, outs, 2, int(n), false);
                            for (UINT32 k = 0; k < n; ++k)
                                for (UINT32 c = 0; c < output.wave.Format.nChannels; ++c)
                                    output.write(data, k, c,
                                                 output.wave.Format.nChannels == 1
                                                     ? (outs[0][k] + outs[1][k]) * 0.5f
                                                 : c < 2 ? outs[c][k]
                                                         : 0.f);
                        }
                        require(output.render->ReleaseBuffer(n, silenced ? AUDCLNT_BUFFERFLAGS_SILENT : 0));
                        ++rendered;
                    }
                }
                inside.store(false, std::memory_order_seq_cst);
            }
        }
        catch (HRESULT hr)
        {
            error = uint32_t(hr);
            if (!published)
            {
                ready.set_value(errorText(hr));
                published = true;
            }
            if (callbacks.failed)
                callbacks.failed(callbacks.context, uint32_t(hr));
        }
        catch (...)
        {
            error = uint32_t(E_FAIL);
            if (!published)
                ready.set_value("Native WASAPI initialization/runtime exception");
            if (callbacks.failed)
                callbacks.failed(callbacks.context, uint32_t(E_FAIL));
        }
        inside.store(false, std::memory_order_seq_cst);
        running = false;
        if (input.client)
            input.client->Stop();
        if (output.client)
            output.client->Stop();
        if (mmcss)
            AvRevertMmThreadCharacteristics(mmcss);
    }
};
NativeWasapi::NativeWasapi() : impl(std::make_unique<Impl>())
{
}
NativeWasapi::~NativeWasapi()
{
    close();
}
std::vector<EndpointDescription> NativeWasapi::enumerate()
{
    ComScope com;
    try
    {
        auto e = enumerator();
        return list(e.Get());
    }
    catch (...)
    {
        return {};
    }
}
std::string NativeWasapi::open(const BackendConfiguration& config, BackendCallbacks callbacks)
{
    close();
    impl->actual = {};
    impl->callbacks = callbacks;
    impl->error = 0;
    impl->captured = 0;
    impl->rendered = 0;
    impl->discontinuities = 0;
    impl->starvations = 0;
    impl->paused = false;
    if (!callbacks.prepare || !callbacks.process || !std::isfinite(config.sampleRate) ||
        config.sampleRate < 8000 || config.sampleRate > 192000 || config.periodFrames < 1 ||
        config.periodFrames > 32768 || config.input.empty() || config.output.empty())
    {
        impl->error = uint32_t(E_INVALIDARG);
        return "Native WASAPI requires endpoints, prepare/process callbacks, 8-192 kHz and 1-32768 frames";
    }
    ResetEvent(impl->stop.value);
    std::promise<std::string> ready;
    auto result = ready.get_future();
    impl->thread =
        std::thread([this, config, p = std::move(ready)]() mutable { impl->worker(config, std::move(p)); });
    return result.get();
}
void NativeWasapi::close()
{
    SetEvent(impl->stop.value);
    if (impl->thread.joinable())
        impl->thread.join();
    impl->running = false;
}
void NativeWasapi::pause(bool value)
{
    impl->paused.store(value, std::memory_order_seq_cst);
    if (value)
        while (impl->inside.load(std::memory_order_seq_cst))
            std::this_thread::yield();
}
bool NativeWasapi::isRunning() const
{
    return impl->running.load();
}
BackendFormat NativeWasapi::format() const
{
    return impl->actual;
}
BackendCounters NativeWasapi::counters() const
{
    return {impl->captured.load(), impl->rendered.load(), impl->discontinuities.load(),
            impl->starvations.load(), impl->error.load()};
}
} // namespace canto
