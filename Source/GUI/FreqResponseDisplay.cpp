#include "FreqResponseDisplay.h"
#include "../DSP/SOSCoefficients.h"
#include "AnalyzerDSP.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ColorPalette.h"

namespace
{
    // 周波数(Hz)から "123.4 Hz / C4" 形式の表示文字列を生成
    juce::String makeFreqNoteText(float f)
    {
        int midiNote = static_cast<int>(std::round(12.0 * std::log2(f / 440.0) + 69.0));
        juce::String noteName;
        if (midiNote >= 0 && midiNote <= 127)
        {
            const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            int octave = (midiNote / 12) - 1;
            int noteIdx = midiNote % 12;
            noteName = juce::String(noteNames[noteIdx]) + juce::String(octave);
        }
        return juce::String(f, 1) + " Hz / " + noteName;
    }
}

FreqResponseDisplay::FreqResponseDisplay()
{
    // VBlankでスムーズな描画同期
    vblankAttachment = std::make_unique<juce::VBlankAttachment>(this, [this]() { vblankUpdate(); });

    // ズーム/表示モードボタンの設定
    zoomInXBtn.setButtonText("H+");
    zoomOutXBtn.setButtonText("H-");

    addAndMakeVisible(zoomInXBtn);
    addAndMakeVisible(zoomOutXBtn);
    addAndMakeVisible(autoFitButton);
    addAndMakeVisible(relativeButton);

    auto configureZoomBtn = [](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0x22ffffff));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffa0a0c0));
        btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0x44ffffff));
    };

    configureZoomBtn(zoomInXBtn);
    configureZoomBtn(zoomOutXBtn);
    configureZoomBtn(autoFitButton);
    configureZoomBtn(relativeButton);

    // トグルボタン: 押下状態を色で示す (Auto-fit V / Relative-Flatten)
    autoFitButton.setClickingTogglesState(true);
    relativeButton.setClickingTogglesState(true);
    autoFitButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a6ea5));
    relativeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a6ea5));
    autoFitButton.setTooltip("Auto-fit V: 見えている帯域に縦軸を自動フィット");
    relativeButton.setTooltip("Relative/Flatten: 平均トレンドを引いて起伏を強調");

    autoFitButton.onClick = [this]() {
        autoFitV = autoFitButton.getToggleState();
        // ONにした瞬間、選択中ポイントを中央にある程度拡大して表示する
        if (autoFitV)
            focusOnSelectedBand();
        pathNeedsRecalculation = true;
        repaint();
    };
    relativeButton.onClick = [this]() {
        relativeMode = relativeButton.getToggleState();
        pathNeedsRecalculation = true;
        repaint();
    };

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

    precomputeFrequencies();
}

void FreqResponseDisplay::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto zoomRow = r.removeFromTop(20);

    // 右上にコンパクトに並べる (H+/H- ズーム と 縦軸モードトグル)
    relativeButton.setBounds(zoomRow.removeFromRight(48));
    zoomRow.removeFromRight(2);
    autoFitButton.setBounds(zoomRow.removeFromRight(56));
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
    float val = (gainDecibels - effCurveMinDb) / (effCurveMaxDb - effCurveMinDb);
    return (1.0f - val) * static_cast<float>(getHeight());
}

float FreqResponseDisplay::yToGain(float y) const
{
    float val = y / static_cast<float>(getHeight());
    return effCurveMinDb + (1.0f - val) * (effCurveMaxDb - effCurveMinDb);
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
    db = std::clamp(db, static_cast<double>(effCurveMinDb), static_cast<double>(effCurveMaxDb));
    return gainToY(static_cast<float>(db));
}

float FreqResponseDisplay::analyzerGainToY(float gainDecibels) const
{
    // アナライザーのゲイン描画範囲 (有効な縦窓。既定 -70〜+10dB、Auto-fit時は統一窓)
    float val = (gainDecibels - effAnaMinDb) / (effAnaMaxDb - effAnaMinDb);
    return (1.0f - val) * static_cast<float>(getHeight());
}

