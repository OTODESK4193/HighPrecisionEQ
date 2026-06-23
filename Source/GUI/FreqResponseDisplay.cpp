#include "FreqResponseDisplay.h"
#include "AnalyzerDSP.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ColorPalette.h"

FreqResponseDisplay::FreqResponseDisplay()
{
    // VBlankでスムーズな描画同期
    vblankAttachment = std::make_unique<juce::VBlankAttachment>(this, [this]() { vblankUpdate(); });

    // ズームボタンの設定
    zoomInXBtn.setButtonText("H+");
    zoomOutXBtn.setButtonText("H-");
    zoomInYBtn.setButtonText("V+");
    zoomOutYBtn.setButtonText("V-");

    addAndMakeVisible(zoomInXBtn);
    addAndMakeVisible(zoomOutXBtn);
    addAndMakeVisible(zoomInYBtn);
    addAndMakeVisible(zoomOutYBtn);

    auto configureZoomBtn = [](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0x22ffffff));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffa0a0c0));
        btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0x44ffffff));
    };

    configureZoomBtn(zoomInXBtn);
    configureZoomBtn(zoomOutXBtn);
    configureZoomBtn(zoomInYBtn);
    configureZoomBtn(zoomOutYBtn);

    zoomInXBtn.onClick = [this]() {
        float centerLog = std::sqrt(currentMinF * currentMaxF);
        float ratio = currentMaxF / currentMinF;
        float newRatio = std::max(ratio * 0.6f, 1.002f);
        currentMinF = centerLog / std::sqrt(newRatio);
        currentMaxF = centerLog * std::sqrt(newRatio);
        if (currentMinF < 1.0f) currentMinF = 1.0f;
        if (currentMaxF > 24000.0f) currentMaxF = 24000.0f;
        pathNeedsRecalculation = true;
        repaint();
    };

    zoomOutXBtn.onClick = [this]() {
        float centerLog = std::sqrt(currentMinF * currentMaxF);
        float ratio = currentMaxF / currentMinF;
        float newRatio = std::min(ratio * 1.5f, 2400.0f);
        currentMinF = centerLog / std::sqrt(newRatio);
        currentMaxF = centerLog * std::sqrt(newRatio);
        if (currentMinF < 1.0f) currentMinF = 1.0f;
        if (currentMaxF > 24000.0f) currentMaxF = 24000.0f;
        pathNeedsRecalculation = true;
        repaint();
    };

    zoomInYBtn.onClick = [this]() {
        currentMinDb = std::max(currentMinDb * 0.7f, -48.0f);
        currentMaxDb = -currentMinDb;
        repaint();
    };

    zoomOutYBtn.onClick = [this]() {
        currentMinDb = std::min(currentMinDb * 1.4f, -3.0f);
        currentMaxDb = -currentMinDb;
        repaint();
    };

    precomputeFrequencies();
}

void FreqResponseDisplay::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto zoomRow = r.removeFromTop(20);
    
    // 右上にコンパクトに並べる
    zoomOutYBtn.setBounds(zoomRow.removeFromRight(32));
    zoomRow.removeFromRight(2);
    zoomInYBtn.setBounds(zoomRow.removeFromRight(32));
    zoomRow.removeFromRight(8);
    zoomOutXBtn.setBounds(zoomRow.removeFromRight(32));
    zoomRow.removeFromRight(2);
    zoomInXBtn.setBounds(zoomRow.removeFromRight(32));

    pathNeedsRecalculation = true;
}

void FreqResponseDisplay::vblankUpdate()
{
    if (analyzer != nullptr)
    {
        uint64_t currentCount = analyzer->getUpdateCount();
        if (lastSeenUpdateCount != currentCount)
        {
            lastSeenUpdateCount = currentCount;
            repaint();
        }
    }
}

void FreqResponseDisplay::setColorPaletteIndex(int index)
{
    currentPaletteIdx = index;
    pathNeedsRecalculation = true;
    repaint();
}

void FreqResponseDisplay::setAnalyzer(AnalyzerDSP* anz)
{
    analyzer = anz;
}

