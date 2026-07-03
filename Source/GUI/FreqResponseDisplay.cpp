#include "FreqResponseDisplay.h"
#include "../DSP/SOSCoefficients.h"
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
        if (currentMaxF > 25000.0f) currentMaxF = 25000.0f;
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
        if (currentMaxF > 25000.0f) currentMaxF = 25000.0f;
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
                                           double highCutFreq, int highCutOrder, double highCutGainDb, bool highCutEnable,
                                           double sampleRate, const std::array<BellParam, 4>& bells)
{
    currentCutoffHz = cutoffHz;
    currentOrder = order;
    currentGainDb = gainDb;
    currentLowcutEnable = lowcutEnable;
    currentHighCutFreq = highCutFreq;
    currentHighCutOrder = highCutOrder;
    currentHighCutGainDb = highCutGainDb;
    currentHighCutEnable = highCutEnable;
    currentSampleRate = sampleRate;
    bellParams = bells;

    pathNeedsRecalculation = true;
    repaint();
}

void FreqResponseDisplay::precomputeFrequencies()
{
    int w = getWidth();
    if (w <= 0) return;

    double sr_safe = currentSampleRate;
    if (sr_safe < 8000.0) {
        if (processor != nullptr && processor->getSampleRate() > 8000.0) {
            sr_safe = processor->getSampleRate();
        } else {
            sr_safe = 44100.0;
        }
    }

    precomputedFreqs.resize(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x)
    {
        float f = xToLogF(static_cast<float>(x));
        double w_rad = 2.0 * std::numbers::pi * f / sr_safe;
        
        precomputedFreqs[static_cast<size_t>(x)].f = f;
        precomputedFreqs[static_cast<size_t>(x)].cosw = std::cos(w_rad);
        precomputedFreqs[static_cast<size_t>(x)].sinw = std::sin(w_rad);
        precomputedFreqs[static_cast<size_t>(x)].cos2w = std::cos(2.0 * w_rad);
        precomputedFreqs[static_cast<size_t>(x)].sin2w = std::sin(2.0 * w_rad);
    }
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

float FreqResponseDisplay::cutPointY(double freqHz) const
{
    // カットポイントは実際のEQカーブ上に置く (Butterworthのカットオフでは約-3dB)。
    // 以前は実効のないgainDbパラメータのY座標を使っていたため、カーブとポイントが
    // ずれて見えることがあった。
    double db = 0.0;
    if (processor != nullptr)
    {
        double mag = processor->getEQ().getMagnitudeForFrequency(freqHz);
        if (!std::isnan(mag) && !std::isinf(mag))
            db = 20.0 * std::log10(std::max(mag, 1e-10));
    }
    db = std::clamp(db, static_cast<double>(currentMinDb), static_cast<double>(currentMaxDb));
    return gainToY(static_cast<float>(db));
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
        if (psr > 8000.0 && (currentSampleRate < 8000.0 || std::abs(currentSampleRate - psr) > 0.01))
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
        float gridFreqs[] = { 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f, 25000.0f };
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
        
        // 右側のアナライザー音量メモリを描画 (左側の目盛りに現在のオフセット分を引いたもの)
        float db_ana = db - analyzerGainOffsetDb;
        g.drawText(juce::String(db_ana, 1) + " dB", static_cast<int>(w) - 55, static_cast<int>(y) - 12, 50, 12, juce::Justification::right);
        
        g.setColour(juce::Colour(0xff222230));
    }

    // 3. アナライザーリアルタイムスペクトラムの描画
    if (analyzer != nullptr)
    {
        std::vector<float> energies = analyzer->getEnergies();
        
        const int numBands = AnalyzerDSP::NumBands;

        // ハイブリッド補間 (1-60Hzは0.2Hz線形、60-200Hzは1Hz線形、200Hz-25kHzは対数)
        auto getInterpolatedDb = [&](double f, const std::vector<float>& srcEnergies) -> float
        {
            double idx = 0.0;
            if (f <= 1.0)
            {
                idx = 0.0;
            }
            else if (f < 60.0)
            {
                // 1.0Hz〜60.0Hz (0.2Hzステップ, バンド0-295)
                idx = (f - 1.0) / 0.2;
            }
            else if (f < 200.0)
            {
                // 60.0Hz〜200.0Hz (1.0Hzステップ, バンド295-435)
                idx = 295.0 + (f - 60.0);
            }
            else
            {
                // 200Hz〜25000Hz (対数等間隔, バンド435〜numBands-1)
                double logF = std::log(f);
                double log200 = std::log(200.0);
                double log25000 = std::log(std::min(25000.0, currentSampleRate * 0.45));
                
                if (log25000 <= log200) log25000 = log200 + 1.0;
                
                double ratio = (logF - log200) / (log25000 - log200);
                idx = 435.0 + ratio * (numBands - 1 - 435);
            }
            
            double clampedIdx = std::clamp(idx, 0.0, static_cast<double>(numBands - 1));
            int idx0 = static_cast<int>(std::floor(clampedIdx));
            int idx1 = std::clamp(idx0 + 1, 0, numBands - 1);
            double frac = clampedIdx - idx0;
            
            return srcEnergies[static_cast<size_t>(idx0)] * (1.0f - static_cast<float>(frac)) 
                 + srcEnergies[static_cast<size_t>(idx1)] * static_cast<float>(frac);
        };

        juce::Path analyzerPath;
        juce::Path strokePath;
        juce::Path holdPath;
        bool started = false;
        
        std::vector<float> holdEnergies = analyzer->getHoldEnergies();

        for (int i = 0; i < w; ++i)
        {
            float f = xToLogF(static_cast<float>(i));
            float magDb = getInterpolatedDb(f, energies);
            float holdDb = getInterpolatedDb(f, holdEnergies);
            float y = analyzerGainToY(magDb);
            float holdY = analyzerGainToY(holdDb);
            y = std::clamp(y, 0.0f, static_cast<float>(h));
            holdY = std::clamp(holdY, 0.0f, static_cast<float>(h));
            
            if (!started)
            {
                analyzerPath.startNewSubPath(static_cast<float>(i), static_cast<float>(h));
                analyzerPath.lineTo(static_cast<float>(i), y);
                strokePath.startNewSubPath(static_cast<float>(i), y);
                holdPath.startNewSubPath(static_cast<float>(i), holdY);
                started = true;
            }
            else
            {
                analyzerPath.lineTo(static_cast<float>(i), y);
                strokePath.lineTo(static_cast<float>(i), y);
                holdPath.lineTo(static_cast<float>(i), holdY);
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

            // Peak Hold描画 (少し明るめのアクセントカラーで細い線)
            g.setColour(pal.bell2.withAlpha(0.8f));
            g.strokePath(holdPath, juce::PathStrokeType(1.0f));
        }
    }

    // 4. EQ特性カーブの再計算と描画
    if (pathNeedsRecalculation || cachedResponsePath.isEmpty() || precomputedFreqs.size() != static_cast<size_t>(w))
    {
        precomputeFrequencies();

        cachedResponsePath.clear();

        if (processor != nullptr)
        {
            // 高Qの鋭いピーク/ノッチがピクセルの隙間に落ちて描画から消えないよう、
            // 1ピクセルあたり4点で評価し、偏差(|dB|)が最大の点を採用する
            constexpr int OverSample = 4;
            const int numEval = w * OverSample;
            curveFreqs.resize(static_cast<size_t>(numEval));
            curveMags.resize(static_cast<size_t>(numEval));

            for (int i = 0; i < numEval; ++i)
                curveFreqs[static_cast<size_t>(i)] = static_cast<double>(xToLogF(static_cast<float>(i) / static_cast<float>(OverSample)));

            // 各Bellの中心周波数ちょうどの点も評価リストに追加する。
            // どれだけ狭いピーク/ノッチでも先端の正確な深さが必ず描画される。
            int numBellExtras = 0;
            int bellExtraIdx[4] = { -1, -1, -1, -1 };
            for (int bIdx = 0; bIdx < 4; ++bIdx)
            {
                if (bellParams[static_cast<size_t>(bIdx)].active
                    && bellParams[static_cast<size_t>(bIdx)].freq >= currentMinF
                    && bellParams[static_cast<size_t>(bIdx)].freq <= currentMaxF)
                {
                    bellExtraIdx[bIdx] = numEval + numBellExtras;
                    curveFreqs.push_back(bellParams[static_cast<size_t>(bIdx)].freq);
                    ++numBellExtras;
                }
            }
            curveMags.resize(curveFreqs.size());

            // 1回のロックで全ポイントを一括評価。
            // (以前は1ピクセルごとに個別取得していたため、描画途中でパラメータ更新が
            //  割り込むとカーブが途中で切り替わって壊れることがあった)
            processor->getEQ().getMagnitudeCurve(curveFreqs.data(), curveMags.data(), static_cast<int>(curveFreqs.size()));

            auto magToDb = [](double mag) -> double
            {
                if (std::isnan(mag) || std::isinf(mag)) mag = 1.0; // NaN/inf ガード
                return 20.0 * std::log10(std::max(mag, 1e-10));
            };

            // ピクセルごとに偏差最大の評価点を採用
            std::vector<double> pixelDb(static_cast<size_t>(w), 0.0);
            for (int x = 0; x < w; ++x)
            {
                double bestDb = 0.0;
                double bestAbs = -1.0;
                for (int k = 0; k < OverSample; ++k)
                {
                    double db = magToDb(curveMags[static_cast<size_t>(x * OverSample + k)]);
                    if (std::abs(db) > bestAbs)
                    {
                        bestAbs = std::abs(db);
                        bestDb = db;
                    }
                }
                pixelDb[static_cast<size_t>(x)] = bestDb;
            }

            // Bell中心周波数の正確な値で該当ピクセルを上書き
            for (int bIdx = 0; bIdx < 4; ++bIdx)
            {
                if (bellExtraIdx[bIdx] < 0) continue;
                int px = static_cast<int>(logFToX(static_cast<float>(bellParams[static_cast<size_t>(bIdx)].freq)));
                px = std::clamp(px, 0, w - 1);
                double db = magToDb(curveMags[static_cast<size_t>(bellExtraIdx[bIdx])]);
                if (std::abs(db) > std::abs(pixelDb[static_cast<size_t>(px)]))
                    pixelDb[static_cast<size_t>(px)] = db;
            }

            bool started = false;
            for (int x = 0; x < w; ++x)
            {
                float y = gainToY(static_cast<float>(pixelDb[static_cast<size_t>(x)]));
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
        }

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
            
            double w_sum = 0.0;
            double r_sum = 0.0, g_sum = 0.0, b_sum = 0.0;
            
            for (int bIdx = 0; bIdx < 4; ++bIdx)
            {
                if (bellParams[bIdx].active && std::abs(bellParams[bIdx].gain) > 0.01)
                {
                    double omega = std::tan(std::numbers::pi * f / currentSampleRate);
                    double g_val = std::tan(std::numbers::pi * bellParams[bIdx].freq / currentSampleRate);
                    
                    if (g_val > 0.0)
                    {
                        double R = omega / g_val;
                        double R2 = R * R;
                        double q_safe = std::max(bellParams[bIdx].q, 0.05);
                        double V = std::pow(10.0, bellParams[bIdx].gain / 20.0);
                        
                        double r_over_q = R / q_safe;
                        double termNum = r_over_q * V;
                        double termDen = r_over_q / V;
                        
                        double numSq = (1.0 - R2) * (1.0 - R2) + termNum * termNum;
                        double denSq = (1.0 - R2) * (1.0 - R2) + termDen * termDen;
                        
                        if (bellParams[bIdx].gain < 0.0)
                        {
                            std::swap(numSq, denSq);
                        }
                        
                        double bellMag = 1.0;
                        if (denSq > 0.0)
                        {
                            bellMag = std::sqrt(numSq / denSq);
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

    // 6. 選択帯域の強調ハイライト（Hover含む）
    if (activeDragBand != -1 || isHovering)
    {
        juce::Colour bellColors[4] = { pal.bell1, pal.bell2, pal.bell3, pal.bell4 };
        juce::Colour c = (activeDragBand == 0) ? pal.lowcut :
                         (activeDragBand == 1) ? pal.highcut :
                         (activeDragBand >= 2 && activeDragBand <= 5) ? bellColors[activeDragBand - 2] :
                         juce::Colours::white;
                         
        if (isHovering && activeDragBand == -1) c = juce::Colours::white.withAlpha(0.7f);

        g.setColour(c.withAlpha(0.05f));
        g.fillRect(0, 0, w, h);
    }
    
    // 7. Hover時の周波数/Keyテキスト描画
    if (mouseX >= 0 && mouseY >= 0 && hoverText.isNotEmpty())
    {
        g.setColour(juce::Colour(0xff2a2a3e).withAlpha(0.8f));
        int textW = 120;
        int textH = 20;
        int rx = mouseX + 10;
        int ry = mouseY - 25;
        if (rx + textW > w) rx = mouseX - textW - 10;
        if (ry < 0) ry = mouseY + 10;
        g.fillRoundedRectangle(static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(textW), static_cast<float>(textH), 4.0f);
        
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions("Outfit", 12.0f, juce::Font::bold)));
        g.drawText(hoverText, rx, ry, textW, textH, juce::Justification::centred);
    }

    // 5. EQコントロールポイントの描画
    drawEQPoints(g);
}

void FreqResponseDisplay::drawEQPoints(juce::Graphics& g)
{
    const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
    
    // 各バンドのコントロールポイント座標を計算して描画
    // Off時のゴースト表示用アルファ (ダブルクリックでOnに戻せるよう位置を示す)
    const float offAlpha = 0.25f;

    // 0: LowCut (Offでも淡色で表示)
    {
        float x = logFToX(static_cast<float>(currentCutoffHz));
        float y = cutPointY(currentCutoffHz);

        if (currentLowcutEnable)
        {
            g.setColour(selectedBandIdx == 0 ? juce::Colours::white : pal.lowcut);
            g.fillEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f);
            g.setColour(pal.lowcut.withAlpha(0.4f));
            g.drawEllipse(x - 9.0f, y - 9.0f, 18.0f, 18.0f, 1.5f);
            g.setColour(pal.text.withAlpha(0.8f));
        }
        else
        {
            g.setColour(pal.lowcut.withAlpha(offAlpha));
            g.drawEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.5f);
            g.setColour(pal.text.withAlpha(offAlpha));
        }

        g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::bold)));
        g.drawText("LC", static_cast<int>(x) - 15, static_cast<int>(y) - 22, 30, 10, juce::Justification::centred);
    }

    // 1: HighCut (Offでも淡色で表示)
    {
        float x = logFToX(static_cast<float>(currentHighCutFreq));
        float y = cutPointY(currentHighCutFreq);

        if (currentHighCutEnable)
        {
            g.setColour(selectedBandIdx == 1 ? juce::Colours::white : pal.highcut);
            g.fillEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f);
            g.setColour(pal.highcut.withAlpha(0.4f));
            g.drawEllipse(x - 9.0f, y - 9.0f, 18.0f, 18.0f, 1.5f);
            g.setColour(pal.text.withAlpha(0.8f));
        }
        else
        {
            g.setColour(pal.highcut.withAlpha(offAlpha));
            g.drawEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.5f);
            g.setColour(pal.text.withAlpha(offAlpha));
        }

        g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::bold)));
        g.drawText("HC", static_cast<int>(x) - 15, static_cast<int>(y) - 22, 30, 10, juce::Justification::centred);
    }

    // 2..5: Bells (Offでも淡色で表示)
    juce::Colour bellColors[4] = { pal.bell1, pal.bell2, pal.bell3, pal.bell4 };
    juce::String bellNames[4] = { "B1", "B2", "B3", "B4" };

    for (int i = 0; i < 4; ++i)
    {
        float x = logFToX(static_cast<float>(bellParams[i].freq));
        float y = gainToY(static_cast<float>(bellParams[i].gain));

        if (bellParams[i].active)
        {
            g.setColour(selectedBandIdx == (i + 2) ? juce::Colours::white : bellColors[i]);
            g.fillEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f);
            g.setColour(bellColors[i].withAlpha(0.4f));
            g.drawEllipse(x - 9.0f, y - 9.0f, 18.0f, 18.0f, 1.5f);
            g.setColour(pal.text.withAlpha(0.8f));
        }
        else
        {
            g.setColour(bellColors[i].withAlpha(offAlpha));
            g.drawEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.5f);
            g.setColour(pal.text.withAlpha(offAlpha));
        }

        g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::bold)));
        g.drawText(bellNames[i], static_cast<int>(x) - 15, static_cast<int>(y) - 22, 30, 10, juce::Justification::centred);
    }
}

