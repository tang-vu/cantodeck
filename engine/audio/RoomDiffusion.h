#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace canto
{
// Four Schroeder allpass sections, only on the room return (never the dry bus).
// H(z) = (z^-N - g) / (1 - g*z^-N), g=0.5. Unlike an approximate allpass,
// each section has unit magnitude in its linear operating range.
class RoomDiffusion
{
    std::array<std::vector<float>, 4> delays;
    std::array<size_t, 4> positions{};
  public:
    void prepare(double rate)
    {
        constexpr int lengths[] = {149, 211, 263, 293};
        for (size_t i = 0; i < delays.size(); ++i)
            delays[i].assign(size_t(std::max(1., std::round(lengths[i] * rate / 48000.))), 0);
        positions = {};
    }
    float process(float input) noexcept
    {
        float value = std::isfinite(input) ? std::clamp(input, -16.f, 16.f) : 0.f;
        for (size_t i = 0; i < delays.size(); ++i)
        {
            if (delays[i].empty())
                return 0;
            auto& delayed = delays[i][positions[i]];
            const float output = delayed - 0.5f * value;
            delayed = std::clamp(value + 0.5f * output, -32.f, 32.f);
            positions[i] = (positions[i] + 1) % delays[i].size();
            value = output;
        }
        return std::clamp(value, -16.f, 16.f);
    }
};
}
