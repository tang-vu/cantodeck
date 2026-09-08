#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include "audio/ParametricEQ.h"

namespace canto
{
static_assert(std::atomic<float>::is_always_lock_free && std::atomic<double>::is_always_lock_free &&
                  std::atomic<uint64_t>::is_always_lock_free,
              "Audio parameters require lock-free atomics");
inline float clean(float x)
{
    return std::isfinite(x) ? std::clamp(x, -16.f, 16.f) : 0.f;
}
template <class T, size_t N> class Ring
{
    std::array<T, N> data{};
    alignas(64) std::atomic<uint64_t> read{0};
    alignas(64) std::atomic<uint64_t> write{0};

  public:
    bool push(const T& value) noexcept
    {
        auto w = write.load(std::memory_order_relaxed);
        if (w - read.load(std::memory_order_acquire) >= N)
            return false;
        data[w % N] = value;
        write.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& value) noexcept
    {
        auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire))
            return false;
        value = data[r % N];
        read.store(r + 1, std::memory_order_release);
        return true;
    }
    size_t size() const noexcept
    {
        return size_t(write.load(std::memory_order_acquire) - read.load(std::memory_order_acquire));
    }
    void reset() noexcept
    {
        read = 0;
        write = 0;
    } // Only while both endpoints stopped.
};
struct Parameters
{
    std::array<EqBandParameters, 3> eqBands;
    std::atomic<bool> transparent{false};
    std::atomic<float> inputBoostDb{0.f};
    std::atomic<float> mic{0.7f}, music{0.65f}, master{0.5f}, echo{0.18f}, feedback{0.25f}, delayMs{180.f},
        reverb{0.12f}, tone{0.f}, threshold{-18.f};
    std::atomic<bool> monitor{false}, mute{false}, gate{true}, compressor{true}, eq{true}, effects{true},
        musicMute{false};
    std::atomic<int> channel{0}; // 0=first, 1=second, 2=average
};
class VocalDSP
{
    ParametricEQ parametric;
    std::vector<float> delay;
    std::array<std::vector<float>, 4> room;
    std::array<size_t, 4> roomPos{};
    std::array<float, 4> damping{};
    size_t pos = 0;
    double rate = 48000;
    float x1 = 0, y1 = 0, low = 0, envelope = 0, gain = 0, echoGain = 0, roomGain = 0, toneGain = 0,
          compGain = 1;
    double delaySamples = 0;
    float inputGain = 1.f, inputGainTarget = 1.f, cachedBoostDb = 0.f;
    float cachedThresholdDb = -18.f, thresholdLinear = 0.12589254f;
    float highPassPole = 0, toneCoefficient = 0, transparentMix = 0, bypassStep = 0;

