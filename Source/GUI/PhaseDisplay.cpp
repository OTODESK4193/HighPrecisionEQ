#include "PhaseDisplay.h"
#include "AnalyzerDSP.h"
#include "ColorPalette.h"
#include "MinimumPhaseEQ.h"
#include "PluginProcessor.h"

PhaseDisplay::PhaseDisplay()
{
    vblankAttachment = std::make_unique<juce::VBlankAttachment>(this, [this]() { vblankUpdate(); });
}

PhaseDisplay::~PhaseDisplay()
{
}

void PhaseDisplay::vblankUpdate()
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

void PhaseDisplay::updateParameters(double cutoffHz, int order, double gainDb, bool lowcutEnable,
                                    double highCutFreq, int highCutOrder, double highCutGainDb, bool highCutEnable,
                                    double sampleRate, const std::array<FreqResponseDisplay::BellParam, 4>& bells)
{
    currentCutoff = cutoffHz;
    currentOrder = order;
    currentGainDb = gainDb;
    currentLowcutEnable = lowcutEnable;
    currentHighCutFreq = highCutFreq;
    currentHighCutOrder = highCutOrder;
    currentHighCutGainDb = highCutGainDb;
    currentHighCutEnable = highCutEnable;
    currentSampleRate = sampleRate;
    bellParams = bells;

    repaint();
}

void PhaseDisplay::setColorPaletteIndex(int index)
{
    currentPaletteIdx = index;
    repaint();
}

void PhaseDisplay::setAnalyzer(AnalyzerDSP* anz)
{
    analyzer = anz;
}

void PhaseDisplay::setResponseDisplay(FreqResponseDisplay* disp)
{
    responseDisplay = disp;
}

void PhaseDisplay::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // 位相軸 (Y軸) のズームファクター調整
    phaseZoomFactor = std::clamp(phaseZoomFactor * std::pow(1.1f, wheel.deltaY), 0.2f, 5.0f);
    repaint();
}

void PhaseDisplay::paint(juce::Graphics& g)
{
    const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
    
    // 背景
    g.fillAll(juce::Colour(0xff0d0d15));

    // サンプリングレートの動的同期
    if (responseDisplay != nullptr && responseDisplay->getProcessor() != nullptr)
    {
        double psr = responseDisplay->getProcessor()->getSampleRate();
        if (psr > 8000.0 && std::abs(currentSampleRate - psr) > 0.01)
        {
            currentSampleRate = psr;
        }
    }

    auto bounds = getLocalBounds();
    int w = bounds.getWidth();
    int h = bounds.getHeight();

    float minF = responseDisplay != nullptr ? responseDisplay->getMinF() : 1.0f;
    float maxF = responseDisplay != nullptr ? responseDisplay->getMaxF() : 25000.0f;

    // 1. 周波数グリッド描画 (ズームに応じた適応型動的グリッド)
    float range = maxF - minF;
    g.setColour(juce::Colour(0xff222230));
    
    auto logFToX = [w, minF, maxF](float f) {
        float logMin = std::log10(minF);
        float logMax = std::log10(maxF);
        float val = (std::log10(f) - logMin) / (logMax - logMin);
        return val * static_cast<float>(w);
    };

    if (range <= 1000.0f)
    {
        // 線形ステップグリッドの算出
        float gridStep = 100.0f;
        if (range <= 2.0f)        gridStep = 0.2f;
        else if (range <= 10.0f)   gridStep = 1.0f;
        else if (range <= 50.0f)   gridStep = 5.0f;
        else if (range <= 200.0f)  gridStep = 20.0f;

        float firstGrid = std::ceil(minF / gridStep) * gridStep;
        float lastLabelX = -100.0f;

        for (float f = firstGrid; f <= maxF + 0.0001f; f += gridStep)
        {
            if (f >= minF && f <= maxF)
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
            if (f >= minF && f <= maxF)
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

    // 2. 位相グリッド描画 (Rad または Degree)
    // 基準は -pi 〜 +pi (ズームファクターによる拡大縮小)
    float maxPhase = static_cast<float>(std::numbers::pi) * phaseZoomFactor;
    float minPhase = -maxPhase;

    auto phaseToY = [h, minPhase, maxPhase](float rad) {
        float val = (rad - minPhase) / (maxPhase - minPhase);
        return (1.0f - val) * static_cast<float>(h);
    };

    g.setColour(juce::Colour(0xff222230));
    float phaseSteps[] = {
        static_cast<float>(std::numbers::pi),
        static_cast<float>(std::numbers::pi / 2.0),
        0.0f,
        static_cast<float>(-std::numbers::pi / 2.0),
        static_cast<float>(-std::numbers::pi)
    };

    for (float p : phaseSteps)
    {
        if (p >= minPhase && p <= maxPhase)
        {
            float y = phaseToY(p);
            g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(w));

            g.setColour(juce::Colour(0xff555570));
            g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::plain)));
            // 度数法で表示
            int deg = static_cast<int>(std::round(p * 180.0 / std::numbers::pi));
            g.drawText(juce::String(deg) + " deg", 5, static_cast<int>(y) - 12, 60, 12, juce::Justification::left);
            g.setColour(juce::Colour(0xff222230));
        }
    }

    // 3. アナライザ入力スペクトラム描画 (位相表示時の背景ガイドとして薄く描画)
    drawAnalyzerSpectrum(g, bounds);

    // 4. 位相遅延カーブの描画
    drawPhaseCurve(g, bounds, pal.bell1); // シアンで描画
}

