#pragma once
#include "Core.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <thread>

namespace canto {
struct RecordedFrame { float left,right,dry,wet; };
class Recorder {
    Ring<RecordedFrame,262144> queue;
    std::thread worker;
    std::atomic<bool> running{false};
public:
    std::atomic<bool> active{false}, failed{false};
    std::atomic<uint64_t> dropped{0}, frames{0};
    ~Recorder() { stop(); }
    juce::String start(const juce::File&, double, bool);
    void stop(); // Caller must stop output callback first.
    void push(RecordedFrame f) noexcept { if(active.load() && !queue.push(f)) { ++dropped; failed=true; active=false; } }
};
class AudioEngine : private juce::AudioIODeviceType::Listener {
    struct Callback : juce::AudioIODeviceCallback {
        AudioEngine& owner; bool input;
        Callback(AudioEngine& o,bool i):owner(o),input(i){}
        void audioDeviceAboutToStart(juce::AudioIODevice* d) override { if(owner.expectedInputRate>0 && d->getCurrentSampleRate()!=(input?owner.expectedInputRate:owner.rate)) { owner.params.monitor=false; owner.fault=true; } }
        void audioDeviceStopped() override { owner.params.monitor=false; }
        void audioDeviceError(const juce::String&) override { owner.params.monitor=false; owner.fault=true; }
        void audioDeviceIOCallbackWithContext(const float* const*,int,float* const*,int,int,const juce::AudioIODeviceCallbackContext&) override;
    } capture{*this,true}, playback{*this,false};
    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> input,output;
    VocalDSP dsp;
    juce::AudioBuffer<float> track;
    double trackRate=48000, position=0, rate=48000, expectedInputRate=0;
    float monitorGain=0, masterGain=0, musicGain=0;
    double testPhase=0;
    int testRemaining=0;
    bool lowLatencyMode=false;
    void audioDeviceListChanged() override { params.monitor=false; fault=true; devicesChanged=true; }
public:
    Parameters params;
    ClockBridge bridge;
    Recorder recorder;
    std::atomic<bool> playing{false},fault{false}, testRequested{false};
    std::atomic<bool> devicesChanged{false};
    std::atomic<float> inputPeak{0},musicPeak{0},outputPeak{0};
    std::atomic<double> seconds{0},seek{-1},callbackLoad{0};
    std::atomic<uint64_t> clipped{0};
    double duration=0;
    juce::String trackName;
    AudioEngine();
    ~AudioEngine();
    juce::StringArray inputs(),outputs();
    void scan();
    juce::String connect(const juce::String&,const juce::String&,double,int,bool lowLatency=false);
    void close();
    bool connected() const { return output && output->isPlaying() && input && input->isPlaying() && !fault.load(); }
    juce::String diagnostics() const;
    juce::String loadTrack(const juce::File&);
    juce::String record(const juce::File&,bool);
    void stopRecording();
    void prepareOffline(double sr) { close(); rate=sr; dsp.prepare(sr); bridge.prepare(sr,sr,256); params.monitor=true; params.master=0.5f; }
    void render(const float* const*,int,float* const*,int,int,bool) noexcept;
};
}
