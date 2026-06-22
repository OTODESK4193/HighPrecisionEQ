#pragma once
#include <JuceHeader.h>
#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"
#include "GUI/ArcDial.h"
#include "GUI/FreqResponseDisplay.h"
#include "GUI/WaveformDisplay.h"
#include "GUI/PhaseDisplay.h"

// ============================================================================
//  FineSlider.h
//  Ctrlキー押下時に微調整（感度低下）を有効にするカスタムスライダー
// ============================================================================
class FineSlider : public juce::Slider
{
public:
    FineSlider() = default;
    ~FineSlider() override = default;
    
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isCtrlDown())
            setMouseDragSensitivity(2500); // 10x slower drag speed for fine tuning
        else
            setMouseDragSensitivity(250);
            
        juce::Slider::mouseDown(e);
    }
    
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.mods.isCtrlDown())
            setMouseDragSensitivity(2500);
        else
            setMouseDragSensitivity(250);
            
        juce::Slider::mouseDrag(e);
    }
};

// ============================================================================
//  PluginEditor.h
//  LowCut Police – GUI エディタ
// ============================================================================

class LowCutPoliceAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit LowCutPoliceAudioProcessorEditor(LowCutPoliceAudioProcessor&);
    ~LowCutPoliceAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    enum class SelectedBand { LowCut, HighCut, Bell1, Bell2, Bell3, Bell4 };
    void selectBand(SelectedBand band);

private:
    juce::OpenGLContext openGLContext;
    LowCutPoliceAudioProcessor& processorRef;

    // GUI コンポーネント
    FreqResponseDisplay freqDisplay;
    WaveformDisplay     waveformDisplay;
    PhaseDisplay        phaseDisplay;
    
    enum class AnalyzeMode {
        Normal,
        Waveform,
        Phase
    };
    AnalyzeMode         analyzeMode = AnalyzeMode::Normal;
    juce::TextButton    analyzeButton{ "Analyze" };

    ArcDialLookAndFeel  arcLAF;
    FineSlider          cutoffSlider;
    juce::Label         cutoffLabel;
    FineSlider          gainSlider;
    juce::Label         gainLabel;
    FineSlider          slopeSlider;
    juce::Label         slopeLabel;
    FineSlider          qSlider;
    juce::Label         qLabel;

    SelectedBand        currentBand = SelectedBand::LowCut;
    int                 currentPaletteIdx = 0;

    juce::TextButton bandButtons[6]; // 0:LowCut, 1:HighCut, 2:Bell1, ...
    juce::TextButton enableButtons[6]; // ON/OFFボタン

    juce::TextButton diffButton{ "Diff" };
    juce::TextButton bypassButton{ "Bypass" };
    juce::TextButton colorButton{ "Color" };

    // 各種アタッチメント
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> cutoffAttachment;
    std::unique_ptr<SliderAttachment> slopeAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment;
    std::unique_ptr<SliderAttachment> qAttachment;
    
    std::unique_ptr<ButtonAttachment> enableAttachments[6];

    std::unique_ptr<ButtonAttachment> diffAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    void updateAttachments();
    void updateComponentColors();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LowCutPoliceAudioProcessorEditor)
};
