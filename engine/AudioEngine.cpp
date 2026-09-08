#include "AudioEngine.h"
#if JUCE_WINDOWS
#include "platform/windows/NativeWasapi.h"
#endif

namespace canto
{
juce::String Recorder::start(const juce::File& file, double sr, bool stems)
{
    stop();
    queue.reset();
    failed = false;
    dropped = 0;
    frames = 0;
    if (file.exists())
        return "Recording already exists";
    auto makeWriter = [sr](const juce::File& f, int channels) -> std::unique_ptr<juce::AudioFormatWriter>
    {
        auto stream = f.createOutputStream();
        if (!stream || stream->failedToOpen())
            return {};
        juce::WavAudioFormat wav;
        auto* w = wav.createWriterFor(stream.get(), sr, (unsigned)channels, 24, {}, 0);
        if (w)
            stream.release();
        return std::unique_ptr<juce::AudioFormatWriter>(w);
    };
    auto dryFile = file.getSiblingFile(file.getFileNameWithoutExtension() + "-dry.wav");
    auto wetFile = file.getSiblingFile(file.getFileNameWithoutExtension() + "-wet.wav");
    if (stems && (dryFile.exists() || wetFile.exists()))
        return "Stem already exists";
    auto mix = makeWriter(file, 2);
    auto dry = stems ? makeWriter(dryFile, 1) : nullptr;
    auto wet = stems ? makeWriter(wetFile, 1) : nullptr;
    if (!mix || (stems && (!dry || !wet)))
        return "Cannot create WAV files";
    running = true;
    active = true;
    worker = std::thread(
        [this, sr, m = std::move(mix), d = std::move(dry), w = std::move(wet)]() mutable
        {
            juce::AudioBuffer<float> mb(2, 1024), db(1, 1024), wb(1, 1024);
            while (running.load() || queue.size() > 0)
            {
                RecordedFrame f{};
                int n = 0;
                while (n < 1024 && queue.pop(f))
                {
                    mb.setSample(0, n, f.left);
                    mb.setSample(1, n, f.right);
                    db.setSample(0, n, f.dry);
                    wb.setSample(0, n, f.wet);
                    ++n;
                }
                if (n > 0)
                {
                    bool ok = m->writeFromAudioSampleBuffer(mb, 0, n);
                    if (d)
                        ok = d->writeFromAudioSampleBuffer(db, 0, n) && ok;
                    if (w)
                        ok = w->writeFromAudioSampleBuffer(wb, 0, n) && ok;
                    frames += uint64_t(n);
                    // Stop before RIFF reaches 4 GiB; no silent truncation.
                    if (!ok || frames.load() > std::min(uint64_t(sr * 3600 * 3), uint64_t(500000000)))
                    {
                        failed = true;
                        active = false;
                        running = false;
                        break;
                    }
                }
                else
                    std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
            m.reset();
            d.reset();
            w.reset();
        });
    return {};
}
void Recorder::stop()
{
    active = false;
    running = false;
    if (worker.joinable())
        worker.join();
}
AudioEngine::AudioEngine()
{
#if JUCE_WINDOWS
    type.reset(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(false));
#endif
    if (type)
        type->addListener(this);
    scan();
}
AudioEngine::~AudioEngine()
{
    close();
    if (type)
        type->removeListener(this);
}
void AudioEngine::scan()
{
    if (type)
        type->scanForDevices();
#if JUCE_WINDOWS
    NativeWasapi discovery;
    endpoints = discovery.enumerate();
#endif
}
juce::String AudioEngine::endpointId(const juce::String& name, bool isInput) const
{
    for (const auto& e : endpoints)
        if (e.input == isInput && name == juce::String::fromUTF8(e.name.c_str()))
            return juce::String::fromUTF8(e.id.c_str());
    return {};
}
juce::String AudioEngine::endpointName(const juce::String& id, bool isInput) const
{
    for (const auto& e : endpoints)
        if (e.input == isInput && id == juce::String::fromUTF8(e.id.c_str()))
            return juce::String::fromUTF8(e.name.c_str());
    return {};
}
juce::StringArray AudioEngine::inputs()
{
    return type ? type->getDeviceNames(true) : juce::StringArray{};
}
juce::StringArray AudioEngine::outputs()
{
    return type ? type->getDeviceNames(false) : juce::StringArray{};
}
void AudioEngine::close()
{
    params.monitor = false;
    if (native)
        native->close();
    if (output)
        output->stop();
    if (input)
        input->stop();
    latencyProbe.cancel();
    recorder.stop();
    native.reset();
    output.reset();
    input.reset();
}
void AudioEngine::prepareNative(const BackendFormat& f)
{
    rate = f.outputRate;
    expectedInputRate = f.inputRate;
    dsp.prepare(rate);
    limiter.prepare(rate);
    latencyProbe.prepare(rate);
    monitorGain = masterGain = musicGain = 0;
    testRemaining = 0;
    bridge.prepare(f.inputRate, f.outputRate, std::max(f.inputPeriod, f.outputQueueTarget), true,
                   f.inputPeriod, f.outputQueueTarget);
}
void AudioEngine::suspendOutput()
{
    if (native)
        native->pause(true);
    else if (output)
        output->stop();
}
void AudioEngine::resumeOutput()
{
    if (native)
        native->pause(false);
    else if (output)
        output->start(&playback);
}
juce::String AudioEngine::connect(const juce::String& in, const juce::String& out, double requested,
                                  int block, bool lowLatency, bool nativeBackend)
{
    if (latencyProbe.busy())
        return "Finish or cancel the latency measurement before reconnecting";
    close();
    fault = false;
#if JUCE_WINDOWS
    if (nativeBackend)
    {
        native = std::make_unique<NativeWasapi>();
        BackendCallbacks callbacks;
        callbacks.context = this;
        callbacks.prepare = [](void* p, const BackendFormat& f)
        { static_cast<AudioEngine*>(p)->prepareNative(f); };
        callbacks.process =
            [](void* p, const float* const* i, int ni, float* const* o, int no, int n, bool isInput)
        { static_cast<AudioEngine*>(p)->render(i, ni, o, no, n, isInput); };
        callbacks.failed = [](void* p, uint32_t)
        {
            auto& self = *static_cast<AudioEngine*>(p);
            self.params.monitor = false;
            self.fault = true;
        };
        return juce::String::fromUTF8(
            native->open({in.toStdString(), out.toStdString(), requested, block, lowLatency}, callbacks)
                .c_str());
    }
    if (lowLatencyMode != lowLatency)
    {
        if (type)
            type->removeListener(this);
        type.reset(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(
            lowLatency ? juce::WASAPIDeviceMode::sharedLowLatency : juce::WASAPIDeviceMode::shared));
        lowLatencyMode = lowLatency;
        if (type)
            type->addListener(this);
        scan();
    }
#endif
    if (!type || in.isEmpty() || out.isEmpty())
        return "Select microphone and output";
    input.reset(type->createDevice({}, in));
    output.reset(type->createDevice(out, {}));
    if (!input || !output)
    {
        close();
        return "Endpoint unavailable";
    }
    juce::BigInteger ins, outs;
    ins.setRange(0, juce::jmin(2, input->getInputChannelNames().size()), true);
    outs.setRange(0, juce::jmin(2, output->getOutputChannelNames().size()), true);
    auto open =
        [requested, block](juce::AudioIODevice& d, const juce::BigInteger& i, const juce::BigInteger& o)
    {
        auto rates = d.getAvailableSampleRates();
        double sr = rates.contains(requested) ? requested : (rates.isEmpty() ? 48000 : rates[0]);
        auto sizes = d.getAvailableBufferSizes();
        int selected = block;
        if (!sizes.isEmpty())
        {
            selected = sizes[0];
            for (auto size : sizes)
                if (std::abs(size - block) < std::abs(selected - block))
                    selected = size;
        }
        return d.open(i, o, sr, selected);
    };
    auto error = open(*input, ins, {});
    if (error.isEmpty())
        error = open(*output, {}, outs);
    if (error.isNotEmpty())
    {
        close();
        return error;
    }
    rate = output->getCurrentSampleRate();
    expectedInputRate = input->getCurrentSampleRate();
    dsp.prepare(rate);
    limiter.prepare(rate);
    latencyProbe.prepare(rate);
    monitorGain = masterGain = musicGain = 0;
    testRemaining = 0;
    bridge.prepare(input->getCurrentSampleRate(), rate,
                   juce::jmax(input->getCurrentBufferSizeSamples(), output->getCurrentBufferSizeSamples()),
                   lowLatencyMode, input->getCurrentBufferSizeSamples(),
                   output->getCurrentBufferSizeSamples());
    input->start(&capture);
    output->start(&playback);
    return {};
}
void AudioEngine::Callback::audioDeviceIOCallbackWithContext(const float* const* i, int ni, float* const* o,
                                                             int no, int n,
                                                             const juce::AudioIODeviceCallbackContext&)
{
    owner.render(i, ni, o, no, n, input);
}
void AudioEngine::render(const float* const* in, int ni, float* const* out, int no, int n,
                         bool isInput) noexcept
{
    juce::ScopedNoDenormals guard;
    if (isInput)
    {
        float peak = 0;
        const int ch = params.channel.load();
        for (int k = 0; k < n; ++k)
        {
            float v = ni > 0 && in[0] ? in[0][k] : 0;
            if (ch == 1)
                v = ni > 1 && in[1] ? in[1][k] : 0;
            else if (ch == 2 && ni > 1 && in[1])
                v = (v + in[1][k]) * 0.5f;
            v = clean(v);
            peak = std::max(peak, std::abs(v));
            bridge.push(v);
        }
        inputPeak = std::max(peak, inputPeak.load() * 0.92f);
        return;
    }
    const auto begin = juce::Time::getHighResolutionTicks();
    bridge.beginBlock();
    if (track)
        track->beginBlock();
    const auto wanted = seek.exchange(-1);
    if (wanted >= 0)
    {
        position = std::clamp(wanted, 0.0, duration) * trackRate;
        if (track)
            track->seek(int64_t(position));
    }
    if (testRequested.exchange(false))
    {
        testRemaining = int(rate * 0.5);
        testPhase = 0;
    }
    float peak = 0, mp = 0;
    for (int k = 0; k < n; ++k)
    {
        const float dry = bridge.next(), wet = dsp.process(dry, params);
        monitorGain += 0.002f * ((params.monitor.load() && !fault.load() ? 1.f : 0.f) - monitorGain);
        masterGain += 0.001f * (params.master.load() - masterGain);
        musicGain += 0.001f * ((params.musicMute.load() ? 0.f : params.music.load()) - musicGain);
        float music[2]{};
        if (playing.load() && track)
        {
            if (position >= track->length - 1 || track->failed.load())
                playing = false;
            else
            {
                if (track->sample(position, music[0], music[1]))
                {
                    music[0] *= musicGain;
                    music[1] *= musicGain;
                    position += trackRate / rate;
                }
            }
        }
        float tone = 0;
        if (testRemaining > 0)
        {
            tone = float(std::sin(testPhase)) * 0.025f * std::min(1.f, testRemaining / 256.f);
            testPhase += 2 * juce::MathConstants<double>::pi * 440 / rate;
            --testRemaining;
        }
        const float probeSignal = latencyProbe.signal();
        const bool probing = latencyProbe.state() == LatencyProbe::State::capturing;
        float mix[2];
        for (int c = 0; c < 2; ++c)
        {
            float v = clean((probing ? probeSignal : wet * monitorGain + music[c] + tone) * masterGain);
            if (std::abs(v) > 0.95f)
                ++clipped;
            mix[c] = (params.mute.load() || fault.load()) ? 0.f : v;
            mp = std::max(mp, std::abs(music[c]));
        }
        limiter.process(mix[0], mix[1]);
        peak = std::max(peak, std::max(std::abs(mix[0]), std::abs(mix[1])));
        latencyProbe.feed(dry, mix[0]);
        for (int c = 0; c < no; ++c)
            if (out[c])
                out[c][k] = no == 1 ? (mix[0] + mix[1]) * 0.5f : mix[std::min(c, 1)];
        const bool silenceStems = params.mute.load() || fault.load();
        recorder.push({mix[0], mix[1], silenceStems ? 0.f : dry, silenceStems ? 0.f : wet});
    }
    seconds = position / trackRate;
    outputPeak = std::max(peak, outputPeak.load() * 0.92f);
    musicPeak = std::max(mp, musicPeak.load() * 0.92f);
    limiterGain = limiter.currentGain();
    callbackLoad =
        juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - begin) / (n / rate);
}
juce::String AudioEngine::loadTrack(const juce::File& file)
{
    juce::WavAudioFormat format;
    auto stream = file.createInputStream();
    if (!stream)
        return "Cannot open file";
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
    if (!reader || reader->lengthInSamples < 2 || reader->numChannels < 1 || reader->numChannels > 2 ||
        !std::isfinite(reader->sampleRate) || reader->sampleRate < 8000 || reader->sampleRate > 192000 ||
        reader->lengthInSamples > reader->sampleRate * 86400)
        return "Use mono/stereo WAV at 8-192 kHz, up to 24 hours";
    auto next = std::make_unique<WavStream>(std::move(reader));
    if (recorder.active.load())
        return "Stop recording before changing track";
    const bool was = transportRunning();
    if (was)
        suspendOutput();
    playing = false;
    auto retired = std::move(track);
    track = std::move(next);
    trackRate = track->sampleRate;
    position = 0;
    seconds = 0;
    seek = -1;
    duration = track->length / trackRate;
    trackName = file.getFileName();
    if (was)
        resumeOutput();
    retired.reset(); // Join/retire the old reader after live audio has resumed.
    return {};
}
juce::String AudioEngine::record(const juce::File& file, bool stems)
{
    if (!connected())
        return "Connect audio first";
    bool monitoring = params.monitor.load();
    suspendOutput();
    auto e = recorder.start(file, rate, stems);
    resumeOutput();
    params.monitor = monitoring && !fault.load();
    return e;
}
void AudioEngine::stopRecording()
{
    bool was = transportRunning(), monitoring = params.monitor.load();
    if (was)
        suspendOutput();
    recorder.stop();
    if (was)
        resumeOutput();
    params.monitor = monitoring && !fault.load();
}
juce::String AudioEngine::diagnostics() const
{
    juce::String s = "CantoDeck 0.1.0 | JUCE 8.0.15 | " + juce::SystemStats::getOperatingSystemName() +
                     (lowLatencyMode ? "\nWASAPI shared LOW LATENCY | adaptive windowed-sinc resampler\n"
                                     : "\nWASAPI shared | adaptive windowed-sinc resampler\n");
    if (native)
    {
        const auto f = native->format();
        const auto c = native->counters();
        s = "CantoDeck 0.1.0 | Native WASAPI | one MMCSS audio thread\nInput/output rates: " +
            juce::String(f.inputRate) + " / " + juce::String(f.outputRate) +
            " Hz\nEngine periods: " + juce::String(f.inputPeriod) + " / " + juce::String(f.outputPeriod) +
            " samples; WASAPI capacities: " + juce::String(f.inputCapacity) + " / " +
            juce::String(f.outputCapacity) + "\nOutput queue target: " + juce::String(f.outputQueueTarget) +
            " samples\nDriver stream latency: " + juce::String(f.inputDriverSeconds * 1000, 2) + " / " +
            juce::String(f.outputDriverSeconds * 1000, 2) +
            " ms (zero may be unreported; not RTT)\nIAudioClient3 input/output: " +
            juce::String(f.inputLowLatency ? "yes" : "no") + " / " +
            juce::String(f.outputLowLatency ? "yes" : "no") +
            "; RAW input request accepted: " + juce::String(f.rawInput ? "yes" : "no") +
            "\nCapture packets/render callbacks: " + juce::String(c.capturePackets) + " / " +
            juce::String(c.renderCallbacks) +
            "; capture discontinuities: " + juce::String(c.captureDiscontinuities) +
            " (first packet: " + juce::String(c.initialCaptureDiscontinuities) +
            "); capture timestamp errors: " + juce::String(c.captureTimestampErrors) +
            "\nMax capture/render service interval: " + juce::String(c.maxCaptureServiceGapMs, 2) + " / " +
            juce::String(c.maxRenderServiceGapMs, 2) + " ms (service timing, not RTT)" +
            "; zero-padding observations: " + juce::String(c.zeroPaddingEvents) + "\nLast HRESULT: 0x" +
            juce::String::toHexString(c.lastError) + "\n";
    }
    if (output && input)
        s += "Input: " + juce::String(input->getCurrentSampleRate()) + " Hz / " +
             juce::String(input->getCurrentBufferSizeSamples()) + " samples; output: " + juce::String(rate) +
             " Hz / " + juce::String(output->getCurrentBufferSizeSamples()) +
             " samples\nDriver latency input/output: " + juce::String(input->getInputLatencyInSamples()) +
             " / " + juce::String(output->getOutputLatencyInSamples()) + " samples\n";
    s += "FIFO: " + juce::String((int)bridge.buffered()) +
         " samples; target: " + juce::String((int)bridge.targetFrames()) +
         "; ratio: " + juce::String(bridge.ratio.load(), 6) +
         "; under/over: " + juce::String(bridge.underruns.load()) + " / " +
         juce::String(bridge.overruns.load()) +
         "; resyncs/discarded input frames: " + juce::String(bridge.resyncs.load()) + " / " +
         juce::String(bridge.discardedFrames.load()) +
         "\nCallback load: " + juce::String(callbackLoad.load() * 100, 1) +
         "%; limited samples: " + juce::String(clipped.load()) +
         "; limiter gain: " + juce::String(limiterGain.load(), 3) +
         "\nRecording dropped: " + juce::String(recorder.dropped.load()) +
         "; fault: " + juce::String(fault.load() ? "yes" : "no") +
         "\nRound-trip latency: not inferred from buffers. Zero-lookahead sample-peak limiter, not "
         "true-peak.\nBrowser audio is NOT included in recording.";
    if (track)
        s += "\nStreaming WAV: 8 x 1024 queued source frames; music wait blocks: " +
             juce::String(track->waitBlocks.load()) + "; read error: " +
             juce::String(track->failed.load() ? "yes" : "no");
    return s;
}
} // namespace canto
