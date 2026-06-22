#include "WaveformDisplay.h"
#include "ColorPalette.h"

WaveformDisplay::WaveformDisplay(SPSCSlotQueue<WaveformSnapshot>& queue)
    : snapshotQueue(queue)
{
    // Enable VBlank Attachment
    vblankAttachment = std::make_unique<juce::VBlankAttachment>(this, [this]() { vblankUpdate(); });
    
    // We do not need the component to buffer itself as we handle cachedImage
    setOpaque(true);
}

WaveformDisplay::~WaveformDisplay()
{
}

void WaveformDisplay::setColorPaletteIndex(int index)
{
    if (currentPaletteIdx != index)
    {
        currentPaletteIdx = std::clamp(index, 0, 9);
        repaint();
    }
}

void WaveformDisplay::vblankUpdate()
{
    bool updated = false;
    
    // Pull the latest snapshot from the queue
    while (auto* slot = snapshotQueue.getReadSlot())
    {
        // For a waveform display, we only care about the most recent one.
        // We can just copy it.
        currentSnapshot = *slot;
        hasData = true;
        updated = true;
        
        snapshotQueue.commitRead();
    }
    
    if (updated)
    {
        // Trigger a repaint. VBlank ensures this is synced with display rate.
        repaint();
    }
}

void WaveformDisplay::resized()
{
    // Recreate the cached image (ARGB for alpha blending, or RGB if fully opaque)
    cachedImage = juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
    
    // IMPORTANT: setBackupEnabled(false) ensures JUCE doesn't keep a software copy
    // when using hardware acceleration (Direct2D / Metal), drastically saving memory bandwidth.
    // It is basically a volatile texture.
}

void WaveformDisplay::paint(juce::Graphics& g)
{
    const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
    g.fillAll(pal.bg); // Dark background
    
    if (!hasData || currentSnapshot.dryWaveform.empty())
        return;
        
    auto leftHalf = getLocalBounds().removeFromLeft(getWidth() / 2);
    auto rightHalf = getLocalBounds().removeFromRight(getWidth() / 2);

    // --------------------------------------------------------------------
    // グリッド線（方眼紙状）の描画
    // --------------------------------------------------------------------
    g.setColour(pal.grid);
    
    int numHorzLines = 8;
    for (int i = 1; i < numHorzLines; ++i) {
        float y = getHeight() * (float)i / numHorzLines;
        g.drawHorizontalLine((int)y, 0.0f, (float)getWidth());
    }
    
    int numVertLines = 10;
    for (int i = 1; i < numVertLines; ++i) {
        float x1 = leftHalf.getWidth() * (float)i / numVertLines;
        g.drawVerticalLine((int)x1, 0.0f, (float)getHeight());
        
        float x2 = rightHalf.getX() + rightHalf.getWidth() * (float)i / numVertLines;
        g.drawVerticalLine((int)x2, 0.0f, (float)getHeight());
    }
    
    // センターライン（ゼロクロス）と分割線
    g.setColour(pal.grid.withMultipliedAlpha(2.0f));
    g.drawHorizontalLine(getHeight() / 2, 0.0f, (float)getWidth());
    g.drawVerticalLine(getWidth() / 2, 0.0f, (float)getHeight());
    
    // Y-axis auto-scaling (Independent for Dry and Wet to see shapes clearly even if volume drops)
    float maxDry = 0.01f;
    float maxWet = 0.01f;
    for (float v : currentSnapshot.dryWaveform) maxDry = std::max(maxDry, std::abs(v));
    for (float v : currentSnapshot.wetWaveform) maxWet = std::max(maxWet, std::abs(v));
    float scaleDry = 1.0f / maxDry;
    float scaleWet = 1.0f / maxWet;
    
    g.saveState();
    g.reduceClipRegion(leftHalf);
    // Draw Dry (Pre-filter) in palette text color
    drawWaveform(g, currentSnapshot.dryWaveform, pal.text, leftHalf, scaleDry);
    g.restoreState();
    
    g.saveState();
    g.reduceClipRegion(rightHalf);
    // Draw Wet (Post-filter) in palette highlight color
    drawWaveform(g, currentSnapshot.wetWaveform, pal.lowcut, rightHalf, scaleWet);
    g.restoreState();
    
    // Labels
    g.setColour(pal.text);
    g.setFont(12.0f);
    g.drawText("Original (Dry)", leftHalf.withTop(10).withHeight(20), juce::Justification::centred);
    g.drawText("Zero-Phase (Wet)", rightHalf.withTop(10).withHeight(20), juce::Justification::centred);
}

void WaveformDisplay::drawWaveform(juce::Graphics& g, const std::vector<float>& data, juce::Colour color, juce::Rectangle<int> bounds, float scale)
{
    g.setColour(color);
    
    int numSamples = (int)data.size();
    if (numSamples == 0) return;
    
    int visibleSamples = std::max(10, static_cast<int>(numSamples / zoomLevel));
    int startIndex = static_cast<int>(panOffset * (numSamples - visibleSamples));
    
    float width = (float)bounds.getWidth();
    float height = (float)bounds.getHeight();
    float xOffset = (float)bounds.getX();
    float yCenter = bounds.getY() + height * 0.5f;
    
    // Avoid juce::Path. Allocate an array of points on the stack or a small vector
    std::vector<juce::Point<float>> points;
    points.reserve(static_cast<size_t>(width)); // We only need 1 point per pixel column for rendering
    
    float samplesPerPixel = (float)visibleSamples / width;
    
    // Simple decimation for drawing
    for (int x = 0; x < width; ++x)
    {
        int startIdx = startIndex + (int)(x * samplesPerPixel);
        int endIdx = startIndex + (int)((x + 1) * samplesPerPixel);
        if (startIdx < 0) startIdx = 0;
        if (endIdx > numSamples) endIdx = numSamples;
        
        // Find min/max in this pixel bucket
        float minVal = 0.0f;
        float maxVal = 0.0f;
        
        if (startIdx < endIdx)
        {
            minVal = data[startIdx];
            maxVal = data[startIdx];
            for (int i = startIdx + 1; i < endIdx; ++i)
            {
                if (data[i] < minVal) minVal = data[i];
                if (data[i] > maxVal) maxVal = data[i];
            }
        }
        
        float y1 = yCenter - (minVal * scale) * (height * 0.45f);
        float y2 = yCenter - (maxVal * scale) * (height * 0.45f);
        
        points.push_back({ xOffset + (float)x, y1 });
        points.push_back({ xOffset + (float)x, y2 });
    }
    
    // Draw as lines (faster than juce::Path strokes)
    for (size_t i = 1; i < points.size(); ++i)
    {
        g.drawLine(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y, 1.5f);
    }
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& event)
{
    dragStartPan = panOffset;
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& event)
{
    float dx = static_cast<float>(event.getDistanceFromDragStartX());
    float panShift = -dx / getWidth(); // Drag left means view moves right
    panOffset = std::clamp(dragStartPan + panShift / zoomLevel, 0.0f, 1.0f);
    repaint();
}

void WaveformDisplay::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    float zFactor = 1.0f + wheel.deltaY * 2.0f;
    zoomLevel = std::clamp(zoomLevel * zFactor, 1.0f, 100.0f);
    repaint();
}
