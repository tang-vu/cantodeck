#pragma once

namespace canto
{
// Distinguish our synchronous output-callback suspension from device loss.
// This policy never enables monitoring. Input stops always require muting.
class MonitoringStopPolicy
{
    inline static thread_local const MonitoringStopPolicy* expected = nullptr;
  public:
    class OutputSuspension
    {
        MonitoringStopPolicy& policy;
        const MonitoringStopPolicy* previous;
      public:
        explicit OutputSuspension(MonitoringStopPolicy& p) : policy(p), previous(expected)
        { expected = &policy; }
        ~OutputSuspension() { expected = previous; }
        OutputSuspension(const OutputSuspension&) = delete;
        OutputSuspension& operator=(const OutputSuspension&) = delete;
    };
    bool mustMute(bool isInput) const noexcept
    { return isInput || expected != this; }
};
}
