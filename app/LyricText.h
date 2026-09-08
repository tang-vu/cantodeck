#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

inline std::optional<double> lyricTimestamp(const juce::String& tag)
{
    const auto parts = juce::StringArray::fromTokens(tag, ":", "");
    if (parts.size() != 2 || parts[0].isEmpty() || parts[0].length() > 4 ||
        !parts[0].containsOnly("0123456789"))
        return {};
    const auto seconds = juce::StringArray::fromTokens(parts[1], ".", "");
    if (seconds.size() < 1 || seconds.size() > 2 || seconds[0].isEmpty() || seconds[0].length() > 2 ||
        !seconds[0].containsOnly("0123456789") || seconds[0].getIntValue() >= 60)
        return {};
    if (parts[1].endsWithChar('.') || (seconds.size() == 2 &&
        (seconds[1].isEmpty() || seconds[1].length() > 3 || !seconds[1].containsOnly("0123456789"))))
        return {};
    return parts[0].getIntValue() * 60.0 + parts[1].getDoubleValue();
}

// Read-only multiline text retains scroll position when the displayed lyric has
// not changed. Keyboard F11/Esc are forwarded to the owning lyrics window.
class LyricText final : public juce::TextEditor
{
  public:
    std::function<bool(const juce::KeyPress&)> windowKey;
    LyricText()
    {
        setMultiLine(true, false);
        setReadOnly(true);
        setCaretVisible(false);
        setScrollbarsShown(true);
        setJustification(juce::Justification::centredTop);
        setColour(backgroundColourId, juce::Colour(0xff141922));
        setColour(outlineColourId, juce::Colours::transparentBlack);
        setColour(textColourId, juce::Colours::white);
        setName("Lyrics / Loi bai hat");
    }
    void display(const juce::String& value)
    {
        if (getText() != value)
        {
            setText(value, false);
            setCaretPosition(0);
        }
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (windowKey && windowKey(key))
            return true;
        return TextEditor::keyPressed(key);
    }
};
