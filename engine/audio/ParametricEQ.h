#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace canto
{
struct EqBandParameters
{
    std::atomic<float> frequency{1000}, gainDb{0}, q{0.707f};
};
// Three bell filters. Coefficients follow the peaking-EQ equations in
// https://www.w3.org/TR/audio-eq-cookbook/ (RBJ/W3C).
// Direct-form I state and parameter smoothing; fixed storage, no graph swaps.
class ParametricEQ
{
    struct Band
    {
        double frequency = 1000, gainDb = 0, q = 0.707;
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };
    std::array<Band, 3> bands{};
    double rate = 48000, smoothing = 0;
    int untilUpdate = 0;
    static double bounded(float value, double fallback, double low, double high) noexcept
    {
        return std::isfinite(value) ? std::clamp(double(value), low, high) : fallback;
    }

  public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        bands = {};
        smoothing = 1 - std::exp(-64 / (rate * 0.020));
        untilUpdate = 0;
    }
    float process(float input, const std::array<EqBandParameters, 3>& parameters, bool enabled) noexcept
    {
        if (untilUpdate-- == 0)
        {
            untilUpdate = 63;
            for (size_t i = 0; i < bands.size(); ++i)
            {
                auto& band = bands[i];
                const auto& p = parameters[i];
                const double frequency = bounded(p.frequency.load(), 1000, 40, std::min(16000., rate * 0.45));
                const double gain = enabled ? bounded(p.gainDb.load(), 0, -12, 12) : 0;
                const double q = bounded(p.q.load(), 0.707, 0.2, 8);
                band.frequency += smoothing * (frequency - band.frequency);
                band.gainDb += smoothing * (gain - band.gainDb);
                band.q += smoothing * (q - band.q);
                if (std::abs(band.gainDb - gain) < 1.e-6)
                    band.gainDb = gain;
                const double omega = 2 * 3.14159265358979323846 * band.frequency / rate;
                const double amplitude = std::pow(10., band.gainDb / 40);
                const double alpha = std::sin(omega) / (2 * band.q);
                const double a0 = 1 + alpha / amplitude;
                band.b0 = (1 + alpha * amplitude) / a0;
                band.b1 = -2 * std::cos(omega) / a0;
                band.b2 = (1 - alpha * amplitude) / a0;
                band.a1 = band.b1;
                band.a2 = (1 - alpha / amplitude) / a0;
            }
        }
        double value = std::isfinite(input) ? std::clamp(double(input), -16., 16.) : 0;
        for (auto& band : bands)
        {
            double output = band.b0 * value + band.b1 * band.x1 + band.b2 * band.x2 -
                            band.a1 * band.y1 - band.a2 * band.y2;
            if (!std::isfinite(output))
            {
                band.x1 = band.x2 = band.y1 = band.y2 = 0;
                output = 0;
            }
            band.x2 = band.x1;
            band.x1 = value;
            band.y2 = band.y1;
            band.y1 = std::clamp(output, -64., 64.);
            value = band.y1;
        }
        return float(std::clamp(value, -16., 16.));
    }
};
}