float FreqResponseDisplay::interpAnalyzerDb(double f, const std::vector<float>& src) const
{
    const int numBands = AnalyzerDSP::NumBands;
    if (static_cast<int>(src.size()) < numBands) return -120.0f;

    double idx = 0.0;
    if (f <= 1.0)                idx = 0.0;
    else if (f < 60.0)          idx = (f - 1.0) / 0.2;              // 1-60Hz: 0.2Hz刻み (バンド0-295)
    else if (f < 200.0)         idx = 295.0 + (f - 60.0);          // 60-200Hz: 1Hz刻み (295-435)
    else
    {
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
    float frac = static_cast<float>(clampedIdx - idx0);
    return src[static_cast<size_t>(idx0)] * (1.0f - frac) + src[static_cast<size_t>(idx1)] * frac;
}

void FreqResponseDisplay::updateVerticalWindows(const std::vector<float>& ana)
{
    const bool unified = autoFitV || relativeMode;

    if (!unified)
    {
        // 既定: 従来の二段スケール (EQカーブ ±12 / アナライザー -70〜+10)
        effCurveMinDb = currentMinDb;
        effCurveMaxDb = currentMaxDb;
        effAnaMinDb = -70.0f + analyzerGainOffsetDb;
        effAnaMaxDb =  10.0f + analyzerGainOffsetDb;
        return;
    }

    // --- 統一窓モード (Auto V / Flat) ---
    // アナライザー・EQカーブ・Hold・ポイントを同一の縦窓で描画する。
    // EQポイント(Bellゲイン)とカーブ(≒0dB)が必ず画面内に入るよう最低限のマージンを確保する。
    float gExt = 3.0f;
    for (int i = 0; i < 4; ++i)
        if (bellParams[i].active)
            gExt = std::max(gExt, std::abs(static_cast<float>(bellParams[i].gain)) + 2.0f);

    if (autoFitV)
    {
        // 可視域オートフィット: アナライザー内容 + EQ(0±gExt) を包む窓に合わせる
        float mn = -gExt, mx = gExt;
        if (!ana.empty())
            for (float v : ana) { mn = std::min(mn, v); mx = std::max(mx, v); }

        float pad = std::max(2.0f, (mx - mn) * 0.12f);
        float targetMin = mn - pad;
        float targetMax = mx + pad;

        const float minSpan = 6.0f;
        if (targetMax - targetMin < minSpan)
        {
            float c = 0.5f * (targetMin + targetMax);
            targetMin = c - 0.5f * minSpan;
            targetMax = c + 0.5f * minSpan;
        }

        // 時間方向に平滑化して窓のジッターを抑える
        const float a = 0.15f;
        viewMinDb += (targetMin - viewMinDb) * a;
        viewMaxDb += (targetMax - viewMaxDb) * a;
    }
    else // relativeMode のみ (オートフィット無し): 0対称の固定窓
    {
        float span = std::max(18.0f, gExt);
        if (!ana.empty())
        {
            float amax = 0.0f;
            for (float v : ana) amax = std::max(amax, std::abs(v));
            span = std::max(span, amax + 3.0f);
        }
        viewMinDb = -span;
        viewMaxDb =  span;
    }

    effCurveMinDb = viewMinDb; effCurveMaxDb = viewMaxDb;
    effAnaMinDb   = viewMinDb; effAnaMaxDb   = viewMaxDb;
}

float FreqResponseDisplay::freqForSelectedBand() const
{
    if (selectedBandIdx == 0) return static_cast<float>(currentCutoffHz);
    if (selectedBandIdx == 1) return static_cast<float>(currentHighCutFreq);
    if (selectedBandIdx >= 2 && selectedBandIdx <= 5)
        return static_cast<float>(bellParams[selectedBandIdx - 2].freq);
    return -1.0f;
}

void FreqResponseDisplay::focusOnSelectedBand()
{
    float f = freqForSelectedBand();
    if (f <= 0.0f) return;

    // 選択点を中心に、ある程度拡大した周波数レンジにする (比率 ~16 ≒ ±2オクターブ)。
    // この後 Ctrl+ホイール / H± でさらに拡大縮小できる。
    const float ratio = 16.0f;
    float half = std::sqrt(ratio);
    currentMinF = std::clamp(f / half, 1.0f, 24000.0f);
    currentMaxF = std::clamp(f * half, currentMinF * 1.01f, 25000.0f);

    pathNeedsRecalculation = true;
    repaint();
}

void FreqResponseDisplay::focusSelectedBandIfAuto()
{
    if (autoFitV)
        focusOnSelectedBand();
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

    // --- アナライザーデータの事前計算 + 縦窓の決定 ---
    // (グリッド/アナライザー/EQカーブ すべてが有効窓 eff* を使うため、描画前に確定させる)
    haveAnaData = false;
    if (analyzer != nullptr)
    {
        std::vector<float> energies = analyzer->getEnergies();
        std::vector<float> holdEnergies = analyzer->getHoldEnergies();

        anaDbPerX.resize(static_cast<size_t>(w));
        holdDbPerX.resize(static_cast<size_t>(w));
        for (int i = 0; i < w; ++i)
        {
            float f = xToLogF(static_cast<float>(i));
            anaDbPerX[static_cast<size_t>(i)]  = interpAnalyzerDb(f, energies);
            holdDbPerX[static_cast<size_t>(i)] = interpAnalyzerDb(f, holdEnergies);
        }

        // relativeMode: 可視スペクトルの平滑トレンドを引いて残差を強調する。
        // アナライザーとHoldは同じトレンドで引き、相対関係を保つ。
        if (relativeMode)
        {
            std::vector<float> trend(static_cast<size_t>(w));
            int R = std::max(4, w / 8); // 移動平均の片側幅
            double acc = 0.0;
            // prefix-sum で O(w) 移動平均
            std::vector<double> prefix(static_cast<size_t>(w) + 1, 0.0);
            for (int i = 0; i < w; ++i) prefix[static_cast<size_t>(i) + 1] = prefix[static_cast<size_t>(i)] + anaDbPerX[static_cast<size_t>(i)];
            for (int i = 0; i < w; ++i)
            {
                int lo = std::max(0, i - R);
                int hi = std::min(w, i + R + 1);
                trend[static_cast<size_t>(i)] = static_cast<float>((prefix[static_cast<size_t>(hi)] - prefix[static_cast<size_t>(lo)]) / (hi - lo));
            }
            (void)acc;
            for (int i = 0; i < w; ++i)
            {
                anaDbPerX[static_cast<size_t>(i)]  -= trend[static_cast<size_t>(i)];
                holdDbPerX[static_cast<size_t>(i)] -= trend[static_cast<size_t>(i)];
            }
        }

        haveAnaData = true;
        updateVerticalWindows(anaDbPerX);
    }
    else
    {
        updateVerticalWindows({});
    }

    // 統一窓モード(Auto V/Flat)では窓が毎フレーム変化しうるため、EQカーブパスを再計算する
    if (autoFitV || relativeMode)
        pathNeedsRecalculation = true;

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

    // 2. ゲイングリッド描画 (dB)。有効窓 (eff*) に合わせて目盛りを描く。
    //    左=EQカーブ縦軸、右=同じy位置でのアナライザー縦軸の値。
    //    Auto-fit時は両者が一致し、相対表示時は右側が「相対dB」を示す。
    g.setColour(juce::Colour(0xff222230));
    float step = (effCurveMaxDb - effCurveMinDb) / 4.0f;
    for (int i = 0; i <= 4; ++i)
    {
        float db = effCurveMinDb + static_cast<float>(i) * step;
        float y = gainToY(db);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(w));

        g.setColour(juce::Colour(0xff555570));
        g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::plain)));
        g.drawText(juce::String(db, 1) + " dB", 5, static_cast<int>(y) - 12, 50, 12, juce::Justification::left);

        // 右側: 同じy位置に対応するアナライザー縦軸のdB値
        float frac = (h > 0) ? (y / static_cast<float>(h)) : 0.0f;
        float db_ana = effAnaMinDb + (1.0f - frac) * (effAnaMaxDb - effAnaMinDb);
        juce::String anaLabel = relativeMode ? (juce::String(db_ana, 1) + " dB(rel)")
                                             : (juce::String(db_ana, 1) + " dB");
        g.drawText(anaLabel, static_cast<int>(w) - 62, static_cast<int>(y) - 12, 57, 12, juce::Justification::right);

        g.setColour(juce::Colour(0xff222230));
    }

    // 3. アナライザーリアルタイムスペクトラムの描画 (dB配列は冒頭で事前計算済み)
    if (haveAnaData)
    {

        juce::Path analyzerPath;
        juce::Path strokePath;
        juce::Path holdPath;
        bool started = false;
        
        for (int i = 0; i < w; ++i)
        {
            float magDb = anaDbPerX[static_cast<size_t>(i)];
            float holdDb = holdDbPerX[static_cast<size_t>(i)];
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
            bool sel = (selectedBandIdx == 0);
            g.setColour(sel ? juce::Colours::white.withAlpha(0.7f) : pal.lowcut.withAlpha(offAlpha));
            g.drawEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.5f);
            g.setColour(pal.text.withAlpha(sel ? 0.9f : offAlpha));
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
            bool sel = (selectedBandIdx == 1);
            g.setColour(sel ? juce::Colours::white.withAlpha(0.7f) : pal.highcut.withAlpha(offAlpha));
            g.drawEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.5f);
            g.setColour(pal.text.withAlpha(sel ? 0.9f : offAlpha));
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
            bool sel = (selectedBandIdx == (i + 2));
            g.setColour(sel ? juce::Colours::white.withAlpha(0.7f) : bellColors[i].withAlpha(offAlpha));
            g.drawEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.5f);
            g.setColour(pal.text.withAlpha(sel ? 0.9f : offAlpha));
        }

        g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::bold)));
        g.drawText(bellNames[i], static_cast<int>(x) - 15, static_cast<int>(y) - 22, 30, 10, juce::Justification::centred);
    }
}

