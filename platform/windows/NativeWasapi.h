#pragma once
#include "engine/audio/AudioBackend.h"
#include <memory>

namespace canto
{
class NativeWasapi final : public AudioBackend
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    NativeWasapi();
    ~NativeWasapi() override;
    std::vector<EndpointDescription> enumerate() override;
    std::string open(const BackendConfiguration&, BackendCallbacks) override;
    void close() override;
    void pause(bool) override;
    bool isRunning() const override;
    BackendFormat format() const override;
    BackendCounters counters() const override;
};
} // namespace canto
