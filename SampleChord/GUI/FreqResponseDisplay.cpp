#include "FreqResponseDisplay.h"
#include "SOSCoefficients.h"
#include "ColorPalette.h"
#include "../DSP/AnalyzerDSP.h"
#include "../PluginProcessor.h"
#include "../PluginEditor.h"
#include <numbers>
#include <algorithm>

FreqResponseDisplay::FreqResponseDisplay()
{
    setWantsKeyboardFocus(true);
    addAndMakeVisible(zoomInXBtn);
    addAndMakeVisible(zoomOutXBtn);
    addAndMakeVisible(zoomInYBtn);
    addAndMakeVisible(zoomOutYBtn);

    auto styleBtn = [](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0x882a2a4e));
        b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    };
    styleBtn(zoomInXBtn); styleBtn(zoomOutXBtn);
    styleBtn(zoomInYBtn); styleBtn(zoomOutYBtn);

    zoomInXBtn.onClick = [this] { currentMaxF = std::max(200.0f, currentMaxF / 2.0f); precomputeFrequencies(); pathNeedsRecalculation = true; repaint(); };
    zoomOutXBtn.onClick = [this] { currentMaxF = std::min(20000.0f, currentMaxF * 2.0f); precomputeFrequencies(); pathNeedsRecalculation = true; repaint(); };
    zoomInYBtn.onClick = [this] { currentMinDb = std::min(-30.0f, currentMinDb + 10.0f); pathNeedsRecalculation = true; repaint(); };
    zoomOutYBtn.onClick = [this] { currentMinDb = std::max(-120.0f, currentMinDb - 10.0f); pathNeedsRecalculation = true; repaint(); };
}

void FreqResponseDisplay::setColorPaletteIndex(int index)
{
    if (currentPaletteIdx != index)
    {
        currentPaletteIdx = std::clamp(index, 0, 9);
        pathNeedsRecalculation = true;
        repaint();
    }
}

void FreqResponseDisplay::updateParameters(double cutoffHz, int order, double gainDb, double sampleRate, const std::array<BellParam, 4>& bells, bool lowcutEnable)
{
    bool changed = false;
    
    if (std::abs(currentCutoffHz - cutoffHz) > 0.1 ||
        currentOrder != order ||
        std::abs(currentGainDb - gainDb) > 0.01 ||
        currentLowcutEnable != lowcutEnable)
    {
        currentCutoffHz = cutoffHz;
        currentOrder = order;
        currentGainDb = gainDb;
        currentLowcutEnable = lowcutEnable;
        changed = true;
    }
    
    if (std::abs(currentSampleRate - sampleRate) > 1.0)
    {
        currentSampleRate = sampleRate;
        precomputeFrequencies();
        changed = true;
    }

    for (int i = 0; i < 4; ++i) {
        if (std::abs(bellParams[i].freq - bells[i].freq) > 0.1 ||
            std::abs(bellParams[i].gain - bells[i].gain) > 0.01 ||
            std::abs(bellParams[i].q - bells[i].q) > 0.01 ||
            bellParams[i].active != bells[i].active)
        {
            bellParams[i] = bells[i];
            changed = true;
        }
    }

    if (changed)
    {
        pathNeedsRecalculation = true;
        repaint();
    }
}

void FreqResponseDisplay::setAnalyzer(AnalyzerDSP* a)
{
    analyzer = a;
    if (analyzer != nullptr)
    {
        vblankAttachment = std::make_unique<juce::VBlankAttachment>(this, [this]() { vblankUpdate(); });
    }
    else
    {
        vblankAttachment.reset();
    }
}

void FreqResponseDisplay::vblankUpdate()
{
    bool shouldRepaint = pathNeedsRecalculation;
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
            shouldRepaint = true;
        }
    }
    
    if (shouldRepaint)
    {
        repaint();
    }
}

float FreqResponseDisplay::logFToX(float f) const
{
    float minF = currentMinF;
    float maxF = currentMaxF;
    float w = (float)getWidth();
    if (w <= 0) return 0.0f;
    
    float logMin = std::log10(minF);
    float logMax = std::log10(maxF);
    float logF = std::log10(std::max(1.0f, f));
    
    return w * (logF - logMin) / (logMax - logMin);
}

