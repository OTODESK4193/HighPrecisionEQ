#pragma once
#include <JuceHeader.h>

// ============================================================================
//  FreqResponseDisplay.h
//  周波数応答カーブの描画コンポーネント
// ============================================================================

class AnalyzerDSP;
class LowCutPoliceAudioProcessor;
class LowCutPoliceAudioProcessorEditor;

class FreqResponseDisplay : public juce::Component
{
public:
    FreqResponseDisplay();
    ~FreqResponseDisplay() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// パラメータを更新して再描画
    struct BellParam {
        double freq = 1000.0;
        double gain = 0.0;
        double q = 1.0;
        bool active = false;
    };

    void updateParameters(double cutoffHz, int order, double gainDb, double sampleRate, const std::array<BellParam, 4>& bells, bool lowcutEnable);
    void setColorPaletteIndex(int index);

    void setAnalyzer(AnalyzerDSP* analyzer);
    void setProcessorAndEditor(LowCutPoliceAudioProcessor* proc, LowCutPoliceAudioProcessorEditor* ed);
    void setSelectedBand(int bandIdx);
    float getMinF() const noexcept { return currentMinF; }
    float getMaxF() const noexcept { return currentMaxF; }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    AnalyzerDSP* analyzer = nullptr;
    LowCutPoliceAudioProcessor* processor = nullptr;
    LowCutPoliceAudioProcessorEditor* editor = nullptr;
    int activeDragBand = -1; // -1: none, 0: LowCut, 1..4: Bell 1..4
    int selectedBandIdx = 0; // 0: LowCut, 1..4: Bell 1..4
    
    std::unique_ptr<juce::VBlankAttachment> vblankAttachment;
    void vblankUpdate();

    double currentCutoffHz = 80.0;
    int    currentOrder = 2;
    double currentGainDb     = 0.0;
    double currentSampleRate = 44100.0;
    bool currentLowcutEnable = true;
    std::array<BellParam, 4> bellParams;
    int currentPaletteIdx = 0;
    
    // キャッシュメンバー
    juce::Path cachedResponsePath;
    juce::ColourGradient cachedLineGrad;
    juce::ColourGradient cachedFillGrad;
    bool pathNeedsRecalculation = true;

    struct PrecomputedFreq {
        float f;
        double cosw;
        double sinw;
        double cos2w;
        double sin2w;
    };
    std::vector<PrecomputedFreq> precomputedFreqs;
    void precomputeFrequencies();

    // ヘルパー: 周波数からX座標、X座標から周波数への変換
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

    float currentMinF = 10.0f;
    float currentMaxF = 20000.0f;
    float currentMinDb = -12.0f;
    float analyzerGainOffsetDb = 0.0f;
    
    float dragStartMinF = 10.0f;
    float dragStartMaxF = 20000.0f;
    float dragStartMinDb = -12.0f;

    juce::TextButton zoomInXBtn{ "H+" };
    juce::TextButton zoomOutXBtn{ "H-" };
    juce::TextButton zoomInYBtn{ "V+" };
    juce::TextButton zoomOutYBtn{ "V-" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreqResponseDisplay)
};