void FreqResponseDisplay::setProcessorAndEditor(HighPrecisionEQAudioProcessor* proc, HighPrecisionEQAudioProcessorEditor* ed)
{
    processor = proc;
    editor = ed;
}

void FreqResponseDisplay::setSelectedBand(int bandIdx)
{
    selectedBandIdx = bandIdx;
    repaint();
}

void FreqResponseDisplay::updateParameters(double cutoffHz, int order, double gainDb, bool lowcutEnable,
                                           double highCutFreq, int highCutOrder, bool highCutEnable,
                                           double sampleRate, const std::array<BellParam, 4>& bells)
{
    currentCutoffHz = cutoffHz;
    currentOrder = order;
    currentGainDb = gainDb;
    currentLowcutEnable = lowcutEnable;
    currentHighCutFreq = highCutFreq;
    currentHighCutOrder = highCutOrder;
    currentHighCutEnable = highCutEnable;
    currentSampleRate = sampleRate;
    bellParams = bells;

    pathNeedsRecalculation = true;
    repaint();
}

void FreqResponseDisplay::precomputeFrequencies()
{
    // 各ピクセルに対応する対数周波数を事前に計算
    precomputedFreqs.resize(1000);
}


float FreqResponseDisplay::gainToY(float gainDecibels) const
{
    float val = (gainDecibels - currentMinDb) / (currentMaxDb - currentMinDb);
    return (1.0f - val) * static_cast<float>(getHeight());
}

float FreqResponseDisplay::yToGain(float y) const
{
    float val = y / static_cast<float>(getHeight());
    return currentMinDb + (1.0f - val) * (currentMaxDb - currentMinDb);
}

float FreqResponseDisplay::analyzerGainToY(float gainDecibels) const
{
    // アナライザーのゲイン描画範囲 (通常 -70 dB 〜 +10 dB)
    float minDb = -70.0f + analyzerGainOffsetDb;
    float maxDb = 10.0f + analyzerGainOffsetDb;
    float val = (gainDecibels - minDb) / (maxDb - minDb);
    return (1.0f - val) * static_cast<float>(getHeight());
}

