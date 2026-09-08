#include "engine/Core.h"
#include "engine/audio/LatencyProbe.h"
#include <iostream>
#include <stdexcept>
#include <limits>
#include <fstream>
#include <string>
void check(bool v, const char* message)
{
    if (!v)
        throw std::runtime_error(message);
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string(argv[1]) == "--fixture")
        {
            std::ofstream file(argv[2], std::ios::binary);
            auto word = [&](uint32_t x, int bytes)
            {
                for (int i = 0; i < bytes; i++)
                    file.put(char((x >> (i * 8)) & 255));
            };
            const int n = 48000 * 3;
            file.write("RIFF", 4);
            word(36 + n * 2, 4);
            file.write("WAVEfmt ", 8);
            word(16, 4);
            word(1, 2);
            word(1, 2);
            word(48000, 4);
            word(96000, 4);
            word(2, 2);
            word(16, 2);
            file.write("data", 4);
            word(n * 2, 4);
            for (int i = 0; i < n; i++)
            {
                double t = i / 48000.;
                float v = i < 48000    ? float(0.25 * std::sin(2 * 3.141592653589793 * 440 * t))
                          : i == 72000 ? 0.8f
                                       : 0.f;
                word(uint16_t(int16_t(v * 32767)), 2);
            }
            return file ? 0 : 1;
        }
        canto::Ring<int, 4> q;
        for (int i = 0; i < 4; i++)
            check(q.push(i), "ring capacity");
        check(!q.push(4), "overflow bounded");
        for (int i = 0; i < 4; i++)
        {
            int x = -1;
            check(q.pop(x) && x == i, "FIFO ordering");
        }
        int x;
        check(!q.pop(x), "underflow bounded");
        {
            canto::LatencyProbe unprepared;
            check(!unprepared.begin(), "unprepared measurement must not start");
            check(unprepared.signal() == 0, "unprepared measurement is silent");
        }
        for (double sr : {44100., 48000.})
        {
            auto eqResponse = [&](float frequency, float gain, float q, int activeBands, double testFrequency)
            {
                canto::ParametricEQ filter;
                filter.prepare(sr);
                std::array<canto::EqBandParameters, 3> bands;
                for (int i = 0; i < 3; ++i)
                {
                    bands[size_t(i)].frequency = frequency;
                    bands[size_t(i)].gainDb = i < activeBands ? gain : 0;
                    bands[size_t(i)].q = q;
                }
                double inEnergy = 0, outEnergy = 0;
                for (int i = 0; i < int(sr); ++i)
                {
                    const float input = float(0.01 * std::sin(2 * 3.141592653589793 * testFrequency * i / sr));
                    const float output = filter.process(input, bands, true);
                    check(std::isfinite(output), "parametric EQ finite response");
                    if (i > sr / 2)
                    {
                        inEnergy += double(input) * input;
                        outEnergy += double(output) * output;
                    }
                }
                return 10 * std::log10(outEnergy / inEnergy);
            };
            for (float center : {120.f, 1200.f, 6500.f})
                for (float gain : {-12.f, -6.f, 0.f, 6.f, 12.f})
                    check(std::abs(eqResponse(center, gain, 0.707f, 1, center) - gain) < 0.08,
                          "parametric EQ measured center gain matches requested dB");
            check(std::abs(eqResponse(1000, 6, 1, 3, 1000) - 18) < 0.08,
                  "three parametric bands are serial and independently active");
            check(eqResponse(1000, 12, 8, 1, 1400) < eqResponse(1000, 12, 0.5f, 1, 1400) - 5,
                  "parametric Q controls bandwidth");
            {
                canto::ParametricEQ filter;
                filter.prepare(sr);
                std::array<canto::EqBandParameters, 3> bands;
                for (auto& band : bands)
                    band.gainDb = 12;
                for (int i = 0; i < int(sr); ++i)
                    filter.process(0.01f, bands, true);
                for (int i = 0; i < int(sr); ++i)
                {
                    const float input = float(0.01 * std::sin(i * 0.11));
                    const float output = filter.process(input, bands, false);
                    if (i > sr / 2)
                        check(std::abs(input - output) < 1.e-6f, "parametric bypass settles at unity");
                }
                for (int i = 0; i < int(sr * 2); ++i)
                {
                    auto& band = bands[size_t(i % 3)];
                    band.frequency = i % 2 ? 40.f : 16000.f;
                    band.q = i % 2 ? 0.2f : 8.f;
                    band.gainDb = i % 2 ? -12.f : 12.f;
                    if (i % 101 == 0)
                        band.frequency = std::numeric_limits<float>::quiet_NaN();
                    const float output = filter.process(i % 2 ? -0.01f : 0.01f, bands, true);
                    check(std::isfinite(output) && std::abs(output) < 1,
                          "parametric automation stays finite and bounded for weak input");
                }
            }
            canto::LatencyProbe probe;
            probe.prepare(sr);
            check(probe.begin(), "prepared measurement starts");
            check(!probe.begin(), "measurement cannot be started twice");
            probe.cancel();
            check(probe.signal() == 0 && !probe.busy(), "cancel before capture remains silent");
            check(!probe.analyse().valid, "cancelled measurement has no result");
            check(probe.begin(), "cancelled measurement can restart");
            probe.signal();
            probe.feed(0, 0);
            probe.cancel();
            probe.feed(1, 1);
            check(probe.signal() == 0 && probe.state() == canto::LatencyProbe::State::cancelled,
                  "cancel during capture is not republished as ready");
            probe.begin();
            const size_t lag = size_t(sr * 0.023);
            std::vector<float> delayed(lag + 1);
            size_t index = 0;
            while (probe.state() != canto::LatencyProbe::State::ready)
            {
                float generated = probe.signal();
                float returned = delayed[index];
                delayed[index] = generated * 0.4f;
                index = (index + 1) % delayed.size();
                probe.feed(returned, generated);
            }
            auto measurement = probe.analyse();
            check(measurement.valid &&
                      std::abs(measurement.milliseconds - double(lag + 1) * 1000 / sr) < 0.01,
                  "latency detector recovers known delayed response");
            probe.begin();
            while (probe.state() != canto::LatencyProbe::State::ready)
            {
                float generated = probe.signal();
                probe.feed(0, generated);
            }
            check(!probe.analyse().valid, "latency detector must reject silence");
            // Every run is bounded, including failures to publish a completed capture.
            auto measure = [&](auto returned, bool muteOutput = false)
            {
                check(probe.begin(), "measurement restart");
                std::vector<float> history(size_t(sr) + 4096, 0.f);
                for (size_t i = 0; i < history.size() && probe.busy(); ++i)
                {
                    const float generated = probe.signal();
                    history[i] = muteOutput ? 0.f : generated;
                    probe.feed(returned(history, i), history[i]);
                    if (probe.state() == canto::LatencyProbe::State::ready)
                        break;
                }
                check(probe.state() == canto::LatencyProbe::State::ready, "measurement completes in bound");
                return probe.analyse();
            };
            auto inverted = measure([&](const auto& history, size_t i)
                                    { return i >= lag ? -0.3f * history[i - lag] + 0.01f : 0.01f; });
            check(inverted.valid && inverted.inverted &&
                      std::abs(inverted.milliseconds - double(lag) * 1000 / sr) < 0.01,
                  "measurement accepts inverted return with DC offset");
            auto ambiguous = measure([&](const auto& history, size_t i)
                                     {
                                         const size_t second = lag + size_t(sr * 0.12);
                                         return (i >= lag ? history[i - lag] : 0.f) +
                                                (i >= second ? history[i - second] : 0.f);
                                     });
            check(!ambiguous.valid && ambiguous.reason == "Ambiguous returns or competing audio",
                  "measurement rejects two equally strong separated paths");
            auto clipped = measure([](const auto&, size_t) { return 1.f; });
            check(!clipped.valid && clipped.reason == "Input clipped", "measurement rejects clipping");
            auto muted = measure([](const auto&, size_t) { return 0.f; }, true);
            check(!muted.valid && muted.reason == "Output muted or probe too quiet",
                  "measurement rejects muted output rather than reporting zero latency");
            canto::SamplePeakLimiter limiter;
            limiter.prepare(sr);
            for (int i = 0; i < int(sr); ++i)
            {
                float left = 2.f * float(std::sin(i * 2 * 3.141592653589793 * 1000 / sr)),
                      right = left * 0.5f;
                limiter.process(left, right);
                check(std::abs(left) <= 0.950001f && std::abs(right - left * 0.5f) < 1.e-6,
                      "linked limiter ceiling and stereo image");
            }
            float l = 0.1f, r = 0.2f;
            for (int i = 0; i < int(sr); ++i)
            {
                l = 0.1f;
                r = 0.2f;
                limiter.process(l, r);
            }
            check(std::abs(r - 0.2f) < 0.0001, "limiter release recovers unity");
            canto::Parameters transparent;
            transparent.transparent = true;
            transparent.mic = 1;
            canto::VocalDSP direct;
            direct.prepare(sr);
            for (int i = 0; i < int(sr); ++i)
            {
                float x = 0.1f * float(std::sin(i * 2 * 3.141592653589793 * 73 / sr));
                float y = direct.process(x, transparent);
                if (i > sr / 2)
                    check(std::abs(x - y) < 0.00001,
                          "transparent path preserves dry waveform without sample delay");
            }
            canto::Parameters p;
            {
                canto::Parameters flatParameters, boostedParameters;
                for (auto* parameters : {&flatParameters, &boostedParameters})
                {
                    parameters->gate = false;
                    parameters->compressor = false;
                    parameters->effects = false;
                    parameters->mic = 1;
                }
                boostedParameters.eqBands[0].frequency = 1000;
                boostedParameters.eqBands[0].gainDb = 6;
                canto::VocalDSP flatVoice, boostedVoice;
                flatVoice.prepare(sr);
                boostedVoice.prepare(sr);
                double flatEnergy = 0, boostedEnergy = 0;
                for (int i = 0; i < int(sr); ++i)
                {
                    const float input = float(0.01 * std::sin(2 * 3.141592653589793 * 1000 * i / sr));
                    const float flat = flatVoice.process(input, flatParameters);
                    const float boosted = boostedVoice.process(input, boostedParameters);
                    if (i > sr / 2)
                    {
                        flatEnergy += double(flat) * flat;
                        boostedEnergy += double(boosted) * boosted;
                    }
                }
                check(std::abs(10 * std::log10(boostedEnergy / flatEnergy) - 6) < 0.08,
                      "parametric EQ is connected to the actual vocal DSP chain");
            }
            {
                // DC separates the high-passed and transparent paths, exposing
                // an abrupt bypass switch without waveform-slope ambiguity.
                canto::Parameters switching;
                switching.mic = 1;
                switching.effects = false;
                switching.gate = false;
                switching.compressor = false;
                canto::VocalDSP switched;
                switched.prepare(sr);
                float previous = 0;
                for (int i = 0; i < int(sr); ++i)
                    previous = switched.process(0.25f, switching);
                for (bool transparentMode : {true, false, true, false})
                {
                    switching.transparent = transparentMode;
                    for (int i = 0; i < int(sr * 0.01); ++i)
                    {
                        const float value = switched.process(0.25f, switching);
                        check(std::abs(value - previous) < 0.002f, "vocal bypass transition is ramped");
                        previous = value;
                    }
                    check(std::abs(previous - (transparentMode ? 0.25f : 0.f)) < 0.00001f,
                          "vocal bypass reaches exact selected path");
                }
                switching.inputBoostDb = std::numeric_limits<float>::quiet_NaN();
                switching.threshold = std::numeric_limits<float>::infinity();
                switching.compressor = true;
                for (int i = 0; i < 1000; ++i)
                    check(std::isfinite(switched.process(0.1f, switching)),
                          "invalid gain conversion parameters cannot poison DSP state");
            }
            canto::VocalDSP dsp;
            dsp.prepare(sr);
            p.feedback = 2;
            p.echo = 0.7f;
            p.reverb = 0.5f;
            for (int i = 0; i < int(sr * 5); i++)
            {
                float y = dsp.process(i == 0 ? 1.f : 0.f, p);
                check(std::isfinite(y) && std::abs(y) <= 16, "impulse stability");
            }
            check(std::isfinite(dsp.process(std::numeric_limits<float>::quiet_NaN(), p)), "NaN protection");
            dsp.prepare(sr);
            p.mic = 0;
            p.echo = 0;
            p.reverb = 0;
            for (int i = 0; i < 10000; i++)
                check(dsp.process(1.f, p) == 0, "fader silence");
            canto::VocalDSP dry, echo;
            dry.prepare(sr);
            echo.prepare(sr);
            canto::Parameters pd, pe;
            pd.gate = false;
            pe.gate = false;
            pd.compressor = false;
            pe.compressor = false;
            pd.reverb = 0;
            pe.reverb = 0;
            pd.echo = 0;
            pe.echo = 0.5f;
            pe.delayMs = 100;
            int onset = 4096, delay = int(sr * 0.1);
            float atDelay = 0;
            for (int i = 0; i < onset + delay + 50; i++)
            {
                float impulse = i == onset ? 1.f : 0.f;
                float delta = echo.process(impulse, pe) - dry.process(impulse, pd);
                if (i < onset + delay)
                    check(std::abs(delta) < 1.e-6f, "echo must not arrive early");
                if (i == onset + delay)
                    atDelay = std::abs(delta);
            }
            check(atDelay > 0.1f, "echo impulse delay timing");
            dry.prepare(sr);
            echo.prepare(sr);
            pe.echo = 0;
            pe.compressor = true;
            pe.threshold = -24;
            double a = 0, b = 0;
            for (int i = 0; i < int(sr); i++)
            {
                float v = 0.5f * float(std::sin(i * 2 * 3.141592653589793 * 440 / sr));
                float d = dry.process(v, pd), c = echo.process(v, pe);
                if (i > sr / 2)
                {
                    a += d * d;
                    b += c * c;
                }
            }
            check(b < a * 0.5, "compressor attenuates sustained loud input");
            dry.prepare(sr);
            echo.prepare(sr);
            pe.compressor = false;
            pe.inputBoostDb = 12;
            a = b = 0;
            for (int i = 0; i < int(sr); i++)
            {
                float v = 0.001f * float(std::sin(i * 2 * 3.141592653589793 * 440 / sr));
                float d = dry.process(v, pd), c = echo.process(v, pe);
                if (i > sr / 2)
                {
                    a += d * d;
                    b += c * c;
                }
            }
            const double boostRatio = std::sqrt(b / a);
            check(boostRatio > 3.95 && boostRatio < 4.02, "weak mic +12 dB gain regression");
        }
        {
            canto::ClockBridge bridge;
            bridge.prepare(48000, 48000, 128, true);
            for (int i = 0; i < 32768 + 100; ++i)
                bridge.push(0.2f);
            check(bridge.overruns == 100, "capture overflow is counted");
            bridge.beginBlock();
            check(bridge.buffered() == bridge.targetFrames() && bridge.resyncs == 1 &&
                      bridge.discardedFrames == 32768 - bridge.targetFrames(),
                  "stalled output discards stale backlog to the live target");
            float previous = 0;
            for (int block = 0; block < 20; ++block)
            {
                if (block > 0)
                    for (int i = 0; i < 128; ++i)
                        bridge.push(0.2f);
                bridge.beginBlock();
                for (int i = 0; i < 128; ++i)
                {
                    float value = bridge.next();
                    check(std::abs(value - previous) < 0.01f, "recovery/startup ramps avoid a DC step");
                    previous = value;
                }
            }
            check(std::abs(previous - 0.2f) < 0.00001f, "recovery reaches live input");
            for (int block = 0; block < 20; ++block)
            {
                bridge.beginBlock();
                for (int i = 0; i < 128; ++i)
                {
                    float value = bridge.next();
                    check(std::abs(value - previous) < 0.01f, "source loss fades rather than stepping");
                    previous = value;
                }
            }
            check(previous == 0 && bridge.underruns > 0, "source loss settles at silence");
            for (int block = 0; block < 20; ++block)
            {
                for (int i = 0; i < 128; ++i)
                    bridge.push(-0.3f);
                bridge.beginBlock();
                for (int i = 0; i < 128; ++i)
                {
                    const float value = bridge.next();
                    check(std::abs(value - previous) < 0.01f, "resumed source fades in without stale history");
                    previous = value;
                }
            }
            check(std::abs(previous + 0.3f) < 0.00001f, "resumed source reaches new input");
        }
        for (bool lowLatency : {false, true})
            for (double drift : {-0.001, 0.001})
            {
                canto::ClockBridge bridge;
                bridge.prepare(48000, 48000, 256, lowLatency);
                double capture = 0;
                for (int block = 0; block < 20000; block++)
                {
                    capture += 256 * (1 + drift);
                    while (capture >= 1)
                    {
                        bridge.push(0.2f);
                        capture -= 1;
                    }
                    bridge.beginBlock();
                    for (int i = 0; i < 256; i++)
                        check(std::isfinite(bridge.next()), "resampler finite");
                }
                check(bridge.overruns == 0 && bridge.underruns == 0 && bridge.resyncs == 0,
                      "clock drift stability without dropping audio");
                check(bridge.buffered() < 2000, "clock queue bounded");
                for (int b = 0; b < 100; b++)
                {
                    bridge.beginBlock();
                    for (int i = 0; i < 256; i++)
                        bridge.next();
                }
                check(bridge.underruns > 0, "source loss reported");
            }
        for (auto rates : {std::pair{44100., 48000.}, std::pair{48000., 44100.}})
        {
            canto::ClockBridge bridge;
            bridge.prepare(rates.first, rates.second, 512);
            double capture = 0;
            for (int block = 0; block < 10000; block++)
            {
                int n = block % 3 == 0 ? 127 : block % 3 == 1 ? 512 : 64;
                capture += n * rates.first / rates.second;
                while (capture >= 1)
                {
                    bridge.push(0.2f);
                    capture -= 1;
                }
                bridge.beginBlock();
                for (int i = 0; i < n; i++)
                {
                    float v = bridge.next();
                    if (block > 100)
                        check(std::abs(v - 0.2f) < 1.e-5f, "mixed-rate resampling and arbitrary blocks");
                }
            }
            check(bridge.overruns == 0 && bridge.underruns == 0 && bridge.resyncs == 0,
                  "mixed sample rate stability without dropping audio");
        }
        for (double drift : {-0.001, 0.001})
        {
            canto::ClockBridge bridge;
            bridge.prepare(48000, 48000, 480, true, 128, 480);
            double produced = 0;
            for (int block = 0; block < 20000; block++)
            {
                produced += 480 * (1 + drift);
                while (produced >= 128)
                {
                    for (int i = 0; i < 128; i++)
                        bridge.push(0.1f);
                    produced -= 128;
                }
                bridge.beginBlock();
                for (int i = 0; i < 480; i++)
                    check(std::isfinite(bridge.next()), "asymmetric low-latency output finite");
            }
            check(bridge.overruns == 0 && bridge.underruns == 0 && bridge.resyncs == 0,
                  "128-in/480-out low-latency drift stability");
        }
        {
            canto::ClockBridge bridge;
            bridge.prepare(48000, 48000, 128, true);
            double pending = 0, energy = 0;
            uint64_t inputIndex = 0, outputCount = 0;
            for (int block = 0; block < 4000; ++block)
            {
                pending += 128 * 1.0005;
                while (pending >= 1)
                {
                    bridge.push(
                        float(0.2 * std::sin(2 * 3.141592653589793 * 10000 * double(inputIndex++) / 48024)));
                    pending -= 1;
                }
                bridge.beginBlock();
                for (int k = 0; k < 128; ++k)
                {
                    const float y = bridge.next();
                    if (block > 1000)
                    {
                        energy += double(y) * y;
                        ++outputCount;
                    }
                }
            }
            const double amplitude = std::sqrt(2 * energy / outputCount);
            check(amplitude > 0.196 && amplitude < 0.204,
                  "resampler preserves 10 kHz vocal harmonics under fractional drift");
        }
        std::cout << "PASS: FIFO, DSP impulse/finite/mute, delay timing, compressor, +/-1000 ppm drift, "
                     "source loss, sinc response, transparent voice, linked limiter, latency measurement "
                     "and invalid-return/cancellation handling\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