int FreqResponseDisplay::findNearestBand(float mx, float my, float radius, bool includeDisabled,
                                         int preferredBand, float preferredExtra) const
{
    int best = -1;
    float bestDist = 1.0e9f;

    auto consider = [&](int band, float x, float y)
    {
        float d = std::hypot(mx - x, my - y);
        // 選択中バンドのみ判定半径を少し広げる (少し外しても掴めるように)。
        // 比較は必ず実距離で行うため、他の点の真上をクリックしたときは
        // その点が優先される (選択中の点が近くの点を横取りしない)。
        float limit = (band == preferredBand) ? radius + preferredExtra : radius;
        if (d < limit && d < bestDist)
        {
            bestDist = d;
            best = band;
        }
    };

    // 0: LowCut
    if (includeDisabled || currentLowcutEnable)
        consider(0, logFToX(static_cast<float>(currentCutoffHz)), cutPointY(currentCutoffHz));

    // 1: HighCut
    if (includeDisabled || currentHighCutEnable)
        consider(1, logFToX(static_cast<float>(currentHighCutFreq)), cutPointY(currentHighCutFreq));

    // 2..5: Bells
    for (int i = 0; i < 4; ++i)
    {
        if (includeDisabled || bellParams[i].active)
            consider(i + 2,
                     logFToX(static_cast<float>(bellParams[i].freq)),
                     gainToY(static_cast<float>(bellParams[i].gain)));
    }

    return best;
}

