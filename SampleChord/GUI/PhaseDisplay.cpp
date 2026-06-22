#include "PhaseDisplay.h"
#include "../DSP/AnalyzerDSP.h"
#include "FreqResponseDisplay.h"
#include "ColorPalette.h"
#include <complex>

PhaseDisplay::PhaseDisplay()
{
    setOpaque(true);
}

PhaseDisplay::~PhaseDisplay()
{
}

void PhaseDisplay::setAnalyzer(AnalyzerDSP* a)
{
    analyzer = a;
    if (analyzer != nullptr)
        vblankAttachment = std::make_unique<juce::VBlankAttachment>(this, [this]() { vblankUpdate(); });
    else
        vblankAttachment.reset();
}

void PhaseDisplay::setResponseDisplay(FreqResponseDisplay* r)
{
    responseDisplay = r;
}

void PhaseDisplay::vblankUpdate()
{
    if (analyzer != nullptr)
    {
        std::vector<float> energies = analyzer->getEnergies();
        float maxEnergy = -180.0f;
        for (float e : energies)
        {
            if (e > maxEnergy) maxEnergy = e;
        }
        if (maxEnergy > -89.5f)
        {
            repaint();
        }
    }
}

void PhaseDisplay::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    float zoomDelta = wheel.deltaY * 0.15f;
    phaseZoomFactor = std::clamp(phaseZoomFactor + zoomDelta, 0.2f, 8.0f);
    repaint();
}

void PhaseDisplay::updateParameters(double cutoffHz, int order, double sampleRate, double gainDb, const std::array<FreqResponseDisplay::BellParam, 4>& bells, bool lowcutEnable)
{
    currentCutoff = cutoffHz;
    currentOrder = order;
    currentSampleRate = sampleRate;
    currentGainDb = gainDb;
    bellParams = bells;
    currentLowcutEnable = lowcutEnable;
    repaint();
}

void PhaseDisplay::setColorPaletteIndex(int index)
{
    if (currentPaletteIdx != index)
    {
        currentPaletteIdx = std::clamp(index, 0, 9);
        repaint();
    }
}

void PhaseDisplay::paint(juce::Graphics& g)
{
    const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
    g.fillAll(pal.bg);
    
    if (currentSampleRate <= 0.0) return;
    
    auto leftHalf = getLocalBounds().removeFromLeft(getWidth() / 2);
    auto rightHalf = getLocalBounds().removeFromRight(getWidth() / 2);
 
    // 1. Draw WET Analyzer Spectrum Overlay (Background)
    g.saveState();
    g.reduceClipRegion(leftHalf);
    drawAnalyzerSpectrum(g, leftHalf);
    g.restoreState();
 
    g.saveState();
    g.reduceClipRegion(rightHalf);
    drawAnalyzerSpectrum(g, rightHalf);
    g.restoreState();
 
    // 2. Draw Grid Lines (Dynamic step based on zoom)
    float maxDeg = 180.0f / phaseZoomFactor;
    float degStep = 90.0f;
    if (maxDeg <= 30.0f)       degStep = 10.0f;
    else if (maxDeg <= 60.0f)  degStep = 15.0f;
    else if (maxDeg <= 120.0f) degStep = 30.0f;
    else if (maxDeg <= 240.0f) degStep = 45.0f;
 
    float height = (float)getHeight();
    float yCenter = height * 0.5f;
 
    for (float deg = 0.0f; deg <= 180.0f; deg += degStep)
    {
        float rad = deg * juce::MathConstants<float>::pi / 180.0f;
        float yOffset = (rad / juce::MathConstants<float>::pi) * phaseZoomFactor * (height * 0.45f);
        
        // Positive angle (+deg)
        float yPlus = yCenter - yOffset;
        if (yPlus >= 0.0f && yPlus <= height)
        {
            g.setColour(deg == 0.0f ? pal.grid.withMultipliedAlpha(2.0f) : pal.grid);
            g.drawHorizontalLine((int)yPlus, 0.0f, (float)getWidth());
            
            g.setColour(pal.text);
            g.setFont(9.0f);
            g.drawText(juce::String((int)deg) + " deg", 5, (int)yPlus - 11, 50, 10, juce::Justification::left);
        }
        
        // Negative angle (-deg)
        if (deg > 0.0f)
        {
            float yMinus = yCenter + yOffset;
            if (yMinus >= 0.0f && yMinus <= height)
            {
                g.setColour(pal.grid);
                g.drawHorizontalLine((int)yMinus, 0.0f, (float)getWidth());
                
                g.setColour(pal.text);
                g.setFont(9.0f);
                g.drawText(juce::String(-(int)deg) + " deg", 5, (int)yMinus - 11, 50, 10, juce::Justification::left);
            }
        }
    }
 
    // Border line between Minimum and Zero Phase
    g.setColour(pal.grid.withMultipliedAlpha(2.0f));
    g.drawVerticalLine(getWidth() / 2, 0.0f, height);
    
    // 3. Draw IIR Phase Curve (Minimum Phase) on Left
    g.saveState();
    g.reduceClipRegion(leftHalf);
    drawPhaseCurve(g, leftHalf, false, pal.text);
    g.restoreState();
    
    // 4. Draw Zero Phase Curve on Right
    g.saveState();
    g.reduceClipRegion(rightHalf);
    drawPhaseCurve(g, rightHalf, true, pal.lowcut);
    g.restoreState();
    
    // Labels
    g.setColour(pal.text);
    g.setFont(12.0f);
    g.drawText("Standard IIR Phase", leftHalf.withTop(10).withHeight(20), juce::Justification::centred);
    g.drawText("Zero-Phase (Ideal)", rightHalf.withTop(10).withHeight(20), juce::Justification::centred);
}