void FreqResponseDisplay::paint(juce::Graphics& g)
{
    const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
    
    // 背景
    g.fillAll(juce::Colour(0xff0d0d15));

    if (processor != nullptr)
    {
        double psr = processor->getSampleRate();
        if (psr > 8000.0 && std::abs(currentSampleRate - psr) > 0.01)
        {
            currentSampleRate = psr;
            pathNeedsRecalculation = true;
        }
    }

    int w = getWidth();
    int h = getHeight();

    if (w <= 0 || h <= 0) return;

    // 1. 周波数グリッド描画 (ズームに応じた適応型動的グリッド)
    float range = currentMaxF - currentMinF;
    g.setColour(juce::Colour(0xff222230));

    if (range <= 1000.0f)
    {
        // 線形ステップグリッドの算出
        float gridStep = 100.0f;
        if (range <= 2.0f)        gridStep = 0.2f;
        else if (range <= 10.0f)   gridStep = 1.0f;
        else if (range <= 50.0f)   gridStep = 5.0f;
        else if (range <= 200.0f)  gridStep = 20.0f;

        float firstGrid = std::ceil(currentMinF / gridStep) * gridStep;
        float lastLabelX = -100.0f;

        for (float f = firstGrid; f <= currentMaxF + 0.0001f; f += gridStep)
        {
            if (f >= currentMinF && f <= currentMaxF)
            {
                float x = logFToX(f);
                g.setColour(juce::Colour(0xff222230));
                g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(h));

                // 隣り合うラベルが近すぎる場合はラベル描画をスキップ (間引き)
                if (x - lastLabelX >= 40.0f)
                {
                    g.setColour(juce::Colour(0xff555570));
                    g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::plain)));
                    
                    juce::String text;
                    if (gridStep == 0.2f)
                    {
                        text = juce::String(f, 1) + " Hz";
                    }
                    else if (f >= 1000.0f)
                    {
                        float kF = f / 1000.0f;
                        if (std::abs(kF - std::round(kF)) < 0.01f)
                            text = juce::String(static_cast<int>(std::round(kF))) + "k";
                        else
                            text = juce::String(kF, 1) + "k";
                    }
                    else
                    {
                        text = juce::String(static_cast<int>(std::round(f))) + " Hz";
                    }
                    
                    g.drawText(text, static_cast<int>(x) - 25, h - 15, 50, 12, juce::Justification::centred);
                    lastLabelX = x;
                }
            }
        }
    }
    else
    {
        // 従来の対数グリッド
        float gridFreqs[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
        float lastLabelX = -100.0f;
        for (float f : gridFreqs)
        {
            if (f >= currentMinF && f <= currentMaxF)
            {
                float x = logFToX(f);
                g.setColour(juce::Colour(0xff222230));
                g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(h));
                
                if (x - lastLabelX >= 40.0f)
                {
                    g.setColour(juce::Colour(0xff555570));
                    g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::plain)));
                    juce::String text = (f >= 1000.0f) ? juce::String(f / 1000.0f, 0) + "k" : juce::String(f, 0);
                    g.drawText(text, static_cast<int>(x) - 15, h - 15, 30, 12, juce::Justification::centred);
                    lastLabelX = x;
                }
            }
        }
    }

    // 2. ゲイングリッド描画 (dB)
    g.setColour(juce::Colour(0xff222230));
    float step = (currentMaxDb - currentMinDb) / 4.0f;
    for (int i = 0; i <= 4; ++i)
    {
        float db = currentMinDb + static_cast<float>(i) * step;
        float y = gainToY(db);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(w));

        g.setColour(juce::Colour(0xff555570));
        g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::plain)));
        g.drawText(juce::String(db, 1) + " dB", 5, static_cast<int>(y) - 12, 50, 12, juce::Justification::left);
        g.setColour(juce::Colour(0xff222230));
    }

    // 3. アナライザーリアルタイムスペクトラムの描画
    if (analyzer != nullptr)
    {
        std::vector<float> energies = analyzer->getEnergies();
        
        const int numBands = AnalyzerDSP::NumBands;
        double fmin = 10.0;
        double fmax = std::min(24000.0, currentSampleRate * 0.49);
        double logFmin = std::log(fmin);
        double logRatio = std::log(fmax / fmin);

        // 480バンドの離散周波数値から対数線形補間するラムダ
        auto getInterpolatedDb = [&](double f) -> float
        {
            if (f <= fmin) return energies[0];
            if (f >= fmax) return energies[numBands - 1];
            
            double idx = (std::log(f) - logFmin) / logRatio * (numBands - 1);
            int idx0 = std::clamp(static_cast<int>(std::floor(idx)), 0, numBands - 1);
            int idx1 = std::clamp(idx0 + 1, 0, numBands - 1);
            double frac = idx - idx0;
            
            return energies[static_cast<size_t>(idx0)] * (1.0f - static_cast<float>(frac)) 
                 + energies[static_cast<size_t>(idx1)] * static_cast<float>(frac);
        };

        juce::Path analyzerPath;
        juce::Path strokePath;
        bool started = false;
        
        for (int i = 0; i < w; ++i)
        {
            float f = xToLogF(static_cast<float>(i));
            float magDb = getInterpolatedDb(f);
            float y = analyzerGainToY(magDb);
            y = std::clamp(y, 0.0f, static_cast<float>(h));
            
            if (!started)
            {
                analyzerPath.startNewSubPath(static_cast<float>(i), static_cast<float>(h));
                analyzerPath.lineTo(static_cast<float>(i), y);
                strokePath.startNewSubPath(static_cast<float>(i), y);
                started = true;
            }
            else
            {
                analyzerPath.lineTo(static_cast<float>(i), y);
                strokePath.lineTo(static_cast<float>(i), y);
            }
        }

        if (started)
        {
            analyzerPath.lineTo(static_cast<float>(w), static_cast<float>(h));
            analyzerPath.closeSubPath();
            
            juce::ColourGradient fillGrad(pal.anaFill, 0.0f, 0.0f,
                                          pal.anaFill.withAlpha(0.0f), 0.0f, static_cast<float>(h), false);
            g.setGradientFill(fillGrad);
            g.fillPath(analyzerPath);

            g.setColour(pal.anaStroke);
            g.strokePath(strokePath, juce::PathStrokeType(1.2f));
        }
    }

    // 4. EQ特性カーブの再計算と描画
    if (pathNeedsRecalculation || cachedResponsePath.isEmpty())
    {
        if (precomputedFreqs.size() < static_cast<size_t>(w + 1))
        {
            precomputeFrequencies();
        }

        cachedResponsePath.clear();
        
        // 最適なイコライザー応答カーブ計算
        MinimumPhaseEQ localEQ;
        std::array<MinimumPhaseEQ::BellParam, 4> localBells;
        for (int i = 0; i < 4; ++i)
        {
            localBells[i].freq = bellParams[i].freq;
            localBells[i].gain = bellParams[i].gain;
            localBells[i].q = bellParams[i].q;
            localBells[i].active = bellParams[i].active;
        }
        localEQ.prepare(currentSampleRate, 512);
        localEQ.updateParameters(currentCutoffHz, currentOrder, currentLowcutEnable,
                                 currentHighCutFreq, currentHighCutOrder, currentHighCutEnable,
                                 localBells);

        bool started = false;
        for (int x = 0; x < w; ++x)
        {
            float f = xToLogF(static_cast<float>(x));
            double mag = localEQ.getMagnitudeForFrequency(f);

            // NaN/inf ガード: 振幅が極めて小さい、または無効値のときに -inf になり、
            // juce::Path が壊れて描画全体が消滅するのを完全に防ぐ
            if (std::isnan(mag) || std::isinf(mag)) mag = 1.0;
            mag = std::max(mag, 1e-10);

            float magDb = static_cast<float>(juce::Decibels::gainToDecibels(mag));
            float y = gainToY(magDb);
            y = std::clamp(y, -100.0f, static_cast<float>(h) + 100.0f);

            if (!started)
            {
                cachedResponsePath.startNewSubPath(static_cast<float>(x), y);
                started = true;
            }
            else
            {
                cachedResponsePath.lineTo(static_cast<float>(x), y);
            }
        }

        // --- SampleChordと同等のColorロジック (横方向マルチカラーグラデーションの計算) ---
        cachedLineGrad = juce::ColourGradient(juce::Colours::transparentBlack, 0.0f, 0.0f,
                                              juce::Colours::transparentBlack, static_cast<float>(w), 0.0f, false);
        cachedFillGrad = juce::ColourGradient(juce::Colours::transparentBlack, 0.0f, 0.0f,
                                              juce::Colours::transparentBlack, static_cast<float>(w), 0.0f, false);

        juce::Colour bellColors[4] = { pal.bell1, pal.bell2, pal.bell3, pal.bell4 };
        juce::Colour lowcutColor = pal.lowcut;

        int numPoints = 100;
        for (int i = 0; i <= numPoints; ++i)
        {
            float proportion = static_cast<float>(i) / numPoints;
            float targetX = proportion * static_cast<float>(w);
            float f = xToLogF(targetX);
            
            double w_rad = 2.0 * std::numbers::pi * f / currentSampleRate;
            double cw = std::cos(w_rad);
            double sw = std::sin(w_rad);
            double cw2 = std::cos(2.0 * w_rad);
            double sw2 = std::sin(2.0 * w_rad);
            
            double w_sum = 0.0;
            double r_sum = 0.0, g_sum = 0.0, b_sum = 0.0;
            
            for (int bIdx = 0; bIdx < 4; ++bIdx)
            {
                if (localBells[bIdx].active && std::abs(localBells[bIdx].gain) > 0.01)
                {
                    double A = std::pow(10.0, localBells[bIdx].gain / 40.0);
                    double w0 = 2.0 * std::numbers::pi * localBells[bIdx].freq / currentSampleRate;
                    double alpha = std::sin(w0) / (2.0 * localBells[bIdx].q);
                    
                    double a0 = 1.0 + alpha / A;
                    double b0 = (1.0 + alpha * A) / a0;
                    double b1 = (-2.0 * std::cos(w0)) / a0;
                    double b2 = (1.0 - alpha * A) / a0;
                    double a1 = (-2.0 * std::cos(w0)) / a0;
                    double a2 = (1.0 - alpha / A) / a0;
                    
                    double numRe = b0 + b1 * cw + b2 * cw2;
                    double numIm = -b1 * sw - b2 * sw2;
                    double numMagSq = numRe * numRe + numIm * numIm;
                    
                    double denRe = 1.0 + a1 * cw + a2 * cw2;
                    double denIm = -a1 * sw - a2 * sw2;
                    double denMagSq = denRe * denRe + denIm * denIm;
                    
                    double bellMag = 1.0;
                    if (denMagSq > 0.0) {
                        bellMag = std::sqrt(numMagSq / denMagSq);
                    }
                    
                    double attenuationDb = -20.0 * std::log10(std::max(bellMag, 1e-5));
                    double diffDb = std::abs(attenuationDb);
                    if (diffDb > 0.1)
                    {
                        w_sum += diffDb;
                        r_sum += diffDb * bellColors[bIdx].getRed();
                        g_sum += diffDb * bellColors[bIdx].getGreen();
                        b_sum += diffDb * bellColors[bIdx].getBlue();
                    }
                }
            }
            
            juce::Colour finalColor;
            if (w_sum > 0.0)
            {
                double r_blend = r_sum / w_sum;
                double g_blend = g_sum / w_sum;
                double b_blend = b_sum / w_sum;
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

    // カーブの塗りつぶし用パスを作成
    juce::Path fillPath = cachedResponsePath;
    fillPath.lineTo(static_cast<float>(w), gainToY(0.0f));
    fillPath.lineTo(0.0f, gainToY(0.0f));
    fillPath.closeSubPath();

    g.setGradientFill(cachedFillGrad);
    g.fillPath(fillPath);

    g.setGradientFill(cachedLineGrad);
    g.strokePath(cachedResponsePath, juce::PathStrokeType(2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

    // 5. EQコントロールポイントの描画
    drawEQPoints(g);
}

void FreqResponseDisplay::drawEQPoints(juce::Graphics& g)
{
    const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
    
    // 各バンドのコントロールポイント座標を計算して描画
    // 0: LowCut
    if (currentLowcutEnable)
    {
        float x = logFToX(static_cast<float>(currentCutoffHz));
        // LowCut の表示上のY座標は gainDb に準ずる
        float y = gainToY(static_cast<float>(currentGainDb));

        g.setColour(selectedBandIdx == 0 ? juce::Colours::white : pal.lowcut);
        g.fillEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f);
        g.setColour(pal.lowcut.withAlpha(0.4f));
        g.drawEllipse(x - 9.0f, y - 9.0f, 18.0f, 18.0f, 1.5f);
        
        g.setColour(pal.text.withAlpha(0.8f));
        g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::bold)));
        g.drawText("LC", static_cast<int>(x) - 15, static_cast<int>(y) - 22, 30, 10, juce::Justification::centred);
    }

    // 1: HighCut
    if (currentHighCutEnable)
    {
        float x = logFToX(static_cast<float>(currentHighCutFreq));
        float y = gainToY(0.0f); // HighCutはゲインパラメータが無いので0dBライン

        g.setColour(selectedBandIdx == 1 ? juce::Colours::white : pal.bell1);
        g.fillEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f);
        g.setColour(pal.bell1.withAlpha(0.4f));
        g.drawEllipse(x - 9.0f, y - 9.0f, 18.0f, 18.0f, 1.5f);

        g.setColour(pal.text.withAlpha(0.8f));
        g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::bold)));
        g.drawText("HC", static_cast<int>(x) - 15, static_cast<int>(y) - 22, 30, 10, juce::Justification::centred);
    }

    // 2..5: Bells
    juce::Colour bellColors[4] = { pal.bell1, pal.bell2, pal.bell3, pal.bell4 };
    juce::String bellNames[4] = { "B1", "B2", "B3", "B4" };
    
    for (int i = 0; i < 4; ++i)
    {
        if (bellParams[i].active)
        {
            float x = logFToX(static_cast<float>(bellParams[i].freq));
            float y = gainToY(static_cast<float>(bellParams[i].gain));

            g.setColour(selectedBandIdx == (i + 2) ? juce::Colours::white : bellColors[i]);
            g.fillEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f);
            g.setColour(bellColors[i].withAlpha(0.4f));
            g.drawEllipse(x - 9.0f, y - 9.0f, 18.0f, 18.0f, 1.5f);

            g.setColour(pal.text.withAlpha(0.8f));
            g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::bold)));
            g.drawText(bellNames[i], static_cast<int>(x) - 15, static_cast<int>(y) - 22, 30, 10, juce::Justification::centred);
        }
    }
}

