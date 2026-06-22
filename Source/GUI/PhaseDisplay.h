#pragma once
#include <JuceHeader.h>
#include <array>
#include "FreqResponseDisplay.h"

class AnalyzerDSP;

class PhaseDisplay : public juce::Component
{
public:
    PhaseDisplay();
    ~PhaseDisplay() override;

    void paint(juce::Graphics& g) override;
    
    void updateParameters(double cutoffHz, int order, double gainDb, bool lowcutEnable,
                          double highCutFreq, int highCutOrder, bool highCutEnable,
                          double sampleRate, const std::array<FreqResponseDisplay::BellParam, 4>& bells);

    void setColorPaletteIndex(int index);
    void setAnalyzer(AnalyzerDSP* analyzer);
    void setResponseDisplay(FreqResponseDisplay* display);

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    AnalyzerDSP* analyzer = nullptr;
    FreqResponseDisplay* responseDisplay = nullptr;

    std::unique_ptr<juce::VBlankAttachment> vblankAttachment;
    void vblankUpdate();

    double currentCutoff = 80.0;
    int    currentOrder = 2;
    double currentSampleRate = 48000.0;
    double currentGainDb = -10.0;
    bool   currentLowcutEnable = true;
    double currentHighCutFreq = 20000.0;
    int    currentHighCutOrder = 2;
    bool   currentHighCutEnable = false;
    std::array<FreqResponseDisplay::BellParam, 4> bellParams;

    int currentPaletteIdx = 0;
    float phaseZoomFactor = 1.0f;

    void drawPhaseCurve(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour color);
    void drawAnalyzerSpectrum(juce::Graphics& g, juce::Rectangle<int> bounds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseDisplay)
};
