#include "engine/audio/WavStream.h"
#include <iostream>

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
float fixture(int64_t frame, int channel)
{
    const float value = float(frame % 200 - 100) / 128.f;
    return channel == 0 ? value : -value * 0.5f;
}
struct ReaderControl
{
    std::atomic<bool> release{false}, fail{false}, sampling{false};
    std::atomic<int> calls{0}, consumerReads{0};
    const std::thread::id consumer = std::this_thread::get_id();
};
// Explicit synthetic fault adapter, not an actual disk or hardware benchmark.
class ControlledReader final : public juce::AudioFormatReader
{
    std::shared_ptr<ReaderControl> control;
  public:
    explicit ControlledReader(std::shared_ptr<ReaderControl> state)
        : AudioFormatReader(nullptr, "Synthetic fault reader"), control(std::move(state))
    {
        sampleRate = 48000;
        numChannels = 2;
        bitsPerSample = 32;
        usesFloatingPointData = true;
        lengthInSamples = 48000LL * 86400;
    }
    bool readSamples(int* const* channels, int count, int offset, juce::int64 start, int frames) override
    {
        ++control->calls;
        if (control->sampling && std::this_thread::get_id() == control->consumer)
            ++control->consumerReads;
        if (start >= 4096)
        {
            while (!control->release)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (control->fail)
            return false;
        for (int c = 0; c < count; ++c)
            if (channels[c])
                for (int i = 0; i < frames; ++i)
                    reinterpret_cast<float*>(channels[c])[offset + i] = fixture(start + i, c);
        return true;
    }
};
int main()
{
    try
    {
        {
            auto control = std::make_shared<ReaderControl>();
            canto::WavStream player(std::make_unique<ControlledReader>(control));
            struct ReleaseOnExit
            {
                std::shared_ptr<ReaderControl> state;
                ~ReleaseOnExit() { state->release = true; }
            } releaseOnExit{control}; // Release the worker before player destruction on test failure.
            player.seek(20000);
            player.beginBlock();
            control->sampling = true;
            float left = 1, right = 1;
            for (int i = 0; i < 20; ++i)
                check(!player.sample(20000, left, right) && left == 0 && right == 0,
                      "stalled reader must not return stale pre-seek samples");
            control->sampling = false;
            check(player.waitBlocks == 1 && control->consumerReads == 0,
                  "stream waits counted once per callback, no reader call on sample thread");
            control->release = true;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            for (;;)
            {
                player.beginBlock();
                if (player.sample(20000, left, right))
                    break;
                check(std::chrono::steady_clock::now() < deadline, "stalled source did not resume");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(left == fixture(20000, 0) && right == fixture(20000, 1),
                  "recovered seek returns exact requested source frame");
            while (player.queuedBlocks() < 8)
            {
                check(std::chrono::steady_clock::now() < deadline, "read-ahead queue did not fill");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto reads = control->calls.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            check(control->calls == reads && reads < 24,
                  "read-ahead stops when bounded queue is full, regardless of declared duration");
        }
        {
            auto control = std::make_shared<ReaderControl>();
            canto::WavStream player(std::make_unique<ControlledReader>(control));
            control->fail = true;
            control->release = true;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!player.failed && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            check(player.failed, "worker read failure must be latched");
            float left = 1, right = 1;
            check(!player.sample(0, left, right) && left == 0 && right == 0,
                  "failed worker silences even previously cached audio");
        }
        {
            auto control = std::make_shared<ReaderControl>();
            control->fail = true;
            bool rejected = false;
            try { canto::WavStream player(std::make_unique<ControlledReader>(control)); }
            catch (const std::runtime_error&) { rejected = true; }
            check(rejected, "prefill read failure rejects construction before worker publication");
        }
        constexpr int frames = 17003;
        for (int channels : {1, 2})
            for (double rate : {44100., 48000.})
            {
                juce::TemporaryFile temporary(".wav");
                auto file = temporary.getFile();
                juce::WavAudioFormat format;
                auto output = file.createOutputStream();
                check(output != nullptr, "create synthetic stream fixture");
                std::unique_ptr<juce::AudioFormatWriter> writer(
                    format.createWriterFor(output.get(), rate, unsigned(channels), 24, {}, 0));
                check(writer != nullptr, "create WAV writer");
                output.release();
                juce::AudioBuffer<float> data(channels, frames);
                for (int c = 0; c < channels; ++c)
                    for (int i = 0; i < frames; ++i)
                        data.setSample(c, i, fixture(i, c));
                check(writer->writeFromAudioSampleBuffer(data, 0, frames), "write synthetic fixture");
                writer.reset();
                auto input = file.createInputStream();
                std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(input.release(), true));
                check(reader != nullptr, "open fixture reader");
                canto::WavStream player(std::move(reader));
                check(player.length == frames && player.sampleRate == rate, "stream metadata");
                float invalidLeft = 1, invalidRight = 1;
                check(!player.sample(std::numeric_limits<double>::quiet_NaN(), invalidLeft, invalidRight) &&
                          invalidLeft == 0 && invalidRight == 0, "invalid stream position is silent");
                int sampleCounter = 0;
                auto verify = [&](double position)
                {
                    if (sampleCounter++ % 128 == 0)
                        player.beginBlock();
                    float left = 0, right = 0;
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                    while (!player.sample(position, left, right))
                    {
                        check(!player.failed && std::chrono::steady_clock::now() < deadline,
                              "stream failed or did not deliver requested source frame");
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        player.beginBlock();
                    }
                    const auto index = int64_t(position);
                    const float fraction = float(position - index);
                    for (int c = 0; c < 2; ++c)
                    {
                        const int sourceChannel = channels == 1 ? 0 : c;
                        const float a = fixture(index, sourceChannel),
                                    b = fixture(std::min<int64_t>(index + 1, frames - 1), sourceChannel);
                        const float expected = a + (b - a) * fraction;
                        check(std::abs((c == 0 ? left : right) - expected) < 2.e-6f,
                              "stream/interpolation differs from original source samples");
                    }
                };
                // Fractional positions repeat boundary frames while interpolating across blocks.
                for (double position = 0; position < frames - 1; position += 0.75)
                    verify(position);
                for (int64_t start : {0, 8191, 1023, 16000, 4000})
                {
                    player.seek(start);
                    player.beginBlock();
                    for (double position = double(start); position < std::min(double(start + 500), double(frames - 1)); position += 1.125)
                        verify(position);
                }
                player.seek(frames - 1);
                player.beginBlock();
                verify(frames - 1);
            }
        std::cout << "PASS: bounded WAV streaming, mono/stereo, 44.1/48 kHz, fractional boundaries, seeks/final frame, synthetic reader stall/recovery/failure\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