void FreqResponseDisplay::mouseDown(const juce::MouseEvent& e)
{
    activeDragBand = -1;
    float mouseX = static_cast<float>(e.x);
    float mouseY = static_cast<float>(e.y);

    // 各ポイントへのクリック判定 (半径15ピクセル以内ならキャッチ)
    const float grabRadius = 15.0f;

    // 1. LowCut
    if (currentLowcutEnable)
    {
        float x = logFToX(static_cast<float>(currentCutoffHz));
        float y = gainToY(static_cast<float>(currentGainDb));
        if (std::hypot(mouseX - x, mouseY - y) < grabRadius)
        {
            activeDragBand = 0;
            if (editor != nullptr) editor->selectBand(HighPrecisionEQAudioProcessorEditor::SelectedBand::LowCut);
            repaint();
            return;
        }
    }

    // 2. HighCut
    if (currentHighCutEnable)
    {
        float x = logFToX(static_cast<float>(currentHighCutFreq));
        float y = gainToY(0.0f);
        if (std::hypot(mouseX - x, mouseY - y) < grabRadius)
        {
            activeDragBand = 1;
            if (editor != nullptr) editor->selectBand(HighPrecisionEQAudioProcessorEditor::SelectedBand::HighCut);
            repaint();
            return;
        }
    }

    // 3. Bells
    for (int i = 0; i < 4; ++i)
    {
        if (bellParams[i].active)
        {
            float x = logFToX(static_cast<float>(bellParams[i].freq));
            float y = gainToY(static_cast<float>(bellParams[i].gain));
            if (std::hypot(mouseX - x, mouseY - y) < grabRadius)
            {
                activeDragBand = i + 2;
                if (editor != nullptr) editor->selectBand(static_cast<HighPrecisionEQAudioProcessorEditor::SelectedBand>(i + 2));
                repaint();
                return;
            }
        }
    }

    // どこもクリックされていなければ、X/Yドラッグズームの準備
    dragStartMinF = currentMinF;
    dragStartMaxF = currentMaxF;
    dragStartMinDb = currentMinDb;
}

void FreqResponseDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (editor == nullptr) return;

    float mouseX = static_cast<float>(e.x);
    float mouseY = static_cast<float>(e.y);

    // 画面外へのドラッグ制限
    mouseX = std::clamp(mouseX, 0.0f, static_cast<float>(getWidth()));
    mouseY = std::clamp(mouseY, 0.0f, static_cast<float>(getHeight()));

    float dragFreq = xToLogF(mouseX);
    float dragGain = yToGain(mouseY);

    if (processor != nullptr && activeDragBand != -1)
    {
        auto& apvts = processor->apvts;

        if (activeDragBand == 0) // LowCut
        {
            float targetFreq = std::clamp(dragFreq, 20.0f, 500.0f);
            float targetGain = std::clamp(dragGain, -10.0f, 0.0f);

            auto rangeF = apvts.getParameterRange("cutoffHz");
            apvts.getParameter("cutoffHz")->setValueNotifyingHost(rangeF.convertTo0to1(targetFreq));

            auto rangeG = apvts.getParameterRange("gainDb");
            apvts.getParameter("gainDb")->setValueNotifyingHost(rangeG.convertTo0to1(targetGain));
        }
        else if (activeDragBand == 1) // HighCut
        {
            float targetFreq = std::clamp(dragFreq, 20.0f, 20000.0f);

            auto rangeF = apvts.getParameterRange("highcut_freq");
            apvts.getParameter("highcut_freq")->setValueNotifyingHost(rangeF.convertTo0to1(targetFreq));
        }
        else if (activeDragBand >= 2 && activeDragBand <= 5) // Bells
        {
            int idx = activeDragBand - 1; // Bell1..4 (1-indexed suffix: 1..4)
            juce::String idSuffix = juce::String(idx);
            
            float targetFreq = std::clamp(dragFreq, 20.0f, 20000.0f);
            float targetGain = std::clamp(dragGain, -18.0f, 18.0f);

            juce::String freqID = "bell_freq_" + idSuffix;
            juce::String gainID = "bell_gain_" + idSuffix;

            auto rangeF = apvts.getParameterRange(freqID);
            apvts.getParameter(freqID)->setValueNotifyingHost(rangeF.convertTo0to1(targetFreq));

            auto rangeG = apvts.getParameterRange(gainID);
            apvts.getParameter(gainID)->setValueNotifyingHost(rangeG.convertTo0to1(targetGain));
        }
    }
    else
    {
        // グラフ全体のズーム／シフト操作
        // ドラッグによる対数ピッチシフト
        float dx = static_cast<float>(e.getDistanceFromDragStartX());
        float dy = static_cast<float>(e.getDistanceFromDragStartY());

        if (e.mods.isRightButtonDown())
        {
            // 右ドラッグ: ズーム (X軸: 幅, Y軸: 高さ)
            float xZoomFactor = std::pow(1.005f, -dx);
            float centerLog = std::sqrt(dragStartMinF * dragStartMaxF);
            float ratio = dragStartMaxF / dragStartMinF;
            float newRatio = std::clamp(ratio * xZoomFactor, 1.002f, 2400.0f); // 最小比率を 1.002f に緩和
            
            currentMinF = centerLog / std::sqrt(newRatio);
            currentMaxF = centerLog * std::sqrt(newRatio);
            if (currentMinF < 1.0f) currentMinF = 1.0f; // 1Hzまでズーム可
            if (currentMaxF > 24000.0f) currentMaxF = 24000.0f;

            float yZoomFactor = std::pow(1.005f, -dy);
            currentMinDb = std::clamp(dragStartMinDb * yZoomFactor, -48.0f, -3.0f);
            currentMaxDb = -currentMinDb;
        }
        else
        {
            // 左ドラッグ: シフト移動
            float shiftFactor = std::pow(10.0f, -dx / static_cast<float>(getWidth()) * 0.5f);
            currentMinF = std::max(dragStartMinF * shiftFactor, 1.0f); // 1Hzまで
            currentMaxF = std::min(dragStartMaxF * shiftFactor, 24000.0f);
            
            // Y軸シフト (アナライザーの基準オフセットを調整)
            analyzerGainOffsetDb = std::clamp(analyzerGainOffsetDb - dy * 0.1f, -40.0f, 40.0f);
        }
        pathNeedsRecalculation = true;
        repaint();
    }
}

