#include "platform/windows/NativeWasapi.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
void check(bool value, const char* reason)
{
    if (!value)
        throw std::runtime_error(reason);
}
struct Callbacks
{
    std::atomic<unsigned> prepared{0}, captured{0}, rendered{0}, failed{0};
    canto::BackendCallbacks get()
    {
        return {this,
                [](void* context, const canto::BackendFormat&)
                { ++static_cast<Callbacks*>(context)->prepared; },
                [](void* context, const float* const*, int, float* const* output, int channels,
                   int frames, bool input)
                {
                    auto& self = *static_cast<Callbacks*>(context);
                    if (input)
                        ++self.captured; // Do not inspect or retain microphone samples.
                    else
                    {
                        for (int c = 0; c < channels; ++c)
                            std::fill_n(output[c], frames, 0.f);
                        ++self.rendered;
                    }
                },
                [](void* context, uint32_t) { ++static_cast<Callbacks*>(context)->failed; }};
    }
};
void waitForAudio(const Callbacks& callbacks, unsigned captureBefore, unsigned renderBefore)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline &&
           (callbacks.captured <= captureBefore || callbacks.rendered <= renderBefore))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(callbacks.captured > captureBefore && callbacks.rendered > renderBefore,
          "capture/render callbacks did not progress within three seconds");
}
}

int main(int argc, char** argv)
{
    try
    {
        canto::NativeWasapi backend;
        Callbacks callbacks;
        canto::BackendConfiguration config{"missing-input", "missing-output", 48000, 128, true};
        check(!backend.open(config, {}).empty() && !backend.isRunning(), "null callbacks must be rejected");
        for (double rate : {0., 7999., 192001., std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity()})
        {
            config.sampleRate = rate;
            check(!backend.open(config, callbacks.get()).empty() && !backend.isRunning(),
                  "invalid rate must be rejected before starting a worker");
        }
        config.sampleRate = 48000;
        for (int period : {0, -1, 32769, std::numeric_limits<int>::max()})
        {
            config.periodFrames = period;
            check(!backend.open(config, callbacks.get()).empty() && !backend.isRunning(),
                  "invalid period must be rejected");
        }
        config.periodFrames = 128;
        config.input.clear();
        check(!backend.open(config, callbacks.get()).empty(), "empty selection must be rejected");
        check(callbacks.prepared == 0 && callbacks.captured == 0 && callbacks.rendered == 0 &&
                  callbacks.failed == 0, "invalid requests must not invoke worker callbacks");
        backend.pause(true);
        backend.pause(false);
        backend.close();
        backend.close();
        std::cout << "PASS: native argument validation and idempotent close (no device streams opened)\n";
        if (argc == 1)
            return 0;
        check((argc == 2 || argc == 4) && std::string(argv[1]) == "--hardware-silent",
              "Usage: native_backend_tests [--hardware-silent [input-name output-name]]");
        const auto devices = backend.enumerate();
        const auto input = std::find_if(devices.begin(), devices.end(), [&](const auto& d)
            { return d.input && (argc != 4 || d.name == argv[2] || d.id == argv[2]); });
        const auto output = std::find_if(devices.begin(), devices.end(), [&](const auto& d)
            { return !d.input && (argc != 4 || d.name == argv[3] || d.id == argv[3]); });
        check(input != devices.end() && output != devices.end(), "hardware test requires input and output");
        config.input = input->id;
        config.output = output->id;
        std::cout << "SILENT hardware lifecycle: " << input->name << " -> " << output->name << '\n';
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            const auto captureBefore = callbacks.captured.load(), renderBefore = callbacks.rendered.load();
            const auto error = backend.open(config, callbacks.get());
            if (!error.empty())
                throw std::runtime_error(error);
            check(backend.isRunning(), "backend must run after successful open");
            waitForAudio(callbacks, captureBefore, renderBefore);
            backend.pause(true);
            const auto pausedCapture = callbacks.captured.load(), pausedRender = callbacks.rendered.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            check(callbacks.captured == pausedCapture && callbacks.rendered == pausedRender,
                  "application callback executed after pause returned");
            backend.pause(false);
            waitForAudio(callbacks, pausedCapture, pausedRender);
            const auto format = backend.format();
            const auto counters = backend.counters();
            check(counters.lastError == 0 && callbacks.failed == 0, "runtime backend error");
            backend.close();
            const auto closedCapture = callbacks.captured.load(), closedRender = callbacks.rendered.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            check(!backend.isRunning() && callbacks.captured == closedCapture && callbacks.rendered == closedRender,
                  "callbacks continued after close");
            std::cout << "cycle " << cycle + 1 << ": " << format.inputRate << '/' << format.outputRate
                      << " Hz; periods " << format.inputPeriod << '/' << format.outputPeriod
                      << "; capture discontinuities " << counters.captureDiscontinuities
                      << "; empty-padding observations " << counters.zeroPaddingEvents << '\n';
        }
        check(callbacks.prepared == 3, "each reopen must prepare exactly once");
        std::cout << "PASS: three open/pause/resume/close cycles. No recording, tones, audibility or RTT claim.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
