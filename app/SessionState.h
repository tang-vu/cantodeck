#pragma once
#include <juce_core/juce_core.h>

inline bool supportedSessionState(const juce::var& value)
{
    if (!value.isObject())
        return false;
    const auto version = value["version"];
    // No string/bool coercion or truncation of fractional/future versions.
    return (version.isInt() || version.isInt64() || version.isDouble()) && double(version) == 1.0;
}

inline juce::var recoverSessionState(const juce::var& primary, const juce::var& backup)
{
    if (supportedSessionState(primary))
        return primary;
    return supportedSessionState(backup) ? backup : juce::var{};
}
