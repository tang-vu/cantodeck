#pragma once
#include "Core.h"
#include "audio/AudioBackend.h"
#include "audio/LatencyProbe.h"
#include "audio/WavStream.h"
#include "audio/MonitoringStopPolicy.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <thread>

namespace canto
{
struct RecordedFrame
{
    float left, right, dry, wet;
};
class Recorder
{
    Ring<RecordedFrame, 262144> queue;
    std::thread worker;
    std::atomic<bool> running{false};

  public:
    std::atomic<bool> active{false}, failed{false};
    std::atomic<uint64_t> dropped{0}, frames{0};
    ~Recorder() { stop(); }
    juce::String start(const juce::File&, double, bool);
    void stop(); // Caller must stop output callback first.
    void push(RecordedFrame f) noexcept
    {
        if (active.load() && !queue.push(f))
        {
            ++dropped;
            failed = true;
            active = false;
        }
    }
};
class AudioEngine : private juce::AudioIODeviceType::Listener
{
    MonitoringStopPolicy stopPolicy;
    struct Callback : juce::AudioIODeviceCallback
    {
        AudioEngine& owner;
        bool input;
        Callback(AudioEngine& o, bool i) : owner(o), input(i) {}
        void audioDeviceAboutToStart(juce::AudioIODevice* d) override
        {
            if (owner.expectedInputRate > 0 &&
                d->getCurrentSampleRate() != (input ? owner.expectedInputRate : owner.rate))
            {
                owner.params.monitor = false;
                owner.fault = true;
            }
        }
        void audioDeviceStopped() override
        {
            if (owner.stopPolicy.mustMute(input))
                owner.params.monitor = false;
        }
        void audioDeviceError(const juce::String&) override
        {
            owner.params.monitor = false;
            owner.fault = true;
        }
        void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int,
                                              const juce::AudioIODeviceCallbackContext&) override;
    } capture{*this, true}, playback{*this, false};
    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> input, output;
    std::unique_ptr<AudioBackend> native;
    std::vector<EndpointDescription> endpoints;
    VocalDSP dsp;
    SamplePeakLimiter limiter;
    std::unique_ptr<WavStream> track;
    double trackRate = 48000, position = 0, rate = 48000, expectedInputRate = 0;
    float monitorGain = 0, masterGain = 0, musicGain = 0;
    double testPhase = 0;
    int testRemaining = 0;
    bool lowLatencyMode = false;
    LatencyContinuity measurementStart;
    LatencyContinuity latencyContinuity() const;
    void prepareNative(const BackendFormat&);
    void suspendOutput();
    void resumeOutput();
    bool transportRunning() const { return native ? native->isRunning() : (output && output->isPlaying()); }
    void audioDeviceListChanged() override
    {
        params.monitor = false;
        fault = true;
        devicesChanged = true;
    }

  public:
    Parameters params;
    ClockBridge bridge;
    Recorder recorder;
    LatencyProbe latencyProbe;
    std::atomic<bool> playing{false}, fault{false}, testRequested{false};
    std::atomic<bool> devicesChanged{false};
    std::atomic<float> inputPeak{0}, vocalPeak{0}, musicPeak{0}, outputPeak{0};
    std::atomic<float> limiterGain{1};
    std::atomic<double> seconds{0}, seek{-1}, callbackLoad{0};
    std::atomic<uint64_t> clipped{0};
    double duration = 0;
    juce::String trackName;
    AudioEngine();
    ~AudioEngine();
    juce::StringArray inputs(), outputs();
    juce::String endpointId(const juce::String& name, bool isInput) const;
    juce::String endpointName(const juce::String& id, bool isInput) const;
    void scan();
    juce::String connect(const juce::String&, const juce::String&, double, int, bool lowLatency = false,
                         bool nativeBackend = false);
    void close();
    bool connected() const
    {
        return !fault.load() && (native ? native->isRunning()
                                        : (output && output->isPlaying() && input && input->isPlaying()));
    }
    juce::String diagnostics() const;
    juce::String loadTrack(const juce::File&);
    bool trackReadFailed() const { return track && track->failed.load(); }
    juce::String record(const juce::File&, bool);
    void stopRecording();
    bool startLatencyMeasurement()
    {
        if (!connected() || recorder.active.load() || latencyProbe.busy())
            return false;
        params.monitor = false;
        playing = false;
        measurementStart = latencyContinuity();
        return latencyProbe.begin();
    }
    LatencyResult finishLatencyMeasurement(LatencyResult result) const
    {
        return validateLatencyContinuity(std::move(result), measurementStart, latencyContinuity(), connected());
    }
    void prepareOffline(double sr)
    {
        close();
        rate = sr;
        dsp.prepare(sr);
        limiter.prepare(sr);
        bridge.prepare(sr, sr, 256);
        params.monitor = true;
        params.master = 0.5f;
    }
    void render(const float* const*, int, float* const*, int, int, bool) noexcept;
};
} // namespace canto
