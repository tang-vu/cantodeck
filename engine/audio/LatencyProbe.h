#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace canto
{
struct LatencyResult
{
    bool valid = false;
    double milliseconds = 0, correlation = 0, secondPeak = 0;
    bool inverted = false;
    std::string reason;
};
// User-triggered acoustic/electrical loop measurement. Output and returned input
// are indexed by the output processing clock. No microphone samples leave RAM.
class LatencyProbe
{
  public:
    enum class State
    {
        idle,
        requested,
        capturing,
        ready,
        analysing,
        complete,
        cancelled
    };

  private:
    static constexpr size_t signalLength = 2048;
    std::array<float, signalLength> excitation{}, emitted{};
    std::vector<float> captured;
    double rate = 48000;
    size_t position = 0;
    std::atomic<State> phase{State::idle};
    std::atomic<bool> analysing{false};

  public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        captured.assign(size_t(rate * 0.75) + signalLength, 0);
        emitted = {};
        position = 0;
        uint32_t seed = 0x4815abcd;
        float filtered = 0;
        for (size_t i = 0; i < signalLength; ++i)
        {
            seed = 1664525u * seed + 1013904223u;
            const float noise = (seed & 0x80000000u) ? 1.f : -1.f;
            filtered += 0.28f * (noise - filtered);
            const float edge = std::min({1.f, float(i) / 32.f, float(signalLength - 1 - i) / 32.f});
            excitation[i] = filtered * edge * 0.02f;
        }
        phase = State::idle;
    }
    bool begin() noexcept
    {
        if (captured.empty() || analysing.load())
            return false;
        auto state = phase.load();
        if (state != State::idle && state != State::complete && state != State::cancelled)
            return false;
        return phase.compare_exchange_strong(state, State::requested);
    }
    void cancel() noexcept { phase = State::cancelled; }
    State state() const noexcept { return phase.load(); }
    bool busy() const noexcept
    {
        const auto s = state();
        return s == State::requested || s == State::capturing || s == State::ready || s == State::analysing;
    }
    float signal() noexcept
    {
        if (phase.load() == State::requested)
        {
            position = 0;
            auto expected = State::requested;
            phase.compare_exchange_strong(expected, State::capturing);
        }
        return phase.load() == State::capturing && position < signalLength ? excitation[position] : 0.f;
    }
    void feed(float returnedInput, float actualOutput) noexcept
    {
        if (phase.load() != State::capturing)
            return;
        if (position < signalLength)
            emitted[position] = std::isfinite(actualOutput) ? actualOutput : 0.f;
        captured[position] = std::isfinite(returnedInput) ? returnedInput : 0.f;
        if (++position == captured.size())
        {
            auto expected = State::capturing;
            phase.compare_exchange_strong(expected, State::ready);
        }
    }
    // Worker thread only. Returns an explicit invalid result for silence,
    // ambiguous returns or clipping; never substitutes a buffer-based estimate.
    LatencyResult analyse() { return analyse([] {}); }
    // Worker-only start notification also permits deterministic cancellation tests.
    template <typename OnStarted> LatencyResult analyse(OnStarted onStarted)
    {
        if (analysing.exchange(true))
            return {false, 0, 0, 0, false, "Analysis already running"};
        struct AnalysisGuard
        {
            std::atomic<bool>& active;
            ~AnalysisGuard() { active = false; }
        } guard{analysing};
        auto expected = State::ready;
        if (!phase.compare_exchange_strong(expected, State::analysing))
            return {false, 0, 0, 0, false, "No completed probe"};
        onStarted();
        LatencyResult result;
        auto finish = [&]()
        {
            auto active = State::analysing;
            if (!phase.compare_exchange_strong(active, State::complete))
                return LatencyResult{false, 0, 0, 0, false, "Measurement cancelled"};
            return result;
        };
        const double n = double(signalLength);
        double sumR = 0, squareR = 0;
        for (float x : emitted)
        {
            sumR += x;
            squareR += double(x) * x;
        }
        const double energyR = squareR - sumR * sumR / n;
        if (energyR < 1.e-10)
        {
            result.reason = "Output muted or probe too quiet";
            return finish();
        }
        std::vector<double> sum(captured.size() + 1), energy(captured.size() + 1),
            scores(captured.size() - signalLength + 1);
        size_t clipped = 0;
        for (size_t i = 0; i < captured.size(); ++i)
        {
            sum[i + 1] = sum[i] + captured[i];
            energy[i + 1] = energy[i] + double(captured[i]) * captured[i];
            if (std::abs(captured[i]) >= 0.995f)
                ++clipped;
        }
        size_t best = 0;
        double signedBest = 0;
        for (size_t lag = 0; lag < scores.size(); ++lag)
        {
            if (phase.load() != State::analysing)
                return finish();
            const double sumC = sum[lag + signalLength] - sum[lag],
                         energyC = energy[lag + signalLength] - energy[lag] - sumC * sumC / n;
            if (energyC < 1.e-12)
                continue;
            double dot = 0;
            for (size_t j = 0; j < signalLength; ++j)
                dot += double(emitted[j]) * captured[lag + j];
            const double score = (dot - sumR * sumC / n) / std::sqrt(energyR * energyC);
            scores[lag] = std::abs(score);
            if (scores[lag] > result.correlation)
            {
                result.correlation = scores[lag];
                best = lag;
                signedBest = score;
            }
        }
        for (size_t lag = 0; lag < scores.size(); ++lag)
            if (std::abs(double(lag) - double(best)) > rate * 0.002)
                result.secondPeak = std::max(result.secondPeak, scores[lag]);
        result.milliseconds = double(best) * 1000 / rate;
        result.inverted = signedBest < 0;
        result.valid = result.correlation >= 0.35 && result.secondPeak < result.correlation * 0.85 &&
                       clipped < signalLength / 20;
        if (!result.valid)
            result.reason = clipped >= signalLength / 20 ? "Input clipped"
                            : result.correlation < 0.35  ? "No reliable acoustic/electrical return"
                                                         : "Ambiguous returns or competing audio";
        return finish();
    }
};
} // namespace canto
