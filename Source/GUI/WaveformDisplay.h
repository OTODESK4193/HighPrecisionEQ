#pragma once
#include <JuceHeader.h>
#include "../DSP/SPSCQueue.h"

class WaveformDisplay : public juce::Component
{
public:
    WaveformDisplay(SPSCSlotQueue<WaveformSnapshot>& queue);
    ~WaveformDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    void setColorPaletteIndex(int index);

private:
    int currentPaletteIdx = 0;
    SPSCSlotQueue<WaveformSnapshot>& snapshotQueue;
    
    // JUCE 8 VBlank synchronization
    std::unique_ptr<juce::VBlankAttachment> vblankAttachment;

    void vblankUpdate();

    // Data buffers
    WaveformSnapshot currentSnapshot;
    bool hasData = false;
    
    // For drawing optimization
    juce::Image cachedImage;

    float zoomLevel = 8.0f;
    float panOffset = 0.03f;
    float dragStartPan = 0.03f;

    // Draw a single waveform using drawPolyline (no juce::Path allocations)
    void drawWaveform(juce::Graphics& g, const std::vector<float>& data, juce::Colour color, juce::Rectangle<int> bounds, float scale);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};