void PhaseDisplay::drawAnalyzerSpectrum(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (analyzer == nullptr) return;

    float w = static_cast<float>(bounds.getWidth());
    float h = static_cast<float>(bounds.getHeight());
    float xOffset = static_cast<float>(bounds.getX());

    std::vector<float> energies = analyzer->getEnergies();
    
    const int numBands = AnalyzerDSP::NumBands;

    // ハイブリッド補間 (1-200Hzは線形、200Hz-25kHzは対数)
    auto getInterpolatedDb = [&](double f) -> float
    {
        double idx = 0.0;
        if (f <= 1.0)
        {
            idx = 0.0;
        }
        else if (f < 200.0)
        {
            // 1Hz〜200Hz (線形等間隔, バンド0-199)
            idx = f - 1.0;
        }
        else
        {
            // 200Hz〜25000Hz (対数等間隔, バンド200-479)
            double logF = std::log(f);
            double log200 = std::log(200.0);
            double log25000 = std::log(std::min(25000.0, currentSampleRate * 0.45));
            
            if (log25000 <= log200) log25000 = log200 + 1.0;
            
            double ratio = (logF - log200) / (log25000 - log200);
            idx = 199.0 + ratio * 280.0;
        }
        
        double clampedIdx = std::clamp(idx, 0.0, static_cast<double>(numBands - 1));
        int idx0 = static_cast<int>(std::floor(clampedIdx));
        int idx1 = std::clamp(idx0 + 1, 0, numBands - 1);
        double frac = clampedIdx - idx0;
        
        return energies[static_cast<size_t>(idx0)] * (1.0f - static_cast<float>(frac)) 
             + energies[static_cast<size_t>(idx1)] * static_cast<float>(frac);
    };

    juce::Path fillPath;
    juce::Path strokePath;
    bool started = false;

    auto gainToY = [h](float db) {
        float minDb = -70.0f;
        float maxDb = 10.0f;
        float val = (db - minDb) / (maxDb - minDb);
        return (1.0f - val) * h;
    };

    for (int i = 0; i < static_cast<int>(w); ++i)
    {
        float f = responseDisplay != nullptr ? responseDisplay->xToLogF(static_cast<float>(i)) : 10.0f;
        float magDb = getInterpolatedDb(f);
        float y = gainToY(magDb);
        y = std::clamp(y, 0.0f, h);

        if (!started)
        {
            fillPath.startNewSubPath(xOffset + i, h);
            fillPath.lineTo(xOffset + i, y);
            strokePath.startNewSubPath(xOffset + i, y);
            started = true;
        }
        else
        {
            fillPath.lineTo(xOffset + i, y);
            strokePath.lineTo(xOffset + i, y);
        }
    }

    if (started)
    {
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

        g.setColour(pal.anaStroke.withAlpha(0.4f));
        g.strokePath(strokePath, juce::PathStrokeType(1.0f));
    }
}