float FreqResponseDisplay::xToLogF(float x) const
{
    float minF = currentMinF;
    float maxF = currentMaxF;
    float w = (float)getWidth();
    if (w <= 0) return minF;
    
    float logMin = std::log10(minF);
    float logMax = std::log10(maxF);
    float logF = logMin + (x / w) * (logMax - logMin);
    
    return std::pow(10.0f, logF);
}

float FreqResponseDisplay::gainToY(float gainDecibels) const
{
    float maxG = 20.0f;  // +18dB boost + 2dB margin
    float minG = -20.0f; // -10dB lowcut, -18dB cut, -20dB margin
    float h = (float)getHeight();
    if (h <= 0) return 0.0f;
    
    return h * (maxG - gainDecibels) / (maxG - minG);
}

float FreqResponseDisplay::analyzerGainToY(float gainDecibels) const
{
    float maxG = 0.0f + analyzerGainOffsetDb;
    float minG = -90.0f + analyzerGainOffsetDb; // Default analyzer floor set to -90dB + offset
    float h = (float)getHeight();
    if (h <= 0) return 0.0f;
    
    return h * (maxG - gainDecibels) / (maxG - minG);
}

void FreqResponseDisplay::paint(juce::Graphics& g)
{
    const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
    
    // Draw background
    g.fillAll(pal.bg);
    
    auto w = (float)getWidth();
    auto h = (float)getHeight();
    
    if (w <= 0 || h <= 0 || currentSampleRate <= 0.0) return;
    
    // Draw grid lines
    g.setColour(pal.grid);
    
    // Frequency grid
    std::vector<float> freqs;
    float rangeLog = std::log10(currentMaxF / currentMinF);
    if (rangeLog > 1.5f) {
        float f = 10.0f;
        while (f <= 100000.0f) {
            freqs.push_back(f);
            freqs.push_back(f * 2.0f);
            freqs.push_back(f * 5.0f);
            f *= 10.0f;
        }
    } else if (rangeLog > 0.5f) {
        float f = 10.0f;
        while (f <= 100000.0f) {
            for (int i = 1; i <= 9; ++i) freqs.push_back(f * i);
            f *= 10.0f;
        }
    } else if (rangeLog > 0.1f) {
        float f = std::floor(currentMinF / 10.0f) * 10.0f;
        while (f <= currentMaxF + 10.0f) {
            freqs.push_back(f);
            f += 10.0f;
        }
    } else {
        float f = std::floor(currentMinF);
        while (f <= currentMaxF + 1.0f) {
            freqs.push_back(f);
            f += 1.0f;
        }
    }
    float lastLabelX = -100.0f;
    for (float f : freqs)
    {
        if (f < currentMinF || f > currentMaxF) continue;

        float x = logFToX(f);
        g.drawVerticalLine(static_cast<int>(x), 0.0f, h);
        
        if (x - lastLabelX >= 45.0f)
        {
            g.setColour(pal.text);
            g.setFont(10.0f);
            
            juce::String label;
            if (f >= 1000.0f) label = juce::String(f / 1000.0f, 1) + " kHz";
            else label = juce::String(static_cast<int>(f)) + " Hz";

            g.drawText(label, 
                       static_cast<int>(x) + 3, static_cast<int>(h) - 15, 
                       50, 12, juce::Justification::left);
            lastLabelX = x;
        }
        
        g.setColour(pal.grid);
    }
    
    // Gain grid
    std::vector<float> gains = { 18.0f, 12.0f, 6.0f, 0.0f, -3.0f, -6.0f, -10.0f, -18.0f };
    for (float db : gains)
    {
        float y = gainToY(db);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, w);
        
        g.setColour(pal.text);
        g.setFont(10.0f);
        g.drawText(juce::String(static_cast<int>(db)) + " dB", 
                   5, static_cast<int>(y) - 12, 
                   50, 12, juce::Justification::left);
        g.setColour(pal.grid);
    }
    
    // Draw analyzer spectrum
    if (analyzer != nullptr)
    {
        std::vector<float> energies = analyzer->getEnergies();
        
        double fmin = 10.0;
        double fmax = 24000.0;
        
        juce::Path fillPath;
        juce::Path strokePath;
        bool firstAnalyzerPoint = true;
        float firstX = 0;
        float lastX = 0;
        float lastY = h;
        
        for (int i = 0; i < AnalyzerDSP::NumBands; ++i)
        {
            double fc = fmin * std::pow(fmax / fmin, static_cast<double>(i) / (AnalyzerDSP::NumBands - 1));
            float x = logFToX(static_cast<float>(fc));
            float y = analyzerGainToY(energies[i]);
            if (std::isnan(y) || std::isinf(y)) y = h;
            y = std::clamp(y, 0.0f, h);

            if (firstAnalyzerPoint)
            {
                fillPath.startNewSubPath(0.0f, h);
                fillPath.lineTo(0.0f, y);
                fillPath.lineTo(x, y);
                strokePath.startNewSubPath(0.0f, y);
                strokePath.lineTo(x, y);
                firstX = x;
                firstAnalyzerPoint = false;
            }
            else
            {
                fillPath.lineTo(x, y);
                strokePath.lineTo(x, y);
            }
            lastX = x;
            lastY = y;
            
            if (x > w) 
            {
                fillPath.lineTo(w, lastY);
                strokePath.lineTo(w, lastY);
                break;
            }
        }
        
        if (!firstAnalyzerPoint)
        {
            if (lastX <= w)
            {
                fillPath.lineTo(w, lastY);
                strokePath.lineTo(w, lastY);
            }
            fillPath.lineTo(w, h);
            fillPath.closeSubPath();
            
            juce::ColourGradient fillGrad(
                pal.anaFill, 0.0f, 0.0f,
                pal.anaFill.withAlpha(0.0f), 0.0f, h,
                false
            );
            g.setGradientFill(fillGrad);
            g.fillPath(fillPath);
            
            g.setColour(pal.anaStroke);
            g.strokePath(strokePath, juce::PathStrokeType(1.2f));
        }
    }
    
    // Calculate and cache the response path
    if (pathNeedsRecalculation || cachedResponsePath.isEmpty())
    {
        if (precomputedFreqs.size() < static_cast<size_t>(w + 1))
        {
            precomputeFrequencies();
        }

        struct PrecomputedBell {
            bool active = false;
            double b0, b1, b2, a1, a2;
        };
        std::array<PrecomputedBell, 4> prepBells;
        for (int i = 0; i < 4; ++i)
        {
            const auto& bp = bellParams[i];
            if (!bp.active || std::abs(bp.gain) < 0.01)
            {
                prepBells[i].active = false;
                continue;
            }
            prepBells[i].active = true;
            
            double A = std::pow(10.0, bp.gain / 40.0);
            double w0 = 2.0 * std::numbers::pi * bp.freq / currentSampleRate;
            double alpha = std::sin(w0) / (2.0 * bp.q);
            
            double a0 = 1.0 + alpha / A;
            prepBells[i].b0 = (1.0 + alpha * A) / a0;
            prepBells[i].b1 = (-2.0 * std::cos(w0)) / a0;
            prepBells[i].b2 = (1.0 - alpha * A) / a0;
            prepBells[i].a1 = (-2.0 * std::cos(w0)) / a0;
            prepBells[i].a2 = (1.0 - alpha / A) / a0;
        }

        auto getBellMagCached = [](double cosw, double sinw, double cos2w, double sin2w, const PrecomputedBell& pb) {
            if (!pb.active) return 1.0;
            
            double numRe = pb.b0 + pb.b1 * cosw + pb.b2 * cos2w;
            double numIm = -pb.b1 * sinw - pb.b2 * sin2w;
            double numMagSq = numRe * numRe + numIm * numIm;
            
            double denRe = 1.0 + pb.a1 * cosw + pb.a2 * cos2w;
            double denIm = -pb.a1 * sinw - pb.a2 * sin2w;
            double denMagSq = denRe * denRe + denIm * denIm;
            
            if (denMagSq == 0.0) return 1.0;
            return std::sqrt(numMagSq / denMagSq);
        };

        auto sos = SOSCoefficients::computeHighPass(currentCutoffHz, currentSampleRate, currentOrder, currentGainDb);
        double mix = std::clamp(std::abs(currentGainDb) / 10.0, 0.0, 1.0);

        cachedResponsePath.clear();
        bool firstPoint = true;

        for (int x = 0; x <= static_cast<int>(w); ++x)
        {
            if (static_cast<size_t>(x) >= precomputedFreqs.size()) break;
            
            const auto& pf = precomputedFreqs[static_cast<size_t>(x)];
            
            double wetMagSq = 1.0;
            for (const auto& sec : sos)
            {
                double numReal = sec.b0 + sec.b1 * pf.cosw + sec.b2 * pf.cos2w;
                double numImag = -(sec.b1 * pf.sinw + sec.b2 * pf.sin2w);
                double numMagSq = numReal * numReal + numImag * numImag;
                
                double denReal = 1.0 + sec.a1 * pf.cosw + sec.a2 * pf.cos2w;
                double denImag = -(sec.a1 * pf.sinw + sec.a2 * pf.sin2w);
                double denMagSq = denReal * denReal + denImag * denImag;
                
                if (denMagSq > 1e-15)
                    wetMagSq *= (numMagSq / denMagSq);
            }
            
            double totalMag = 1.0;
            if (currentLowcutEnable) {
                totalMag = (1.0 - mix) + mix * wetMagSq;
            }

            for (int i = 0; i < 4; ++i) {
                totalMag *= getBellMagCached(pf.cosw, pf.sinw, pf.cos2w, pf.sin2w, prepBells[i]);
            }

            double db = 20.0 * std::log10(std::max(totalMag, 1e-10));
            float y = gainToY(static_cast<float>(db));
            
            if (firstPoint)
            {
                cachedResponsePath.startNewSubPath(static_cast<float>(x), y);
                firstPoint = false;
            }
            else
            {
                cachedResponsePath.lineTo(static_cast<float>(x), y);
            }
        }

        // Setup color gradients (fully re-initialize to clear previous colors and avoid accumulation)
        cachedLineGrad = juce::ColourGradient(juce::Colours::transparentBlack, 0.0f, 0.0f,
                                              juce::Colours::transparentBlack, w, 0.0f, false);
        cachedFillGrad = juce::ColourGradient(juce::Colours::transparentBlack, 0.0f, 0.0f,
                                              juce::Colours::transparentBlack, w, 0.0f, false);

        juce::Colour bellColors[4] = {
            pal.bell1,
            pal.bell2,
            pal.bell3,
            pal.bell4
        };
        juce::Colour lowcutColor = pal.lowcut;

        int numPoints = 100;
        for (int i = 0; i <= numPoints; ++i)
        {
            float proportion = static_cast<float>(i) / numPoints;
            float targetX = proportion * w;
            float f = xToLogF(targetX);
            
            double w_rad = 2.0 * std::numbers::pi * f / currentSampleRate;
            double cw = std::cos(w_rad);
            double sw = std::sin(w_rad);
            double cw2 = std::cos(2.0 * w_rad);
            double sw2 = std::sin(2.0 * w_rad);
            
            double w_sum = 0.0;
            double r = 0.0, g_val = 0.0, b = 0.0;
            
            for (int bIdx = 0; bIdx < 4; ++bIdx)
            {
                if (prepBells[bIdx].active)
                {
                    double mag = getBellMagCached(cw, sw, cw2, sw2, prepBells[bIdx]);
                    double attenuationDb = -20.0 * std::log10(std::max(mag, 1e-5));
                    
                    double diffDb = std::abs(attenuationDb);
                    if (diffDb > 0.1)
                    {
                        w_sum += diffDb;
                        r += diffDb * bellColors[bIdx].getRed();
                        g_val += diffDb * bellColors[bIdx].getGreen();
                        b += diffDb * bellColors[bIdx].getBlue();
                    }
                }
            }
            
            juce::Colour finalColor;
            if (w_sum > 0.0)
            {
                double r_blend = r / w_sum;
                double g_blend = g_val / w_sum;
                double b_blend = b / w_sum;
                juce::Colour blendedBellColor = juce::Colour(
                    static_cast<juce::uint8>(r_blend),
                    static_cast<juce::uint8>(g_blend),
                    static_cast<juce::uint8>(b_blend)
                );
                
                double mixRatio = std::min(1.0, w_sum / 3.0);
                finalColor = lowcutColor.interpolatedWith(blendedBellColor, static_cast<float>(mixRatio));
            }
            else
            {
                finalColor = lowcutColor;
            }
            
            cachedLineGrad.addColour(proportion, finalColor);
            cachedFillGrad.addColour(proportion, finalColor.withAlpha(0.15f));
        }

        pathNeedsRecalculation = false;
    }
    
    // Render Cached Paths
    float zeroDbY = std::clamp(gainToY(0.0f), 0.0f, h);