void PhaseDisplay::drawPhaseCurve(juce::Graphics& g, juce::Rectangle<int> bounds, bool isZeroPhase, juce::Colour color)
{
    g.setColour(color);
    
    float width = (float)bounds.getWidth();
    float height = (float)bounds.getHeight();
    float xOffset = (float)bounds.getX();
    float yCenter = bounds.getY() + height * 0.5f;
    
    std::vector<juce::Point<float>> points;
    points.reserve(static_cast<size_t>(width));
    
    auto coefs = SOSCoefficients::computeHighPass(currentCutoff, currentSampleRate, currentOrder, currentGainDb);
    
    // Sync frequency boundaries from FreqResponseDisplay
    float minF = responseDisplay != nullptr ? responseDisplay->getMinF() : 20.0f;
    float maxF = responseDisplay != nullptr ? responseDisplay->getMaxF() : 20000.0f;
    
    for (int x = 0; x < width; ++x)
    {
        float freqRatio = x / width;
        float logMin = std::log10(minF);
        float logMax = std::log10(maxF);
        float logF = logMin + freqRatio * (logMax - logMin);
        double freq = std::pow(10.0f, logF);
        
        if (freq >= currentSampleRate / 2.0) break;
        
        float phaseAngle = 0.0f;
        double w = juce::MathConstants<double>::twoPi * freq / currentSampleRate;
        std::complex<double> z(std::cos(w), -std::sin(w));
        
        std::complex<double> H(1.0, 0.0);
        
        // 1. LowCut Phase (only on Standard Left view when enabled)
        if (!isZeroPhase && currentLowcutEnable)
        {
            for (const auto& c : coefs) {
                std::complex<double> num = c.b0 + c.b1 * z + c.b2 * z * z;
                std::complex<double> den = 1.0  + c.a1 * z + c.a2 * z * z;
                H *= (num / den);
            }
        }
        
        // 2. Bell EQ Phase (always Standard Phase because Bell EQ is Minimum Phase in this DSP)
        for (const auto& bell : bellParams)
        {
            if (bell.active && std::abs(bell.gain) > 0.01)
            {
                double A = std::pow(10.0, bell.gain / 40.0);
                
                // Clamp bell frequency slightly below Nyquist to keep filter stable
                double bellFreq = std::clamp(bell.freq, 20.0, currentSampleRate * 0.49);
                double w0 = juce::MathConstants<double>::twoPi * bellFreq / currentSampleRate;
                double alpha = std::sin(w0) / (2.0 * bell.q);
                
                double a0 = 1.0 + alpha / A;
                double b0 = (1.0 + alpha * A) / a0;
                double b1 = (-2.0 * std::cos(w0)) / a0;
                double b2 = (1.0 - alpha * A) / a0;
                double a1 = (-2.0 * std::cos(w0)) / a0;
                double a2 = (1.0 - alpha / A) / a0;
                
                std::complex<double> num = b0 + b1 * z + b2 * z * z;
                std::complex<double> den = 1.0 + a1 * z + a2 * z * z;
                H *= (num / den);
            }
        }
        
        phaseAngle = static_cast<float>(std::arg(H)); // radians between -pi and pi
        
        float phaseNorm = phaseAngle / juce::MathConstants<float>::pi; // -1 to 1
        float y = yCenter - (phaseNorm * phaseZoomFactor) * (height * 0.45f);
        
        // Clip values to bounds
        y = std::clamp(y, (float)bounds.getY(), (float)bounds.getBottom());
        
        points.push_back({ xOffset + x, y });
    }
    
    for (size_t i = 1; i < points.size(); ++i) {
        float y1 = points[i-1].y;
        float y2 = points[i].y;
        float top = (float)bounds.getY();
        float bottom = (float)bounds.getBottom();
        
        // Prevent drawing flat lines at the clipping edges
        if ((y1 <= top + 0.1f && y2 <= top + 0.1f) || (y1 >= bottom - 0.1f && y2 >= bottom - 0.1f))
            continue;
            
        g.drawLine(points[i-1].x, y1, points[i].x, y2, 2.0f);
    }
}

