#pragma once
#include "engine/Core.h"
#include <juce_gui_basics/juce_gui_basics.h>

class EqPanel final : public juce::Component, private juce::Timer
{
    canto::Parameters& parameters;
    std::array<juce::Slider, 9> controls;
    std::array<juce::Label, 9> labels;
    juce::Label status;
    void timerCallback() override
    {
        status.setText(parameters.transparent.load()
            ? "Giong goc / Dry voice ON: EQ bypassed"
            : parameters.eq.load() ? "EQ ON | Hz / dB / Q | higher Q = narrower band"
                                   : "EQ OFF: enable EQ in Advanced", juce::dontSendNotification);
        for (size_t i = 0; i < controls.size(); ++i)
        {
            const auto& band = parameters.eqBands[i / 3];
            const float value = i % 3 == 0 ? band.frequency.load() : i % 3 == 1 ? band.gainDb.load() : band.q.load();
            if (!controls[i].isMouseButtonDown() && !controls[i].hasKeyboardFocus(true))
                controls[i].setValue(value, juce::dontSendNotification);
        }
    }
  public:
    explicit EqPanel(canto::Parameters& p) : parameters(p)
    {
        addAndMakeVisible(status);
        for (size_t i = 0; i < controls.size(); ++i)
        {
            auto& slider = controls[i];
            const int kind = int(i % 3);
            slider.setRange(kind == 0 ? 40 : kind == 1 ? -12 : 0.2,
                            kind == 0 ? 16000 : kind == 1 ? 12 : 8, kind == 0 ? 1 : 0.01);
            if (kind == 0)
                slider.setSkewFactorFromMidPoint(1000);
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 24);
            slider.setTextValueSuffix(kind == 0 ? " Hz" : kind == 1 ? " dB" : " Q");
            slider.onValueChange = [this, i, kind]
            {
                auto& band = parameters.eqBands[i / 3];
                auto& target = kind == 0 ? band.frequency : kind == 1 ? band.gainDb : band.q;
                target = float(controls[i].getValue());
            };
            labels[i].setText("Band " + juce::String(int(i / 3) + 1) +
                (kind == 0 ? " - Frequency" : kind == 1 ? " - Gain" : " - Q"), juce::dontSendNotification);
            slider.setName(labels[i].getText());
            addAndMakeVisible(slider);
            addAndMakeVisible(labels[i]);
        }
        setSize(690, 350);
        timerCallback();
        startTimerHz(10);
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff141922)); }
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(16);
        status.setBounds(bounds.removeFromTop(32));
        const int width = bounds.getWidth() / 3, height = bounds.getHeight() / 3;
        for (size_t band = 0; band < 3; ++band)
        {
            auto row = bounds.removeFromTop(height);
            for (size_t kind = 0; kind < 3; ++kind)
            {
                auto cell = row.removeFromLeft(width).reduced(5);
                labels[band * 3 + kind].setBounds(cell.removeFromTop(22));
                controls[band * 3 + kind].setBounds(cell);
            }
        }
    }
};
class EqWindow final : public juce::DocumentWindow
{
  public:
    explicit EqWindow(canto::Parameters& parameters)
        : DocumentWindow("Parametric EQ - 3 bands", juce::Colour(0xff141922), closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new EqPanel(parameters), true);
        setResizable(true, false);
        setResizeLimits(620, 340, 1200, 700);
        centreWithSize(690, 350);
    }
    void closeButtonPressed() override { setVisible(false); }
};
