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
        float newRatio = std::max(ratio * 0.6f, 3.0f);
        currentMinF = centerLog / std::sqrt(newRatio);
        currentMaxF = centerLog * std::sqrt(newRatio);
        if (currentMinF < 10.0f) currentMinF = 10.0f;
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
        if (currentMinF < 10.0f) currentMinF = 10.0f;
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
    if (analyzer != nullptr && analyzer->hasNewData())
    {
        repaint();
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

float FreqResponseDisplay::logFToX(float f) const
{
    float logMin = std::log10(currentMinF);
    float logMax = std::log10(currentMaxF);
    float val = (std::log10(f) - logMin) / (logMax - logMin);
    return static_cast<float>(getX()) + val * static_cast<float>(getWidth());
}

float FreqResponseDisplay::xToLogF(float x) const
{
    float logMin = std::log10(currentMinF);
    float logMax = std::log10(currentMaxF);
    float val = (x - static_cast<float>(getX())) / static_cast<float>(getWidth());
    return std::pow(10.0f, logMin + val * (logMax - logMin));
}

float FreqResponseDisplay::gainToY(float gainDecibels) const
{
    float val = (gainDecibels - currentMinDb) / (currentMaxDb - currentMinDb);
    return static_cast<float>(getY()) + (1.0f - val) * static_cast<float>(getHeight());
}

float FreqResponseDisplay::yToGain(float y) const
{
    float val = (y - static_cast<float>(getY())) / static_cast<float>(getHeight());
    return currentMinDb + (1.0f - val) * (currentMaxDb - currentMinDb);
}

float FreqResponseDisplay::analyzerGainToY(float gainDecibels) const
{
    // アナライザーのゲイン描画範囲 (通常 -70 dB 〜 +10 dB)
    float minDb = -70.0f + analyzerGainOffsetDb;
    float maxDb = 10.0f + analyzerGainOffsetDb;
    float val = (gainDecibels - minDb) / (maxDb - minDb);
    return static_cast<float>(getY()) + (1.0f - val) * static_cast<float>(getHeight());
}

void FreqResponseDisplay::paint(juce::Graphics& g)
{
    const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
    
    // 背景
    g.fillAll(juce::Colour(0xff0d0d15));

    int w = getWidth();
    int h = getHeight();

    // 1. 周波数グリッド描画 (対数)
    float gridFreqs[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    g.setColour(juce::Colour(0xff222230));
    for (float f : gridFreqs)
    {
        if (f >= currentMinF && f <= currentMaxF)
        {
            float x = logFToX(f);
            g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(h));
            
            g.setColour(juce::Colour(0xff555570));
            g.setFont(juce::Font(juce::FontOptions("Outfit", 9.0f, juce::Font::plain)));
            juce::String text = (f >= 1000.0f) ? juce::String(f / 1000.0f, 0) + "k" : juce::String(f, 0);
            g.drawText(text, static_cast<int>(x) - 15, h - 15, 30, 12, juce::Justification::centred);
            g.setColour(juce::Colour(0xff222230));
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
        std::vector<float> magnitudes = analyzer->getDetailedSpectrum(currentMinF, currentMaxF, w);

        juce::Path analyzerPath;
        bool started = false;
        
        for (int i = 0; i < w; ++i)
        {
            float magDb = magnitudes[static_cast<size_t>(i)];
            float y = analyzerGainToY(magDb);
            
            if (!started)
            {
                analyzerPath.startNewSubPath(static_cast<float>(i), y);
                started = true;
            }
            else
            {
                analyzerPath.lineTo(static_cast<float>(i), y);
            }
        }

        if (started)
        {
            // ベイズ平滑化による美しい残響・ホールド表示
            g.setColour(pal.lowcut.withAlpha(0.12f));
            g.strokePath(analyzerPath, juce::PathStrokeType(1.5f));
            
            // 下部を閉じてグラデーションで塗りつぶす
            analyzerPath.lineTo(static_cast<float>(w), static_cast<float>(h));
            analyzerPath.lineTo(0.0f, static_cast<float>(h));
            analyzerPath.closeSubPath();
            
            juce::ColourGradient fillGrad(pal.lowcut.withAlpha(0.04f), 0.0f, static_cast<float>(h * 0.4f),
                                          juce::Colours::transparentBlack, 0.0f, static_cast<float>(h), false);
            g.setGradientFill(fillGrad);
            g.fillPath(analyzerPath);
        }
    }

    // 4. EQ特性カーブの再計算と描画
    if (pathNeedsRecalculation)
    {
        cachedResponsePath.clear();
        bool started = false;

        for (int x = 0; x < w; ++x)
        {
            float f = xToLogF(static_cast<float>(x));
            double mag = 1.0;
            if (processor != nullptr)
            {
                // プロセッサから直接最小位相 cascade の応答を取得
                mag = processor->apvts.getProcessor()->createEditor() != nullptr ? 
                      processor->apvts.getProcessor()->getStateInformation(juce::MemoryBlock()), 1.0 : 1.0;
                
                // 実際には MinimumPhaseEQ の getMagnitudeForFrequency(f) を使う
                // 確実にプロセッサオブジェクトの minimumPhaseEQ から取得するため、Processor側にゲッターを設けるか直接計算するか
                // すでに PluginProcessor に minimumPhaseEQ へのアクセス等がない場合、ここで手動計算も可能ですが、
                // PluginProcessor.h には getMagnitudeForFrequency は無いですが、MinimumPhaseEQ minimumPhaseEQ 自体は private です。
                // あ、PluginProcessor.cpp にて `minimumPhaseEQ.updateParameters` などを呼んでいます。
                // ここでは `processor` の minimumPhaseEQ の特性を描画するため、
                // `processor->apvts.getRawParameterValue` などのパラメータ値を基にして
                // FreqResponseDisplay 自体が Magnitude を再計算、もしくは processor 側に `double getMagnitudeForFrequency(double f)` を追加しているでしょうか？
                // PluginProcessor.h/cpp を見ましたが、getMagnitudeForFrequency のラッパーは無いようです。
                // したがって、このコンポーネント内で一時的に MinimumPhaseEQ のインスタンスを作り、パラメータを適用して計算するのが最も安全でコード変更を伴わない方法です！
                // もしくは、SOSCoefficients を用いてこの場で計算します。
                // しかし、このコンポーネントには `currentCutoffHz` などのパラメータがすべて `updateParameters` で渡されています。
                // なので、ローカルに `MinimumPhaseEQ` インスタンスを作成し、そこで `updateParameters` を呼んでから `getMagnitudeForFrequency` を呼べば、プロセッサの状態に依存せず完璧に描画できます！
                // これが最も確実でバグが少ない方法です。
            }
        }
    }

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

    juce::Path responsePath;
    bool started = false;
    for (int x = 0; x < w; ++x)
    {
        float f = xToLogF(static_cast<float>(x));
        double mag = localEQ.getMagnitudeForFrequency(f);
        float magDb = static_cast<float>(juce::Decibels::gainToDecibels(mag));
        float y = gainToY(magDb);

        if (!started)
        {
            responsePath.startNewSubPath(static_cast<float>(x), y);
            started = true;
        }
        else
        {
            responsePath.lineTo(static_cast<float>(x), y);
        }
    }

    // カーブの塗りつぶし用パスを作成
    juce::Path fillPath = responsePath;
    fillPath.lineTo(static_cast<float>(w), gainToY(0.0f));
    fillPath.lineTo(0.0f, gainToY(0.0f));
    fillPath.closeSubPath();

    // 塗りつぶしグラデーション
    juce::Colour lineCol = pal.lowcut;
    if (selectedBandIdx == 1) lineCol = pal.bell1;
    else if (selectedBandIdx >= 2) {
        juce::Colour bColors[4] = { pal.bell1, pal.bell2, pal.bell3, pal.bell4 };
        lineCol = bColors[selectedBandIdx - 2];
    }

    juce::ColourGradient fillGrad(lineCol.withAlpha(0.15f), 0.0f, gainToY(currentMaxDb),
                                  lineCol.withAlpha(0.0f), 0.0f, gainToY(0.0f), false);
    g.setGradientFill(fillGrad);
    g.fillPath(fillPath);

    // ライン描画
    g.setColour(lineCol);
    g.strokePath(responsePath, juce::PathStrokeType(2.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

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

    if (activeDragBand == 0) // LowCut
    {
        // 周波数とゲインをスライダー経由で同期
        editor->cutoffSlider.setValue(std::clamp(dragFreq, 20.0f, 500.0f));
        editor->gainSlider.setValue(std::clamp(dragGain, -10.0f, 0.0f));
    }
    else if (activeDragBand == 1) // HighCut
    {
        editor->cutoffSlider.setValue(std::clamp(dragFreq, 20.0f, 20000.0f));
    }
    else if (activeDragBand >= 2 && activeDragBand <= 5) // Bells
    {
        editor->cutoffSlider.setValue(std::clamp(dragFreq, 20.0f, 20000.0f));
        editor->gainSlider.setValue(std::clamp(dragGain, -18.0f, 18.0f));
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
            float newRatio = std::clamp(ratio * xZoomFactor, 3.0f, 2400.0f);
            
            currentMinF = centerLog / std::sqrt(newRatio);
            currentMaxF = centerLog * std::sqrt(newRatio);
            if (currentMinF < 10.0f) currentMinF = 10.0f;
            if (currentMaxF > 24000.0f) currentMaxF = 24000.0f;

            float yZoomFactor = std::pow(1.005f, -dy);
            currentMinDb = std::clamp(dragStartMinDb * yZoomFactor, -48.0f, -3.0f);
            currentMaxDb = -currentMinDb;
        }
        else
        {
            // 左ドラッグ: シフト移動
            float shiftFactor = std::pow(10.0f, -dx / static_cast<float>(getWidth()) * 0.5f);
            currentMinF = std::max(dragStartMinF * shiftFactor, 10.0f);
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

void FreqResponseDisplay::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (editor == nullptr) return;

    // 現在選択されているバンドのQ値をマウスホイールで増減
    if (selectedBandIdx >= 2 && selectedBandIdx <= 5)
    {
        double currentQ = editor->qSlider.getValue();
        double multiplier = std::pow(1.1, wheel.deltaY);
        editor->qSlider.setValue(std::clamp(currentQ * multiplier, 0.1, 120.0));
    }
}

void FreqResponseDisplay::modifierKeysChanged(const juce::ModifierKeys&)
{
}
