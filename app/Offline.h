#pragma once
#include "engine/AudioEngine.h"
#include "platform/windows/InputLevel.h"

inline int runOffline(const juce::String& args)
{
    using namespace juce;
    auto tokens = StringArray::fromTokens(args, true);
    for (auto& t : tokens)
        t = t.unquoted();
    tokens.removeEmptyStrings();
    if (tokens.size() < 2)
        return 2;
    auto ownedEngine = std::make_unique<canto::AudioEngine>();
    auto& engine = *ownedEngine;
    const File destination = File::getCurrentWorkingDirectory().getChildFile(tokens[tokens.size() - 1]);
    if (destination.exists())
        return 3;
    if (tokens[0] == "--diagnostics" || tokens[0] == "--probe" || tokens[0] == "--probe-low" ||
        tokens[0] == "--probe-native")
    {
        int resultCode = 0;
        String report = engine.diagnostics() + "\nInputs:\n" + engine.inputs().joinIntoString("\n") +
                        "\nOutputs:\n" + engine.outputs().joinIntoString("\n") + "\n" +
                        canto::windowsInputLevels();
        if (tokens[0] == "--probe" || tokens[0] == "--probe-low" || tokens[0] == "--probe-native")
        {
            auto ins = engine.inputs(), outs = engine.outputs();
            if (ins.isEmpty() || outs.isEmpty())
            {
                report += "\nPROBE: no input/output endpoints";
                resultCode = 11;
            }
            else
            {
                const bool native = tokens[0] == "--probe-native", low = tokens[0] != "--probe",
                           named = tokens.size() >= 4;
                const String selectedIn = named ? tokens[1] : ins[0],
                             selectedOut = named ? tokens[2] : outs[0];
                const int duration = tokens.size() == 3   ? jlimit(1, 1800, tokens[1].getIntValue())
                                     : tokens.size() == 5 ? jlimit(1, 1800, tokens[3].getIntValue())
                                                          : 3;
                report += "\nSelected input: " + selectedIn + "\nSelected output: " + selectedOut;
                auto e = engine.connect(selectedIn, selectedOut, 48000, low ? 128 : 512, low, native);
                if (e.isNotEmpty())
                {
                    report += "\nOPEN FAILED: " + e;
                    resultCode = 12;
                }
                else
                {
                    Thread::sleep(duration * 1000);
                    report += "\nSILENT PROBE " + String(duration) +
                              " seconds (no monitoring or recording):\n" + engine.diagnostics() +
                              "\nInput peak: " + String(engine.inputPeak.load());
                    if (!engine.connected())
                        resultCode = 13;
                }
            }
        }
        return destination.replaceWithText(report) ? resultCode : 4;
    }
    if (tokens[0] == "--test-playback" && tokens.size() == 3)
    {
        engine.prepareOffline(48000);
        engine.params.monitor = false;
        const auto error = engine.loadTrack(File::getCurrentWorkingDirectory().getChildFile(tokens[1]));
        if (error.isNotEmpty() || engine.duration < 0.5 || engine.duration > 10)
            return 19; // This command requires a short, non-silent synthetic fixture.
        AudioBuffer<float> capture(2, 256), output(2, 256);
        capture.clear();
        const auto playbackRecording = destination.withFileExtension("wav");
        if (engine.recorder.start(playbackRecording, 48000, false).isNotEmpty())
            return 25;
        float peak = 0;
        auto tick = [&]
        {
            engine.render(capture.getArrayOfReadPointers(), 2, nullptr, 0, 256, true);
            engine.render(nullptr, 0, output.getArrayOfWritePointers(), 2, 256, false);
            peak = std::max(peak, output.getMagnitude(0, 256));
            Thread::sleep(1); // Test driver only, never inside render().
        };
        engine.playing = true;
        for (int i = 0; i < 1000 && engine.seconds < 0.3; ++i)
            tick();
        if (engine.seconds < 0.3 || peak < 0.001 || peak > 0.951)
            return 20;
        engine.playing = false;
        const double pausedPosition = engine.seconds.load();
        for (int i = 0; i < 4; ++i)
            tick();
        if (engine.seconds != pausedPosition || output.getMagnitude(0, 256) != 0)
            return 21;
        engine.seek = 0.1;
        engine.playing = true;
        tick();
        if (engine.seconds < 0.1 || engine.seconds > 0.1 + 256.0 / 48000 + 1.e-6)
            return 22;
        for (int i = 0; i < 1000 && engine.seconds < 0.2; ++i)
            tick();
        if (engine.seconds < 0.2)
            return 23;
        engine.seek = engine.duration - 0.05;
        for (int i = 0; i < 1000 && engine.playing.load(); ++i)
            tick();
        if (engine.playing || engine.trackReadFailed() || engine.seconds < engine.duration - 0.001)
            return 24;
        engine.recorder.stop();
        auto recordedStream = playbackRecording.createInputStream();
        if (!recordedStream || engine.recorder.failed.load())
            return 26;
        WavAudioFormat recordedFormat;
        std::unique_ptr<AudioFormatReader> recorded(recordedFormat.createReaderFor(recordedStream.release(), true));
        if (!recorded || recorded->numChannels != 2 || recorded->lengthInSamples != int64(engine.recorder.frames.load()))
            return 26;
        float recordedPeak = 0;
        for (int64 at = 0; at < recorded->lengthInSamples; at += 256)
        {
            output.clear();
            if (!recorded->read(&output, 0, int(std::min<int64>(256, recorded->lengthInSamples - at)), at, true, true))
                return 26;
            recordedPeak = std::max(recordedPeak, output.getMagnitude(0, 256));
        }
        if (recordedPeak < 0.001 || recordedPeak > 0.951)
            return 27;
        return destination.replaceWithText("PASS: streamed WAV through actual engine; nonzero mix and finalized backing-only recording, pause silence/position, backward/forward seek, end-of-track stop. No devices opened.\n" + engine.diagnostics()) ? 0 : 4;
    }
    if (tokens[0] != "--render" || tokens.size() != 3)
        return 2;
    const File source = File::getCurrentWorkingDirectory().getChildFile(tokens[1]);
    WavAudioFormat format;
    auto stream = source.createInputStream();
    if (!stream)
        return 5;
    std::unique_ptr<AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
    if (!reader || reader->numChannels > 2 || reader->sampleRate < 8000 || reader->sampleRate > 192000)
        return 6;
    engine.prepareOffline(reader->sampleRate);
    auto e = engine.recorder.start(destination, reader->sampleRate, true);
    if (e.isNotEmpty())
        return 7;
    AudioBuffer<float> input(2, 256), output(2, 256);
    int64 processed = 0, total = reader->lengthInSamples + int64(reader->sampleRate * 2);
    while (processed < total)
    {
        int n = int(std::min<int64>(256, total - processed));
        input.clear();
        if (processed < reader->lengthInSamples)
            reader->read(&input, 0, int(std::min<int64>(n, reader->lengthInSamples - processed)), processed,
                         true, true);
        engine.render(input.getArrayOfReadPointers(), 2, nullptr, 0, n, true);
        engine.render(nullptr, 0, output.getArrayOfWritePointers(), 2, n, false);
        processed += n;
        // Offline producer throttling, never executed in a live audio callback.
        if (processed % 32768 == 0)
            Thread::sleep(10);
    }
    engine.recorder.stop();
    if (engine.recorder.failed.load() || engine.recorder.frames.load() != uint64_t(total))
        return 8;
    for (auto suffix : {String{}, String("-dry"), String("-wet")})
    {
        auto f =
            suffix.isEmpty()
                ? destination
                : destination.getSiblingFile(destination.getFileNameWithoutExtension() + suffix + ".wav");
        auto checkStream = f.createInputStream();
        if (!checkStream)
            return 9;
        std::unique_ptr<AudioFormatReader> check(format.createReaderFor(checkStream.release(), true));
        if (!check || check->lengthInSamples != total || check->numChannels != (suffix.isEmpty() ? 2u : 1u))
            return 9;
    }
    engine.params.mute = true;
    engine.render(nullptr, 0, output.getArrayOfWritePointers(), 2, 256, false);
    if (output.getMagnitude(0, 256) != 0)
        return 10;
    // Offline fault injection: no device streams are opened. Verify both the
    // output and recorder taps fail silent even if the monitor flag is still on.
    engine.params.mute = false;
    engine.params.monitor = true;
    engine.fault = true;
    const auto faultFile = destination.getSiblingFile(destination.getFileNameWithoutExtension() + "-fault.wav");
    if (engine.recorder.start(faultFile, reader->sampleRate, true).isNotEmpty())
        return 14;
    for (int block = 0; block < 16; ++block)
    {
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 256; ++k)
                input.setSample(c, k, 0.25f);
        engine.render(input.getArrayOfReadPointers(), 2, nullptr, 0, 256, true);
        engine.render(nullptr, 0, output.getArrayOfWritePointers(), 2, 256, false);
        if (output.getMagnitude(0, 256) != 0)
            return 15;
    }
    engine.recorder.stop();
    if (engine.recorder.failed.load() || engine.recorder.frames.load() != 4096)
        return 16;
    for (auto suffix : {String{}, String("-dry"), String("-wet")})
    {
        const auto file = suffix.isEmpty() ? faultFile : faultFile.getSiblingFile(
            faultFile.getFileNameWithoutExtension() + suffix + ".wav");
        auto stream = file.createInputStream();
        if (!stream)
            return 17;
        std::unique_ptr<AudioFormatReader> check(format.createReaderFor(stream.release(), true));
        if (!check || check->lengthInSamples != 4096 || check->numChannels != (suffix.isEmpty() ? 2u : 1u))
            return 17;
        AudioBuffer<float> silence(int(check->numChannels), 4096);
        if (!check->read(&silence, 0, 4096, 0, true, true) || silence.getMagnitude(0, 4096) != 0)
            return 18;
    }
    return 0;
}