void FreqResponseDisplay::mouseDown(const juce::MouseEvent& e)
{
    activeDragBand = -1;
    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);

    // 有効なポイントのうちカーソルに最も近いものを掴む (半径15px以内)。
    // 複数ポイントが近接していても意図した点を選べるよう、現在選択中のバンドは
    // 判定半径を広げて優先する (選択したのに掴めない問題の対策)。
    const float grabRadius = 15.0f;
    int band = findNearestBand(mx, my, grabRadius, /*includeDisabled=*/false,
                               /*preferredBand=*/selectedBandIdx, /*preferredExtra=*/10.0f);

    if (band != -1)
    {
        activeDragBand = band;
        if (editor != nullptr)
            editor->selectBand(static_cast<HighPrecisionEQAudioProcessorEditor::SelectedBand>(band));

        if (e.mods.isShiftDown() && processor != nullptr)
        {
            float soloFreq = (band == 0) ? static_cast<float>(currentCutoffHz)
                           : (band == 1) ? static_cast<float>(currentHighCutFreq)
                                         : static_cast<float>(bellParams[band - 2].freq);
            float soloQ = (band >= 2) ? std::max(2.0f, static_cast<float>(bellParams[band - 2].q)) : 2.0f;
            processor->setSoloMode(true, soloFreq, soloQ);
        }
        repaint();
        return;
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

    // ドラッグ中もHz/音名ツールチップをリアルタイム追従させる
    mouseX = static_cast<int>(mx);
    mouseY = static_cast<int>(my);
    isHovering = true;

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

        // ドラッグ中のポイント周波数をツールチップに反映
        hoverText = makeFreqNoteText(targetFreq);

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

        repaint();
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

        // 移動後のカーソル位置の周波数をツールチップに反映
        hoverText = makeFreqNoteText(xToLogF(mx));

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

    // ダブルクリックはOn/Offトグル用。Off中の点も淡色表示しているので、
    // Enable/active に関係なく、カーソルに最も近い点を選ぶ (Onにも戻せる)。
    int targetBand = findNearestBand(mx, my, grabRadius, /*includeDisabled=*/true);

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

        // トグルした点をそのまま選択状態にし、表示 (bellParams.active 等) も即時同期する。
        // (setValueNotifyingHost はパラメータを同期的に確定するので、直後の updateGraph で
        //  正しい有効/無効状態が反映され、有効化した点をすぐ掴める)
        if (editor != nullptr)
        {
            editor->selectBand(static_cast<HighPrecisionEQAudioProcessorEditor::SelectedBand>(targetBand));
            editor->updateGraph();
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
    // 1. Ctrlキー押下時の周波数軸ズーム (拡大・縮小)
    if (e.mods.isCtrlDown())
    {
        float mx = static_cast<float>(e.x);
        float targetF = xToLogF(mx);

        float zoomFactor = std::pow(1.15f, -wheel.deltaY);

        float centerLog = std::log10(targetF);
        float minLog = std::log10(currentMinF);
        float maxLog = std::log10(currentMaxF);

        float newMinLog = centerLog - (centerLog - minLog) * zoomFactor;
        float newMaxLog = centerLog + (maxLog - centerLog) * zoomFactor;

        float newMinF = std::pow(10.0f, newMinLog);
        float newMaxF = std::pow(10.0f, newMaxLog);

        float ratio = newMaxF / newMinF;

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

    // カーソルに最も近い有効なEQポイントを対象にする (半径15px以内)
    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);
    const float grabRadius = 15.0f;
    int targetBand = findNearestBand(mx, my, grabRadius, /*includeDisabled=*/false);

    // どのEQポイントの上でもない場合は、EQカーブを左右に移動する (上下操作は廃止)
    if (targetBand == -1)
    {
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

void FreqResponseDisplay::modifierKeysChanged(const juce::ModifierKeys& mods)
{
    if (processor == nullptr) return;

    // ポイントを掴んでいる最中 (クリック/ドラッグ中) にShiftの押下状態が
    // 変わったら、Solo-Sweepを追従させる。「先にクリックしてからShiftを押す」でも発動する。
    if (activeDragBand != -1)
    {
        if (mods.isShiftDown())
        {
            float soloFreq = (activeDragBand == 0) ? static_cast<float>(currentCutoffHz)
                           : (activeDragBand == 1) ? static_cast<float>(currentHighCutFreq)
                                                   : static_cast<float>(bellParams[activeDragBand - 2].freq);
            float soloQ = (activeDragBand >= 2)
                        ? std::max(2.0f, static_cast<float>(bellParams[activeDragBand - 2].q))
                        : 2.0f;
            processor->setSoloMode(true, soloFreq, soloQ);
        }
        else
        {
            processor->setSoloMode(false, 0.0f, 0.0f);
        }
    }
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
        hoverText = makeFreqNoteText(f);
    }
    else
    {
        hoverText.clear();
        mouseX = -1;
        mouseY = -1;
    }

    repaint();
}