  public:
    void prepare(double sr)
    {
        rate = sr;
        parametric.prepare(sr);
        delay.assign(size_t(sr * 0.8) + 1, 0);
        const double times[] = {0.0297, 0.0371, 0.0411, 0.0437};
        for (size_t i = 0; i < 4; ++i)
            room[i].assign(size_t(sr * times[i]) + 1, 0);
        pos = 0;
        roomPos = {};
        damping = {};
        delaySamples = 0;
        inputGain = 1;
        inputGainTarget = 1;
        cachedBoostDb = 0;
        cachedThresholdDb = -18;
        thresholdLinear = std::pow(10.f, cachedThresholdDb / 20.f);
        highPassPole = float(std::exp(-2 * 3.141592653589793 * 75 / rate));
        toneCoefficient = float(1 - std::exp(-2 * 3.141592653589793 * 1800 / rate));
        transparentMix = 0;
        bypassStep = float(1 / (rate * 0.005));
        x1 = y1 = low = envelope = gain = echoGain = roomGain = toneGain = 0;
        compGain = 1;
    }
    float process(float input, const Parameters& p) noexcept
    {
        const float requestedBoost = p.inputBoostDb.load();
        const float boostDb = std::isfinite(requestedBoost) ? std::clamp(requestedBoost, 0.f, 24.f) : 0.f;
        if (boostDb != cachedBoostDb)
        {
            cachedBoostDb = boostDb;
            inputGainTarget = std::pow(10.f, boostDb / 20.f);
        }
        inputGain += 0.001f * (inputGainTarget - inputGain);
        const float x = clean(clean(input) * inputGain);
        gain += 0.001f * (p.mic.load() - gain);
        const float hp = x - x1 + highPassPole * y1;
        x1 = x;
        y1 = hp;
        envelope += (std::abs(hp) > envelope ? 0.01f : 0.0002f) * (std::abs(hp) - envelope);
        float v = hp;
        if (p.gate.load())
            v *= std::clamp(envelope / 0.003f, 0.15f, 1.f);
        low += toneCoefficient * (v - low);
        toneGain += 0.001f * ((p.eq.load() ? p.tone.load() : 0.f) - toneGain);
        v += (v - low) * toneGain;
        v = parametric.process(v, p.eqBands, p.eq.load());
        float targetComp = 1;
        if (p.compressor.load())
        {
            const float requestedThreshold = p.threshold.load();
            const float thresholdDb = std::isfinite(requestedThreshold)
                                          ? std::clamp(requestedThreshold, -60.f, 0.f) : -18.f;
            if (thresholdDb != cachedThresholdDb)
            {
                cachedThresholdDb = thresholdDb;
                thresholdLinear = std::pow(10.f, thresholdDb / 20.f);
            }
            const float t = thresholdLinear;
            if (envelope > t)
                targetComp = std::pow(t / envelope, 0.65f);
        }
        compGain += (targetComp < compGain ? 0.01f : 0.0005f) * (targetComp - compGain);
        v *= compGain * gain;
        echoGain += 0.001f * ((p.effects.load() ? p.echo.load() : 0.f) - echoGain);
        roomGain += 0.001f * ((p.effects.load() ? p.reverb.load() : 0.f) - roomGain);
        const double wantedDelay = std::clamp(p.delayMs.load(), 30.f, 700.f) * rate / 1000;
        if (delaySamples == 0)
            delaySamples = wantedDelay;
        delaySamples += 0.0002 * (wantedDelay - delaySamples);
        auto d = std::clamp(size_t(delaySamples), size_t(1), delay.size() - 2);
        const float frac = float(delaySamples - double(d));
        const float near = delay[(pos + delay.size() - d) % delay.size()],
                    far = delay[(pos + delay.size() - d - 1) % delay.size()];
        const float tail = near + (far - near) * frac;
        delay[pos] = clean(v + tail * std::clamp(p.feedback.load(), 0.f, 0.65f));
        pos = (pos + 1) % delay.size();
        float reverberation = 0;
        for (size_t j = 0; j < 4; ++j)
        {
            float r = room[j][roomPos[j]];
            damping[j] += 0.35f * (r - damping[j]);
            room[j][roomPos[j]] = clean(v + damping[j] * 0.72f);
            roomPos[j] = (roomPos[j] + 1) % room[j].size();
            reverberation += r * 0.25f;
        }
        // Linear (not equal-power) crossfade: both paths contain correlated voice.
        // Keep their states advancing, so bypass changes do not revive frozen tails.
        const float requestedMix = p.transparent.load() ? 1.f : 0.f;
        transparentMix += std::clamp(requestedMix - transparentMix, -bypassStep, bypassStep);
        const float processed = clean(v + tail * echoGain + reverberation * roomGain);
        return clean(processed * (1.f - transparentMix) + clean(x * gain) * transparentMix);
    }
};
// Stereo-linked, zero-lookahead sample-peak limiter. Immediate gain reduction,
// 80 ms release; unlike clipping, subsequent samples retain their waveform.
class SamplePeakLimiter
{
    float gain = 1.f, release = 0.00026f;

  public:
    void prepare(double sr)
    {
        gain = 1.f;
        release = float(1 - std::exp(-1 / (0.080 * sr)));
    }
    void process(float& left, float& right) noexcept
    {
        left = clean(left);
        right = clean(right);
        const float peak = std::max(std::abs(left), std::abs(right));
        const float wanted = peak > 0.95f ? 0.95f / peak : 1.f;
        gain = std::min(wanted, gain + release * (1 - gain));
        left *= gain;
        right *= gain;
    }
    float currentGain() const noexcept { return gain; }
};
// Output-driven adaptive rate conversion. A phase-table windowed-sinc filter
// avoids the high-frequency droop of the former two-point interpolation.
class ClockBridge
{
    Ring<float, 32768> fifo;
    double phase = 0, nominal = 1, correction = 1;
    static constexpr size_t taps = 32, phases = 1024;
    std::array<float, taps> history{};
    std::vector<std::array<float, taps>> kernel;
    bool primed = false;
    size_t target = 1024, recoveryThreshold = 4096;
    int rampFrames = 240, rampRemaining = 0;
    float lastOutput = 0, rampFrom = 0;
    void transition() noexcept
    {
        rampFrom = lastOutput;
        rampRemaining = rampFrames;
    }
    float emit(float value) noexcept
    {
        if (rampRemaining > 0)
        {
            const float blend = float(--rampRemaining) / float(rampFrames);
            value = value * (1.f - blend) + rampFrom * blend;
        }
        lastOutput = clean(value);
        return lastOutput;
    }

