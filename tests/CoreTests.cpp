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
        for (double sr : {44100., 48000.})
        {
            canto::LatencyProbe probe;
            probe.prepare(sr);
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
                check(bridge.overruns == 0 && bridge.underruns == 0, "clock drift stability");
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
            check(bridge.overruns == 0 && bridge.underruns == 0, "mixed sample rate stability");
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
            check(bridge.overruns == 0 && bridge.underruns == 0,
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
                     "source loss\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
