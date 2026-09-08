#include "engine/Core.h"
#include <iostream>
#include <utility>

// Event-driven synthetic device clocks. Capture is not grouped by render block:
// phase and bounded delivery jitter independently change callback ordering.
int main(int argc, char** argv)
{
    const double jitter = argc == 2 && std::string(argv[1]) == "--no-jitter" ? 0.0 : 0.2;
    for (bool adaptive : {false, true})
    {
        canto::ClockBridge bridge;
        bridge.prepare(48000, 48000, 128, adaptive);
        const auto initial = bridge.targetFrames();
        for (uint64_t cycle = 1; cycle <= 4; ++cycle)
        {
            for (size_t i = 0; i < bridge.targetFrames() + 32; ++i)
                bridge.push(0.1f);
            bridge.beginBlock();
            for (int i = 0; i < 32768 && bridge.underruns < cycle; ++i)
                bridge.next();
            const auto expected = initial + (adaptive ? std::min(cycle, uint64_t(2)) * 64 : 0);
            if (bridge.underruns != cycle || bridge.targetFrames() != expected)
            {
                std::cerr << "FAIL: jitter reserve growth/cap or compatibility target\n";
                return 1;
            }
        }
        bridge.prepare(48000, 48000, 128, adaptive);
        if (bridge.targetFrames() != initial || bridge.underruns != 0)
        {
            std::cerr << "FAIL: reconnect resets adaptive target and counters\n";
            return 1;
        }
    }
    int cases = 0;
    uint64_t totalUnderruns = 0, maximumUnderruns = 0;
    for (auto rates : {std::pair{48000., 48000.}, std::pair{44100., 48000.},
                       std::pair{48000., 44100.}})
        for (auto periods : {std::pair{128, 128}, std::pair{128, 480},
                             std::pair{480, 128}, std::pair{480, 480}})
            for (double drift : {-0.001, 0.001})
                for (double phase : {0.0, 0.49, 0.99})
                {
                    canto::ClockBridge bridge;
                    bridge.prepare(rates.first, rates.second, std::max(periods.first, periods.second),
                                   true, periods.first, periods.second);
                    const auto initialTarget = bridge.targetFrames();
                    const auto step = std::max(size_t(32), (size_t(periods.first) +
                                      size_t(std::ceil(periods.second * rates.first / rates.second))) / 4);
                    const double capturePeriod = periods.first / (rates.first * (1 + drift));
                    const double renderPeriod = periods.second / rates.second;
                    uint64_t captures = 0, renders = 0;
                    auto captureTime = [&]
                    {
                        return (double(captures) + phase + jitter * std::sin(double(captures) * 0.71)) *
                               capturePeriod;
                    };
                    auto renderTime = [&]
                    {
                        return (double(renders) + jitter * std::sin(double(renders) * 0.43)) * renderPeriod;
                    };
                    bool finite = true, continuous = true;
                    uint64_t observedUnderruns = 0, checkedFrames = 0;
                    double lastUnderrunTime = 0;
                    size_t maximumQueue = 0;
                    while (renderTime() < 20.0)
                    {
                        if (captureTime() <= renderTime())
                        {
                            for (int i = 0; i < periods.first; ++i)
                                bridge.push(0.1f);
                            ++captures;
                            maximumQueue = std::max(maximumQueue, bridge.buffered());
                        }
                        else
                        {
                            const double now = renderTime();
                            bridge.beginBlock();
                            for (int i = 0; i < periods.second; ++i)
                            {
                                const auto value = bridge.next();
                                finite = finite && std::isfinite(value);
                                if (bridge.underruns.load() != observedUnderruns)
                                {
                                    observedUnderruns = bridge.underruns.load();
                                    lastUnderrunTime = now;
                                }
                                // Check recovery after at most 100 ms, not just
                                // counters. A long silent restart cannot pass.
                                if (now > 1.0 && now > lastUnderrunTime + 0.1)
                                {
                                    continuous = continuous && std::abs(value - 0.1f) < 1.e-5f;
                                    ++checkedFrames;
                                }
                            }
                            ++renders;
                        }
                    }
                    if (!finite || !continuous || checkedFrames < uint64_t(rates.second * 18.5) ||
                        bridge.underruns > (jitter > 0 ? 2u : 0u) ||
                        bridge.overruns || bridge.resyncs ||
                        bridge.targetFrames() != initialTarget + step * std::min(uint64_t(2), observedUnderruns) ||
                        maximumQueue > bridge.targetFrames() + size_t(periods.first + periods.second) * 2)
                    {
                        std::cerr << "FAIL: rates " << rates.first << '/' << rates.second << " periods "
                                  << periods.first << '/' << periods.second << " drift " << drift
                                  << " phase " << phase << " under/over/resync " << bridge.underruns << '/'
                                  << bridge.overruns << '/' << bridge.resyncs << " max queue " << maximumQueue
                                  << " target " << bridge.targetFrames() << " continuous " << continuous << '\n';
                        return 1;
                    }
                    ++cases;
                    totalUnderruns += observedUnderruns;
                    maximumUnderruns = std::max(maximumUnderruns, observedUnderruns);
                }
    std::cout << "PASS: " << cases << " independent clock schedules, 20 simulated seconds each; jitter "
              << jitter << " periods; total/max per-case underruns " << totalUnderruns << '/' << maximumUnderruns
              << "; bounded recovery within 100 ms, at least 18.5 seconds of checked steady output. "
                 "128/480 capture/render periods, mixed rates, +/-1000 ppm, phase. "
                 "No hardware or RTT measurement.\n";
}
