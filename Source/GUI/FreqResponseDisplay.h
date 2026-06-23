#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

class AnalyzerDSP;
class HighPrecisionEQAudioProcessor;
class HighPrecisionEQAudioProcessorEditor;

class FreqResponseDisplay : public juce::Component
{
public:
    FreqResponseDisplay();
    ~FreqResponseDisplay() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    struct BellParam {
        double freq = 1000.0;
        double gain = 0.0;
        double q = 1.0;
        bool active = false;
    };

    void updateParameters(double cutoffHz, int order, double gainDb, bool lowcutEnable,
                          double highCutFreq, int highCutOrder, bool highCutEnable,
                          double sampleRate, const std::array<BellParam, 4>& bells);

    void setColorPaletteIndex(int index);
    void setAnalyzer(AnalyzerDSP* analyzer);
    void setProcessorAndEditor(HighPrecisionEQAudioProcessor* proc, HighPrecisionEQAudioProcessorEditor* ed);
    void setSelectedBand(int bandIdx);

    float getMinF() const noexcept { return currentMinF; }
    float getMaxF() const noexcept { return currentMaxF; }
    HighPrecisionEQAudioProcessor* getProcessor() const noexcept { return processor; }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    AnalyzerDSP* analyzer = nullptr;
    HighPrecisionEQAudioProcessor* processor = nullptr;
    HighPrecisionEQAudioProcessorEditor* editor = nullptr;

    int activeDragBand = -1; // -1:なし, 0:LowCut, 1:HighCut, 2..5:Bell1..4
    int selectedBandIdx = 0;

    std::unique_ptr<juce::VBlankAttachment> vblankAttachment;
    void vblankUpdate();

    double currentCutoffHz = 80.0;
    int    currentOrder = 2;
    double currentGainDb     = 0.0;
    double currentSampleRate = 44100.0;
    bool   currentLowcutEnable = true;
    double currentHighCutFreq = 20000.0;
    int    currentHighCutOrder = 2;
    bool   currentHighCutEnable = false;
    std::array<BellParam, 4> bellParams;

    int currentPaletteIdx = 0;

    juce::Path cachedResponsePath;
    juce::ColourGradient cachedLineGrad;
    juce::ColourGradient cachedFillGrad;
    bool pathNeedsRecalculation = true;

    struct PrecomputedFreq {
        float f;
        double cosw, sinw, cos2w, sin2w;
    };
    std::vector<PrecomputedFreq> precomputedFreqs;
    void precomputeFrequencies();

    float logFToX(float f) const;
    float xToLogF(float x) const;
    float gainToY(float gainDecibels) const;
    float yToGain(float y) const;
    float analyzerGainToY(float gainDecibels) const;

    void drawEQPoints(juce::Graphics& g);

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;

    float currentMinF = 10.0f, currentMaxF = 20000.0f;
    float currentMinDb = -12.0f, currentMaxDb = 12.0f;
    float analyzerGainOffsetDb = 0.0f;
    uint64_t lastSeenUpdateCount = 0;

    float dragStartMinF = 10.0f, dragStartMaxF = 20000.0f;
    float dragStartMinDb = -12.0f;

    juce::TextButton zoomInXBtn{ "H+" }, zoomOutXBtn{ "H-" }, zoomInYBtn{ "V+" }, zoomOutYBtn{ "V-" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreqResponseDisplay)
};