void FreqResponseDisplay::mouseDown(const juce::MouseEvent& e)
{
    activeDragBand = -1;
    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);

    // 各ポイントへのクリック判定 (半径15ピクセル以内ならキャッチ)
    const float grabRadius = 15.0f;

    // 1. LowCut
    if (currentLowcutEnable)
    {
        float x = logFToX(static_cast<float>(currentCutoffHz));
        float y = cutPointY(currentCutoffHz);
        if (std::hypot(mx - x, my - y) < grabRadius)
        {
            activeDragBand = 0;
            if (editor != nullptr) editor->selectBand(HighPrecisionEQAudioProcessorEditor::SelectedBand::LowCut);
            if (e.mods.isShiftDown() && processor != nullptr)
            {
                processor->setSoloMode(true, static_cast<float>(currentCutoffHz), 2.0f);
            }
            repaint();
            return;
        }
    }

    // 2. HighCut
    if (currentHighCutEnable)
    {
        float x = logFToX(static_cast<float>(currentHighCutFreq));
        float y = cutPointY(currentHighCutFreq);
        if (std::hypot(mx - x, my - y) < grabRadius)
        {
            activeDragBand = 1;
            if (editor != nullptr) editor->selectBand(HighPrecisionEQAudioProcessorEditor::SelectedBand::HighCut);
            if (e.mods.isShiftDown() && processor != nullptr)
            {
                processor->setSoloMode(true, static_cast<float>(currentHighCutFreq), 2.0f);
            }
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
            if (std::hypot(mx - x, my - y) < grabRadius)
            {
                activeDragBand = i + 2;
                if (editor != nullptr) editor->selectBand(static_cast<HighPrecisionEQAudioProcessorEditor::SelectedBand>(i + 2));
                if (e.mods.isShiftDown() && processor != nullptr)
                {
                    processor->setSoloMode(true, static_cast<float>(bellParams[i].freq), std::max(2.0f, static_cast<float>(bellParams[i].q)));
                }
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

    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);

    // 画面外へのドラッグ制限
    mx = std::clamp(mx, 0.0f, static_cast<float>(getWidth()));
    my = std::clamp(my, 0.0f, static_cast<float>(getHeight()));

    float dragFreq = xToLogF(mx);

    // ズームイン表示中かつ60Hz以下のとき、0.2Hz刻みにスナップ
    if (currentMaxF / currentMinF < 10.0f && dragFreq <= 60.0f)
    {
        dragFreq = std::round(dragFreq / 0.2f) * 0.2f;
    }

    float dragGain = yToGain(my);

    if (processor != nullptr && activeDragBand != -1)
    {
        auto& apvts = processor->apvts;
        float targetFreq = dragFreq;

        if (activeDragBand == 0) // LowCut (横ドラッグのみ。カットゲインは廃止)
        {
            targetFreq = std::clamp(dragFreq, 1.0f, 500.0f);

            auto rangeF = apvts.getParameterRange("cutoffHz");
            apvts.getParameter("cutoffHz")->setValueNotifyingHost(rangeF.convertTo0to1(targetFreq));
        }
        else if (activeDragBand == 1) // HighCut
        {
            targetFreq = std::clamp(dragFreq, 20.0f, 25000.0f);

            auto rangeF = apvts.getParameterRange("highcut_freq");
            apvts.getParameter("highcut_freq")->setValueNotifyingHost(rangeF.convertTo0to1(targetFreq));
        }
        else if (activeDragBand >= 2 && activeDragBand <= 5) // Bells
        {
            int idx = activeDragBand - 1; // Bell1..4 (1-indexed suffix: 1..4)
            juce::String idSuffix = juce::String(idx);
            
            targetFreq = std::clamp(dragFreq, 10.0f, 25000.0f);
            float targetGain = std::clamp(dragGain, -18.0f, 18.0f);

            juce::String freqID = "bell_freq_" + idSuffix;
            juce::String gainID = "bell_gain_" + idSuffix;

            auto rangeF = apvts.getParameterRange(freqID);
            apvts.getParameter(freqID)->setValueNotifyingHost(rangeF.convertTo0to1(targetFreq));

            auto rangeG = apvts.getParameterRange(gainID);
            apvts.getParameter(gainID)->setValueNotifyingHost(rangeG.convertTo0to1(targetGain));
        }

        // Shiftドラッグ中のSolo更新
        if (e.mods.isShiftDown())
        {
            float soloQ = 2.0f;
            if (activeDragBand >= 2 && activeDragBand <= 5)
            {
                soloQ = std::max(2.0f, static_cast<float>(bellParams[activeDragBand - 2].q));
            }
            processor->setSoloMode(true, targetFreq, soloQ);
        }
        else
        {
            processor->setSoloMode(false, 0.0f, 0.0f);
        }
    }
    else
    {
        // グラフ全体のズーム／シフト操作
        // ドラッグによる対数ピッチシフト
        float dx = static_cast<float>(e.getDistanceFromDragStartX());

        // 左ドラッグ: EQカーブの左右移動のみ (上下シフト・右ドラッグズームは廃止)
        float shiftFactor = std::pow(10.0f, -dx / static_cast<float>(getWidth()) * 0.5f);
        currentMinF = std::max(dragStartMinF * shiftFactor, 1.0f); // 1Hzまで
        currentMaxF = std::min(dragStartMaxF * shiftFactor, 24000.0f);

        pathNeedsRecalculation = true;
        repaint();
    }
}

void FreqResponseDisplay::mouseUp(const juce::MouseEvent&)
{
    activeDragBand = -1;
    if (processor != nullptr)
    {
        processor->setSoloMode(false, 0.0f, 0.0f);
    }
}

void FreqResponseDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (processor == nullptr) return;
    auto& apvts = processor->apvts;

    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);
    const float grabRadius = 15.0f;
    int targetBand = -1;

    // ダブルクリックはOn/Offトグル用。Off中の点も表示しているので、
    // Enable/active に関係なく当たり判定する (Onに戻せるようにするため)。

    // 1. LowCut
    {
        float x = logFToX(static_cast<float>(currentCutoffHz));
        float y = cutPointY(currentCutoffHz);
        if (std::hypot(mx - x, my - y) < grabRadius)
        {
            targetBand = 0;
        }
    }

    // 2. HighCut
    if (targetBand == -1)
    {
        float x = logFToX(static_cast<float>(currentHighCutFreq));
        float y = cutPointY(currentHighCutFreq);
        if (std::hypot(mx - x, my - y) < grabRadius)
        {
            targetBand = 1;
        }
    }

    // 3. Bells
    if (targetBand == -1)
    {
        for (int i = 0; i < 4; ++i)
        {
            float x = logFToX(static_cast<float>(bellParams[i].freq));
            float y = gainToY(static_cast<float>(bellParams[i].gain));
            if (std::hypot(mx - x, my - y) < grabRadius)
            {
                targetBand = i + 2;
                break;
            }
        }
    }

    if (targetBand != -1)
    {
        juce::String paramID;
        if (targetBand == 0) paramID = "lowcut_enable";
        else if (targetBand == 1) paramID = "highcut_enable";
        else paramID = "bell_enable_" + juce::String(targetBand - 1);

        auto* param = apvts.getParameter(paramID);
        if (param != nullptr)
        {
            bool currentVal = param->getValue() > 0.5f;
            param->setValueNotifyingHost(currentVal ? 0.0f : 1.0f);
        }
    }
    else
    {
        // ダブルクリックでズーム初期化
        currentMinF = 1.0f;
        currentMaxF = 25000.0f;
        currentMinDb = -12.0f;
        currentMaxDb = 12.0f;
        analyzerGainOffsetDb = 0.0f;
        pathNeedsRecalculation = true;
        repaint();
    }
}

void FreqResponseDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // 1. Ctrlキー押下時の周波数軸ズーム
    if (e.mods.isCtrlDown())
    {
        float mx = static_cast<float>(e.x);
        float targetF = xToLogF(mx);
        
        float zoomFactor = std::pow(1.15f, -wheel.deltaY); // 上スクロールで拡大、下で縮小
        
        float centerLog = std::log10(targetF);
        float minLog = std::log10(currentMinF);
        float maxLog = std::log10(currentMaxF);
        
        float newMinLog = centerLog - (centerLog - minLog) * zoomFactor;
        float newMaxLog = centerLog + (maxLog - centerLog) * zoomFactor;
        
        float newMinF = std::pow(10.0f, newMinLog);
        float newMaxF = std::pow(10.0f, newMaxLog);
        
        float ratio = newMaxF / newMinF;
        
        // ズーム比制限 (1.002f 〜 25000.0f)
        if (ratio >= 1.002f && ratio <= 25000.0f)
        {
            currentMinF = std::max(newMinF, 1.0f);
            currentMaxF = std::min(newMaxF, 25000.0f);
            pathNeedsRecalculation = true;
            repaint();
        }
        return;
    }

    // 2. 通常のスクロール (Q幅やスロープの調整)
    if (processor == nullptr) return;
    auto& apvts = processor->apvts;

    // マウスカーソルがEQポイントの上にあるか判定
    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);
    const float grabRadius = 15.0f;
    int targetBand = -1;

    // 1. LowCut
    if (currentLowcutEnable)
    {
        float x = logFToX(static_cast<float>(currentCutoffHz));
        float y = cutPointY(currentCutoffHz);
        if (std::hypot(mx - x, my - y) < grabRadius)
        {
            targetBand = 0;
        }
    }

    // 2. HighCut
    if (targetBand == -1 && currentHighCutEnable)
    {
        float x = logFToX(static_cast<float>(currentHighCutFreq));
        float y = cutPointY(currentHighCutFreq);
        if (std::hypot(mx - x, my - y) < grabRadius)
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
                if (std::hypot(mx - x, my - y) < grabRadius)
                {
                    targetBand = i + 2;
                    break;
                }
            }
        }
    }

    // どのEQポイントの上でもない場合は、EQカーブを左右に移動する (上下操作は廃止)
    if (targetBand == -1)
    {
        // ホイール量を対数周波数シフトに変換 (左ドラッグと同方向の感覚)
        float shiftFactor = std::pow(10.0f, -wheel.deltaY * 0.1f);
        currentMinF = std::max(currentMinF * shiftFactor, 1.0f);
        currentMaxF = std::min(currentMaxF * shiftFactor, 24000.0f);
        pathNeedsRecalculation = true;
        repaint();
        return;
    }

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