void PhaseDisplay::drawAnalyzerSpectrum(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (analyzer == nullptr) return;
    
    std::vector<float> energies = analyzer->getEnergies();
    float w = (float)bounds.getWidth();
    float h = (float)bounds.getHeight();
    float xOffset = (float)bounds.getX();
    
    float minF = responseDisplay != nullptr ? responseDisplay->getMinF() : 20.0f;
    float maxF = responseDisplay != nullptr ? responseDisplay->getMaxF() : 20000.0f;
    
    juce::Path fillPath;
    bool firstPoint = true;
    
    double fmin = 10.0;
    double fmax = 24000.0;
    
    float lastX = 0;
    float lastY = h;
    
    for (int i = 0; i < AnalyzerDSP::NumBands; ++i)
    {
        double fc = fmin * std::pow(fmax / fmin, static_cast<double>(i) / (AnalyzerDSP::NumBands - 1));
        
        float logMin = std::log10(minF);
        float logMax = std::log10(maxF);
        float logF = std::log10(std::max(1.0f, static_cast<float>(fc)));
        float x = w * (logF - logMin) / (logMax - logMin);
        
        float analyzerMinDb = -90.0f;
        float y = h * (0.0f - energies[i]) / (0.0f - analyzerMinDb);
        if (std::isnan(y) || std::isinf(y)) y = h;
        y = std::clamp(y, 0.0f, h);
        
        if (firstPoint)
        {
            fillPath.startNewSubPath(xOffset, h);
            fillPath.lineTo(xOffset, y);
            fillPath.lineTo(xOffset + x, y);
            firstPoint = false;
        }
        else
        {
            fillPath.lineTo(xOffset + x, y);
        }
        lastX = x;
        lastY = y;
        
        if (x > w)
        {
            fillPath.lineTo(xOffset + w, lastY);
            break;
        }
    }
    
    if (!firstPoint)
    {
        if (lastX <= w)
        {
            fillPath.lineTo(xOffset + w, lastY);
        }
        fillPath.lineTo(xOffset + w, h);
        fillPath.closeSubPath();
        
        const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
        juce::ColourGradient fillGrad(
            pal.anaFill, xOffset, h - 120.0f,
            pal.anaFill.withAlpha(0.0f), xOffset, h,
            false
        );
        g.setGradientFill(fillGrad);
        g.fillPath(fillPath);
    }
}