void FreqResponseDisplay::mouseUp(const juce::MouseEvent&)
{
    activeDragBand = -1;
}

void FreqResponseDisplay::mouseDoubleClick(const juce::MouseEvent&)
{
    // ダブルクリックでズーム初期化
    currentMinF = 10.0f;
    currentMaxF = 20000.0f;
    currentMinDb = -12.0f;
    currentMaxDb = 12.0f;
    analyzerGainOffsetDb = 0.0f;
    pathNeedsRecalculation = true;
    repaint();
}

void FreqResponseDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // 1. Ctrlキー押下時の周波数軸ズーム
    if (e.mods.isCtrlDown())
    {
        float mouseX = static_cast<float>(e.x);
        float targetF = xToLogF(mouseX);
        
        float zoomFactor = std::pow(1.15f, -wheel.deltaY); // 上スクロールで拡大、下で縮小
        
        float centerLog = std::log10(targetF);
        float minLog = std::log10(currentMinF);
        float maxLog = std::log10(currentMaxF);
        
        float newMinLog = centerLog - (centerLog - minLog) * zoomFactor;
        float newMaxLog = centerLog + (maxLog - centerLog) * zoomFactor;
        
        float newMinF = std::pow(10.0f, newMinLog);
        float newMaxF = std::pow(10.0f, newMaxLog);
        
        float ratio = newMaxF / newMinF;
        
        // ズーム比制限 (1.002f 〜 2400.0f)
        if (ratio >= 1.002f && ratio <= 2400.0f)
        {
            currentMinF = std::max(newMinF, 1.0f);
            currentMaxF = std::min(newMaxF, 24000.0f);
            pathNeedsRecalculation = true;
            repaint();
        }
        return;
    }

    // 2. 通常のスクロール (Q幅やスロープの調整)
    if (processor == nullptr) return;
    auto& apvts = processor->apvts;

    // マウスカーソルがEQポイントの上にあるか判定
    float mouseX = static_cast<float>(e.x);
    float mouseY = static_cast<float>(e.y);
    const float grabRadius = 15.0f;
    int targetBand = -1;

    // 1. LowCut
    if (currentLowcutEnable)
    {
        float x = logFToX(static_cast<float>(currentCutoffHz));
        float y = gainToY(static_cast<float>(currentGainDb));
        if (std::hypot(mouseX - x, mouseY - y) < grabRadius)
        {
            targetBand = 0;
        }
    }

    // 2. HighCut
    if (targetBand == -1 && currentHighCutEnable)
    {
        float x = logFToX(static_cast<float>(currentHighCutFreq));
        float y = gainToY(0.0f);
        if (std::hypot(mouseX - x, mouseY - y) < grabRadius)
        {
            targetBand = 1;
        }
    }

    // 3. Bells
    if (targetBand == -1)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (bellParams[i].active)
            {
                float x = logFToX(static_cast<float>(bellParams[i].freq));
                float y = gainToY(static_cast<float>(bellParams[i].gain));
                if (std::hypot(mouseX - x, mouseY - y) < grabRadius)
                {
                    targetBand = i + 2;
                    break;
                }
            }
        }
    }

    // どのEQポイントの上でもない場合は何もしない
    if (targetBand == -1)
        return;

    // 操作したバンドを選択状態にする
    if (selectedBandIdx != targetBand && editor != nullptr)
    {
        editor->selectBand(static_cast<HighPrecisionEQAudioProcessorEditor::SelectedBand>(targetBand));
    }

    if (targetBand == 0) // LowCut
    {
        juce::String slopeID = "slopeDbOct";
        float currentSlope = apvts.getRawParameterValue(slopeID)->load();
        float direction = (wheel.deltaY > 0.0f) ? 12.0f : -12.0f;
        float newSlope = currentSlope + direction;
        
        auto rangeS = apvts.getParameterRange(slopeID);
        newSlope = std::clamp(newSlope, rangeS.start, rangeS.end);
        
        apvts.getParameter(slopeID)->setValueNotifyingHost(rangeS.convertTo0to1(newSlope));
    }
    else if (targetBand == 1) // HighCut
    {
        juce::String slopeID = "highcut_slope";
        float currentSlope = apvts.getRawParameterValue(slopeID)->load();
        float direction = (wheel.deltaY > 0.0f) ? 12.0f : -12.0f;
        float newSlope = currentSlope + direction;
        
        auto rangeS = apvts.getParameterRange(slopeID);
        newSlope = std::clamp(newSlope, rangeS.start, rangeS.end);
        
        apvts.getParameter(slopeID)->setValueNotifyingHost(rangeS.convertTo0to1(newSlope));
    }
    else if (targetBand >= 2 && targetBand <= 5) // Bells
    {
        int idx = targetBand - 1; // Bell1..4 (1..4)
        juce::String idSuffix = juce::String(idx);
        juce::String qID = "bell_q_" + idSuffix;
        
        float currentQ = apvts.getRawParameterValue(qID)->load();
        float multiplier = std::pow(1.15f, wheel.deltaY * 2.0f);
        float newQ = currentQ * multiplier;
        
        auto rangeQ = apvts.getParameterRange(qID);
        newQ = std::clamp(newQ, rangeQ.start, rangeQ.end);
        
        apvts.getParameter(qID)->setValueNotifyingHost(rangeQ.convertTo0to1(newQ));
    }
}

void FreqResponseDisplay::modifierKeysChanged(const juce::ModifierKeys&)
{
}