  public:
    std::atomic<uint64_t> underruns{0}, overruns{0}, resyncs{0}, discardedFrames{0};
    std::atomic<double> ratio{1};
    void prepare(double inputRate, double outputRate, int block, bool lowLatency = false, int inputBlock = 0,
                 int outputBlock = 0)
    {
        fifo.reset();
        nominal = inputRate / outputRate;
        phase = 0;
        correction = 1;
        primed = false;
        history = {};
        lastOutput = rampFrom = 0;
        rampFrames = std::max(1, int(outputRate * 0.005));
        rampRemaining = 0;
        kernel.resize(phases + 1);
        constexpr double pi = 3.14159265358979323846;
        const double cutoff = nominal > 1 ? 0.96 / nominal : 1.0;
        for (size_t p = 0; p <= phases; ++p)
        {
            double sum = 0;
            for (size_t k = 0; k < taps; ++k)
            {
                const double x = double(k) - 15.0 - double(p) / phases;
                const double z = pi * x * cutoff;
                const double sinc = std::abs(z) < 1.e-12 ? 1.0 : std::sin(z) / z;
                const double window = 0.42 + 0.5 * std::cos(pi * x / 16) + 0.08 * std::cos(2 * pi * x / 16);
                kernel[p][k] = float(cutoff * sinc * window);
                sum += kernel[p][k];
            }
            for (auto& value : kernel[p])
                value = float(value / sum);
        }
        const auto captureQuantum = inputBlock > 0 ? inputBlock : block;
        const auto renderQuantum = outputBlock > 0 ? outputBlock : block;
        const auto lowTarget = size_t(std::ceil(renderQuantum * nominal)) + size_t(captureQuantum) + 16;
        target = std::clamp(lowLatency ? lowTarget : size_t(block * 3), size_t(lowLatency ? 128 : 512),
                            size_t(8192));
        recoveryThreshold = std::min(size_t(32767), target +
            std::max(size_t(1024), size_t(std::max(captureQuantum, renderQuantum)) * 4));
        underruns = 0;
        overruns = 0;
        resyncs = 0;
        discardedFrames = 0;
    }
    void push(float v) noexcept
    {
        if (!fifo.push(clean(v)))
            ++overruns;
    }
    void beginBlock() noexcept
    {
        // A stalled output must not replay up to an entire FIFO of old speech.
        // Only the consumer discards; never reset shared indices while capture runs.
        const auto queued = fifo.size();
        if (queued > recoveryThreshold)
        {
            float unused = 0;
            size_t dropped = 0;
            const auto excess = std::min(queued - target, size_t(32768));
            while (dropped < excess && fifo.pop(unused))
                ++dropped;
            discardedFrames.fetch_add(dropped);
            ++resyncs;
            primed = false;
            history = {};
            phase = 0;
            transition();
        }
        // A direct proportional occupancy controller is monotonic under constant
        // drift. The former extra multi-second low-pass produced underdamped
        // queue oscillations and forced larger prebuffers.
        const double error = (double(fifo.size()) - double(target)) / double(target);
        correction = std::clamp(1 + error * 0.01, 0.98, 1.02);
        ratio.store(nominal * correction);
    }
    float next() noexcept
    {
        if (!primed)
        {
            if (fifo.size() < target)
                return emit(0);
            history = {};
            for (size_t i = 15; i < taps; ++i)
                fifo.pop(history[i]);
            primed = true;
            transition();
        }
        const auto p = std::min(size_t(phase * phases), phases);
        float result = 0;
        for (size_t k = 0; k < taps; ++k)
            result += history[k] * kernel[p][k];
        phase += nominal * correction;
        while (phase >= 1)
        {
            phase -= 1;
            for (size_t k = 1; k < taps; ++k)
                history[k - 1] = history[k];
            if (!fifo.pop(history.back()))
            {
                ++underruns;
                primed = false;
                history = {};
                phase = 0;
                transition();
                return emit(0);
            }
        }
        return emit(result);
    }
    size_t buffered() const { return fifo.size(); }
    size_t targetFrames() const { return target; }
    static constexpr int lookaheadFrames() { return 16; }
};
} // namespace canto