void FreqResponseDisplay::mouseEnter(const juce::MouseEvent&)
{
    isHovering = true;
    repaint();
}

void FreqResponseDisplay::mouseExit(const juce::MouseEvent&)
{
    isHovering = false;
    hoverText.clear();
    mouseX = -1;
    mouseY = -1;
    repaint();
}

void FreqResponseDisplay::mouseMove(const juce::MouseEvent& e)
{
    mouseX = e.x;
    mouseY = e.y;
    
    if (mouseX >= 0 && mouseX < getWidth() && mouseY >= 0 && mouseY < getHeight())
    {
        isHovering = true;
        float f = xToLogF(static_cast<float>(mouseX));
        
        // MIDI Note calculation
        int midiNote = static_cast<int>(std::round(12.0 * std::log2(f / 440.0) + 69.0));
        juce::String noteName = "";
        
        if (midiNote >= 0 && midiNote <= 127)
        {
            const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            int octave = (midiNote / 12) - 1;
            int noteIdx = midiNote % 12;
            noteName = juce::String(noteNames[noteIdx]) + juce::String(octave);
        }
        
        hoverText = juce::String(f, 1) + " Hz / " + noteName;
    }
    else
    {
        hoverText.clear();
        mouseX = -1;
        mouseY = -1;
    }
    
    repaint();
}