void PhaseDisplay::drawPhaseCurve(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour color)
{
    int w = bounds.getWidth();
    int h = bounds.getHeight();

    float minF = responseDisplay != nullptr ? responseDisplay->getMinF() : 1.0f;
    float maxF = responseDisplay != nullptr ? responseDisplay->getMaxF() : 25000.0f;

    float maxPhase = static_cast<float>(std::numbers::pi) * phaseZoomFactor;
    float minPhase = -maxPhase;

    auto xToLogF = [minF, maxF, w](float x) {
        float logMin = std::log10(minF);
        float logMax = std::log10(maxF);
        float val = x / static_cast<float>(w);
        return std::pow(10.0f, logMin + val * (logMax - logMin));
    };

    auto phaseToY = [h, minPhase, maxPhase](float rad) {
        float val = (rad - minPhase) / (maxPhase - minPhase);
        return (1.0f - val) * static_cast<float>(h);
    };

    // ローカルで MinimumPhaseEQ の位相応答を正確に算出
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
    localEQ.updateParameters(currentCutoff, currentOrder, currentLowcutEnable, currentGainDb,
                             currentHighCutFreq, currentHighCutOrder, currentHighCutEnable, currentHighCutGainDb,
                             localBells);

    // 位相遅延計算のサブルーチン
    // 複素応答の偏角の累積値 (アンラップ) をプロット
    juce::Path phasePath;
    bool started = false;

    // 前回の偏角を保持してアンラップ処理
    double lastPhase = 0.0;
    double unwrapOffset = 0.0;

    for (int x = 0; x < w; ++x)
    {
        float f = xToLogF(static_cast<float>(x));
        
        // 各セクションの複素偏角の総和を計算
        // ここでは MinimumPhaseEQ 内のアクティブセクションの位相を求める
        // 各 FilterSection は type, freq, gain, q などを保持している。
        // ローカルインスタンスのパラメータが反映されているため、そこから位相を計算する
        // MinimumPhaseEQ 内で定義されている 2次/1次 フィルタセクションの複素特性の合計値
        // 実際には、伝達関数 H(z) を直接使って計算可能。
        // パラメータに基づいて IIR の位相を合計します。
        
        double totalPhase = 0.0;

        // 1. LowCut
        if (currentLowcutEnable)
        {
            // LowCut (HPF)
            // 2次 Butterworth (または 1次) がカスケードされている (order = slope / 12)
            // Butterworth の Q値は各セクションで異なる (SOSCoefficients参照)
            // ここでは簡易的に、現在の order 分のカスケードとして計算
            int numSections = currentOrder;
            for (int s = 0; s < numSections; ++s)
            {
                // セクションごとのQ値 (Butterworth極配置)
                double qVal = 0.70710678;
                if (numSections > 1)
                {
                    double angle = std::numbers::pi * (2.0 * s + 1.0) / (4.0 * numSections);
                    qVal = 1.0 / (2.0 * std::sin(angle));
                }

                // HPF 複素偏角
                double f_eval = std::clamp(static_cast<double>(f), 1.0, currentSampleRate * 0.4999);
                double w_ang = 2.0 * std::numbers::pi * f_eval / currentSampleRate;
                double cosw = std::cos(w_ang);
                double alpha = std::sin(2.0 * std::numbers::pi * currentCutoff / currentSampleRate) / (2.0 * qVal);
                double cosw0 = std::cos(2.0 * std::numbers::pi * currentCutoff / currentSampleRate);
                
                double a0 = 1.0 + alpha;
                double b0 = (1.0 + cosw0) / 2.0 / a0;
                double b1 = -(1.0 + cosw0) / a0;
                double b2 = (1.0 + cosw0) / 2.0 / a0;
                double a1 = -2.0 * cosw0 / a0;
                double a2 = (1.0 - alpha) / a0;

                double numRe = b0 + b1 * cosw + b2 * std::cos(2.0 * w_ang);
                double numIm = -b1 * std::sin(w_ang) - b2 * std::sin(2.0 * w_ang);
                double denRe = 1.0 + a1 * cosw + a2 * std::cos(2.0 * w_ang);
                double denIm = -a1 * std::sin(w_ang) - a2 * std::sin(2.0 * w_ang);

                double denMagSq = denRe * denRe + denIm * denIm;
                if (denMagSq > 0.0)
                {
                    double re = (numRe * denRe + numIm * denIm) / denMagSq;
                    double im = (numIm * denRe - numRe * denIm) / denMagSq;
                    totalPhase += std::atan2(im, re);
                }
            }
        }

        // 2. HighCut
        if (currentHighCutEnable)
        {
            int numSections = currentHighCutOrder;
            for (int s = 0; s < numSections; ++s)
            {
                double qVal = 0.70710678;
                if (numSections > 1)
                {
                    double angle = std::numbers::pi * (2.0 * s + 1.0) / (4.0 * numSections);
                    qVal = 1.0 / (2.0 * std::sin(angle));
                }

                // LPF 複素偏角
                double f_eval = std::clamp(static_cast<double>(f), 1.0, currentSampleRate * 0.4999);
                double w_ang = 2.0 * std::numbers::pi * f_eval / currentSampleRate;
                double cosw = std::cos(w_ang);
                double alpha = std::sin(2.0 * std::numbers::pi * currentHighCutFreq / currentSampleRate) / (2.0 * qVal);
                double cosw0 = std::cos(2.0 * std::numbers::pi * currentHighCutFreq / currentSampleRate);

                double a0 = 1.0 + alpha;
                double b0 = (1.0 - cosw0) / 2.0 / a0;
                double b1 = (1.0 - cosw0) / a0;
                double b2 = (1.0 - cosw0) / 2.0 / a0;
                double a1 = -2.0 * cosw0 / a0;
                double a2 = (1.0 - alpha) / a0;

                double numRe = b0 + b1 * cosw + b2 * std::cos(2.0 * w_ang);
                double numIm = -b1 * std::sin(w_ang) - b2 * std::sin(2.0 * w_ang);
                double denRe = 1.0 + a1 * cosw + a2 * std::cos(2.0 * w_ang);
                double denIm = -a1 * std::sin(w_ang) - a2 * std::sin(2.0 * w_ang);

                double denMagSq = denRe * denRe + denIm * denIm;
                if (denMagSq > 0.0)
                {
                    double re = (numRe * denRe + numIm * denIm) / denMagSq;
                    double im = (numIm * denRe - numRe * denIm) / denMagSq;
                    totalPhase += std::atan2(im, re);
                }
            }
        }

        // 3. Bells
        for (int i = 0; i < 4; ++i)
        {
            if (bellParams[i].active)
            {
                double A = std::pow(10.0, bellParams[i].gain / 40.0);
                double w0 = 2.0 * std::numbers::pi * bellParams[i].freq / currentSampleRate;
                double alpha = std::sin(w0) / (2.0 * bellParams[i].q);
                
                double a0 = 1.0 + alpha / A;
                double b0 = (1.0 + alpha * A) / a0;
                double b1 = -2.0 * std::cos(w0) / a0;
                double b2 = (1.0 - alpha * A) / a0;
                double a1 = -2.0 * std::cos(w0) / a0;
                double a2 = (1.0 - alpha / A) / a0;

                double f_eval = std::clamp(static_cast<double>(f), 1.0, currentSampleRate * 0.4999);
                double w_ang = 2.0 * std::numbers::pi * f_eval / currentSampleRate;
                double cosw = std::cos(w_ang);
                
                double numRe = b0 + b1 * cosw + b2 * std::cos(2.0 * w_ang);
                double numIm = -b1 * std::sin(w_ang) - b2 * std::sin(2.0 * w_ang);
                double denRe = 1.0 + a1 * cosw + a2 * std::cos(2.0 * w_ang);
                double denIm = -a1 * std::sin(w_ang) - a2 * std::sin(2.0 * w_ang);

                double denMagSq = denRe * denRe + denIm * denIm;
                if (denMagSq > 0.0)
                {
                    double re = (numRe * denRe + numIm * denIm) / denMagSq;
                    double im = (numIm * denRe - numRe * denIm) / denMagSq;
                    totalPhase += std::atan2(im, re);
                }
            }
        }

        // アンラップ処理で不連続なジャンプを防ぐ
        if (started)
        {
            double diff = totalPhase - lastPhase;
            if (diff > std::numbers::pi)
                unwrapOffset -= 2.0 * std::numbers::pi;
            else if (diff < -std::numbers::pi)
                unwrapOffset += 2.0 * std::numbers::pi;
        }
        
        lastPhase = totalPhase;
        double unwrappedPhase = totalPhase + unwrapOffset;

        float y = phaseToY(static_cast<float>(unwrappedPhase));

        if (!started)
        {
            phasePath.startNewSubPath(static_cast<float>(x), y);
            started = true;
        }
        else
        {
            phasePath.lineTo(static_cast<float>(x), y);
        }
    }

    if (started)
    {
        g.setColour(color);
        g.strokePath(phasePath, juce::PathStrokeType(2.0f));
    }
}
