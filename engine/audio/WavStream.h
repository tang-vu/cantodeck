#pragma once
#include "engine/Core.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <thread>
#include <chrono>
#include <stdexcept>

namespace canto
{
// A bounded read-ahead source. Only the loader/worker touches the WAV reader.
// Blocks carry source positions, so stale seek data is discarded by the consumer
// without resetting live SPSC indices or taking a callback lock.
class WavStream
{
    static constexpr int blockFrames = 1024;
    struct Block
    {
        int64_t first = -1;
        int count = 0;
        std::array<float, blockFrames> left{}, right{};
    };
    std::unique_ptr<juce::AudioFormatReader> reader;
    Ring<Block, 8> ready;
    Block current;
    std::thread worker;
    std::atomic<bool> running{true};
    std::atomic<int64_t> requested{-1};
    int64_t cursor = 0, cachedIndex = -1;
    float cachedLeft = 0, cachedRight = 0;
    int popBudget = 16;
    bool waited = false;

    bool produce(juce::AudioBuffer<float>& scratch, Block& block)
    {
        block.first = cursor;
        block.count = int(std::min<int64_t>(blockFrames, length - cursor));
        if (block.count <= 0)
            return false;
        scratch.clear();
        if (!reader->read(&scratch, 0, block.count, cursor, true, true))
            throw std::runtime_error("WAV streaming read failed");
        for (int i = 0; i < block.count; ++i)
        {
            block.left[size_t(i)] = clean(scratch.getSample(0, i));
            block.right[size_t(i)] = clean(scratch.getSample(reader->numChannels == 1 ? 0 : 1, i));
        }
        cursor += block.count;
        return true;
    }
    bool frame(int64_t index, float& left, float& right) noexcept
    {
        if (index == cachedIndex)
        {
            left = cachedLeft;
            right = cachedRight;
            return true;
        }
        for (;;)
        {
            if (index >= current.first && index < current.first + current.count)
            {
                const auto at = size_t(index - current.first);
                left = current.left[at];
                right = current.right[at];
                if (at == size_t(current.count - 1))
                {
                    cachedIndex = index;
                    cachedLeft = left;
                    cachedRight = right;
                }
                return true;
            }
            if (popBudget == 0 || !ready.pop(current))
                return false;
            --popBudget;
        }
    }
  public:
    const int64_t length;
    const double sampleRate;
    std::atomic<bool> failed{false};
    std::atomic<uint64_t> waitBlocks{0};
    explicit WavStream(std::unique_ptr<juce::AudioFormatReader> source)
        : reader(std::move(source)), length(reader->lengthInSamples), sampleRate(reader->sampleRate)
    {
        // Preload on the caller's file-loader thread before publishing the source.
        juce::AudioBuffer<float> scratch(2, blockFrames);
        Block block;
        for (int i = 0; i < 4 && produce(scratch, block); ++i)
            ready.push(block);
        worker = std::thread([this]
        {
            try
            {
                juce::AudioBuffer<float> scratch(2, blockFrames);
                Block block;
                while (running.load())
                {
                    const auto target = requested.exchange(-1);
                    if (target >= 0)
                        cursor = std::clamp(target, int64_t(0), length - 1);
                    if (ready.size() < 8 && cursor < length)
                    {
                        if (produce(scratch, block))
                            ready.push(block);
                    }
                    else
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
            catch (...)
            {
                failed = true;
            }
        });
    }
    ~WavStream()
    {
        running = false;
        if (worker.joinable())
            worker.join();
    }
    void beginBlock() noexcept { popBudget = 16; waited = false; }
    void seek(int64_t index) noexcept
    {
        current.count = 0;
        cachedIndex = -1;
        requested = std::clamp(index, int64_t(0), length - 1);
    }
    bool sample(double position, float& left, float& right) noexcept
    {
        if (!std::isfinite(position) || position < 0 || position >= double(length))
        {
            left = right = 0;
            return false;
        }
        const auto index = int64_t(position);
        float aLeft = 0, aRight = 0, bLeft = 0, bRight = 0;
        if (index < 0 || index >= length || failed.load() ||
            !frame(index, aLeft, aRight) || !frame(std::min(index + 1, length - 1), bLeft, bRight))
        {
            left = right = 0;
            if (!waited)
            {
                ++waitBlocks;
                waited = true;
            }
            return false;
        }
        const float fraction = float(position - double(index));
        left = aLeft + (bLeft - aLeft) * fraction;
        right = aRight + (bRight - aRight) * fraction;
        return true;
    }
};
}
