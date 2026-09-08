#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace canto
{
struct EndpointDescription
{
    std::string id, name;
    bool input = false;
};
struct BackendConfiguration
{
    std::string input, output;
    double sampleRate = 48000;
    int periodFrames = 128;
    bool lowLatency = true;
};
struct BackendFormat
{
    double inputRate = 0, outputRate = 0;
    int inputPeriod = 0, outputPeriod = 0, inputCapacity = 0, outputCapacity = 0;
    int outputQueueTarget = 0;
    double inputDriverSeconds = 0, outputDriverSeconds = 0;
    bool inputLowLatency = false, outputLowLatency = false, rawInput = false;
};
struct BackendCallbacks
{
    void* context = nullptr;
    void (*prepare)(void*, const BackendFormat&) = nullptr;
    void (*process)(void*, const float* const*, int, float* const*, int, int, bool) = nullptr;
    void (*failed)(void*, uint32_t) = nullptr;
};
struct BackendCounters
{
    uint64_t capturePackets = 0, renderCallbacks = 0, captureDiscontinuities = 0, zeroPaddingEvents = 0;
    uint32_t lastError = 0;
    uint64_t initialCaptureDiscontinuities = 0, captureTimestampErrors = 0;
    double maxCaptureServiceGapMs = 0, maxRenderServiceGapMs = 0;
};
class AudioBackend
{
  public:
    virtual ~AudioBackend() = default;
    virtual std::vector<EndpointDescription> enumerate() = 0;
    virtual std::string open(const BackendConfiguration&, BackendCallbacks) = 0;
    virtual void close() = 0;
    // Control thread only. Waits for an in-flight callback to leave, never blocks
    // the processing thread. Capture is drained and output is silent while paused.
    virtual void pause(bool) = 0;
    virtual bool isRunning() const = 0;
    virtual BackendFormat format() const = 0;
    virtual BackendCounters counters() const = 0;
};
} // namespace canto
