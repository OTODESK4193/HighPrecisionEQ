#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GUI/ColorPalette.h"

HighPrecisionEQAudioProcessorEditor::HighPrecisionEQAudioProcessorEditor(HighPrecisionEQAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p),
      freqDisplay(), waveformDisplay(p.getSnapshotQueue()), phaseDisplay()
{
    // Intel GPU向けにOpenGL Contextをアタッチ
    openGLContext.attachTo(*this);

    // リサイズを有効化（アスペクト比固定は解除し、自由にサイズ変更できるようにする）
    setResizable(true, true);
    setResizeLimits(720, 400, 1800, 1000);

    // スライダーのLookAndFeelをセット
    cutoffSlider.setLookAndFeel(&arcLAF);
    cutoffSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    cutoffSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 16);
    
    gainSlider.setLookAndFeel(&arcLAF);
    gainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 16);
    
    slopeSlider.setLookAndFeel(&arcLAF);
    slopeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slopeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 16);
    
    qSlider.setLookAndFeel(&arcLAF);
    qSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    qSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 16);

    // ラベルの設定
    cutoffLabel.setText("FREQUENCY", juce::dontSendNotification);
    cutoffLabel.setFont(juce::Font(juce::FontOptions("Outfit", 10.0f, juce::Font::bold)));
    cutoffLabel.setJustificationType(juce::Justification::centred);
    
    gainLabel.setText("GAIN", juce::dontSendNotification);
    gainLabel.setFont(juce::Font(juce::FontOptions("Outfit", 10.0f, juce::Font::bold)));
    gainLabel.setJustificationType(juce::Justification::centred);
    
    slopeLabel.setText("SLOPE", juce::dontSendNotification);
    slopeLabel.setFont(juce::Font(juce::FontOptions("Outfit", 10.0f, juce::Font::bold)));
    slopeLabel.setJustificationType(juce::Justification::centred);
    
    qLabel.setText("Q", juce::dontSendNotification);
    qLabel.setFont(juce::Font(juce::FontOptions("Outfit", 10.0f, juce::Font::bold)));
    qLabel.setJustificationType(juce::Justification::centred);

    // バンド選択ボタンとONボタンの初期化
    juce::String bandNames[6] = { "LC", "HC", "B1", "B2", "B3", "B4" };
    for (int i = 0; i < 6; ++i)
    {
        bandButtons[i].setButtonText(bandNames[i]);
        bandButtons[i].setRadioGroupId(1001);
        bandButtons[i].setClickingTogglesState(true);
        bandButtons[i].onClick = [this, i]() { selectBand(static_cast<SelectedBand>(i)); };
        addAndMakeVisible(bandButtons[i]);
        
        enableButtons[i].setClickingTogglesState(true);
        enableButtons[i].onStateChange = [this]() { updateComponentColors(); };
        addAndMakeVisible(enableButtons[i]);
    }

    // コールバック接続 (メンバ関数を呼ぶ)
    cutoffSlider.onValueChange = [this]() { updateGraph(); };
    gainSlider.onValueChange = [this]() { updateGraph(); };
    qSlider.onValueChange = [this]() { updateGraph(); };
    slopeSlider.onValueChange = [this]() { updateGraph(); };

    for (int i = 0; i < 6; ++i)
        enableButtons[i].onClick = [this]() { updateGraph(); };

    // アタッチメント作成 (ボタン関係)
    enableAttachments[0] = std::make_unique<ButtonAttachment>(processorRef.apvts, "lowcut_enable", enableButtons[0]);
    enableAttachments[1] = std::make_unique<ButtonAttachment>(processorRef.apvts, "highcut_enable", enableButtons[1]);
    for (int i = 0; i < 4; ++i)
    {
        enableAttachments[i + 2] = std::make_unique<ButtonAttachment>(processorRef.apvts, "bell_enable_" + juce::String(i + 1), enableButtons[i + 2]);
    }
    diffAttachment = std::make_unique<ButtonAttachment>(processorRef.apvts, "listenDiff", diffButton);
    bypassAttachment = std::make_unique<ButtonAttachment>(processorRef.apvts, "bypass", bypassButton);

    // Analyze ボタン
    analyzeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a4e));
    analyzeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    analyzeButton.onClick = [this]() {
        if (analyzeMode == AnalyzeMode::Normal) analyzeMode = AnalyzeMode::Waveform;
        else if (analyzeMode == AnalyzeMode::Waveform) analyzeMode = AnalyzeMode::Phase;
        else analyzeMode = AnalyzeMode::Normal;
        
        freqDisplay.setVisible(analyzeMode == AnalyzeMode::Normal);
        waveformDisplay.setVisible(analyzeMode == AnalyzeMode::Waveform);
        phaseDisplay.setVisible(analyzeMode == AnalyzeMode::Phase);
        resized();
    };

    // Diff / Bypass / Color ボタン
    diffButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a4e));
    diffButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    diffButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xaa00ccff));
    diffButton.setClickingTogglesState(true);

    bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a4e));
    bypassButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    bypassButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffff4444));
    bypassButton.setClickingTogglesState(true);

    colorButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a4e));
    colorButton.onClick = [this]() {
        currentPaletteIdx = (currentPaletteIdx + 1) % 10;
        processorRef.currentPaletteIdx = currentPaletteIdx;
        
        freqDisplay.setColorPaletteIndex(currentPaletteIdx);
        phaseDisplay.setColorPaletteIndex(currentPaletteIdx);
        waveformDisplay.setColorPaletteIndex(currentPaletteIdx);
        
        updateComponentColors();
        repaint();
    };

    // ディスプレイ・アナライザとの関連付け
    freqDisplay.setAnalyzer(&processorRef.getAnalyzer());
    freqDisplay.setProcessorAndEditor(&processorRef, this);
    phaseDisplay.setAnalyzer(&processorRef.getAnalyzer());
    phaseDisplay.setResponseDisplay(&freqDisplay);

    currentPaletteIdx = processorRef.currentPaletteIdx;
    freqDisplay.setColorPaletteIndex(currentPaletteIdx);
    phaseDisplay.setColorPaletteIndex(currentPaletteIdx);
    waveformDisplay.setColorPaletteIndex(currentPaletteIdx);

    // 子コンポーネントを追加
    addChildComponent(freqDisplay);
    addChildComponent(waveformDisplay);
    addChildComponent(phaseDisplay);
    freqDisplay.setVisible(true);

    addAndMakeVisible(analyzeButton);
    addAndMakeVisible(diffButton);
    addAndMakeVisible(bypassButton);
    addAndMakeVisible(colorButton);
    addAndMakeVisible(cutoffLabel);
    addAndMakeVisible(cutoffSlider);
    addAndMakeVisible(gainLabel);
    addAndMakeVisible(gainSlider);
    addAndMakeVisible(slopeLabel);
    addAndMakeVisible(slopeSlider);
    addAndMakeVisible(qLabel);
    addAndMakeVisible(qSlider);

    setSize(900, 500);

    // 初期バンド選択
    selectBand(SelectedBand::LowCut);
    updateGraph();
    updateComponentColors();
}

HighPrecisionEQAudioProcessorEditor::~HighPrecisionEQAudioProcessorEditor()
{
    openGLContext.detach();
    cutoffSlider.setLookAndFeel(nullptr);
    gainSlider.setLookAndFeel(nullptr);
    qSlider.setLookAndFeel(nullptr);
    slopeSlider.setLookAndFeel(nullptr);
}

void HighPrecisionEQAudioProcessorEditor::paint(juce::Graphics& g)
{
    // 暗い背景
    g.fillAll(juce::Colour(0xff12121e));

    // ヘッダーテキスト (Modern Typography)
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font(juce::FontOptions("Outfit", 18.0f, juce::Font::bold)));
    g.drawText("LOWCUT POLICE", 15, 10, 200, 25, juce::Justification::left);
    
    g.setColour(juce::Colour(0xffff8800));
    g.setFont(juce::Font(juce::FontOptions("Outfit", 11.0f, juce::Font::plain)));
    g.drawText("PRECISION IIR EQ", 15, 28, 200, 15, juce::Justification::left);

    // バージョン情報
    g.setColour(juce::Colour(0xff50506f));
    g.setFont(10.0f);
    g.drawText("v1.0.0", getWidth() - 65, 15, 50, 15, juce::Justification::right);
}

void HighPrecisionEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);

    // ヘッダー空間
    auto headerArea = area.removeFromTop(40);
    analyzeButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));
    headerArea.removeFromRight(10);
    bypassButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));
    headerArea.removeFromRight(10);
    colorButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));
    headerArea.removeFromRight(10);
    diffButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));

    // コントロール行
    auto controlRow = area.removeFromBottom(120);
    area.removeFromBottom(20);

    // ディスプレイ割り当て
    auto displayArea = area;
    
    if (analyzeMode == AnalyzeMode::Waveform)
        waveformDisplay.setBounds(displayArea);
    else if (analyzeMode == AnalyzeMode::Phase)
        phaseDisplay.setBounds(displayArea);
    else
        freqDisplay.setBounds(displayArea);
    
    int totalWidth = controlRow.getWidth();
    int knobW = 150;
    int gap = 40;
    int bandsW = 300;
    
    if (totalWidth < 750)
    {
        knobW = 105;
        gap = 15;
        bandsW = 240;
    }
    else if (totalWidth < 850)
    {
        knobW = 125;
        gap = 25;
        bandsW = 270;
    }
    
    // スライダーの表示切り替えと配置
    auto cutoffArea = controlRow.removeFromLeft(knobW);
    cutoffLabel.setBounds(cutoffArea.removeFromTop(20));
    cutoffSlider.setBounds(cutoffArea);
    
    controlRow.removeFromLeft(gap);
    
    auto gainArea = controlRow.removeFromLeft(knobW);
    gainLabel.setBounds(gainArea.removeFromTop(20));
    gainSlider.setBounds(gainArea);
    
    controlRow.removeFromLeft(gap);
    
    auto thirdKnobArea = controlRow.removeFromLeft(knobW);
    if (currentBand == SelectedBand::LowCut || currentBand == SelectedBand::HighCut)
    {
        slopeLabel.setBounds(thirdKnobArea.removeFromTop(20));
        slopeSlider.setBounds(thirdKnobArea);
    }
    else
    {
        qLabel.setBounds(thirdKnobArea.removeFromTop(20));
        qSlider.setBounds(thirdKnobArea);
    }

    // バンド選択ボタン
    auto bandsArea = controlRow.removeFromRight(bandsW).withSizeKeepingCentre(bandsW, 44);
    float btnW = (bandsW - 25) / 6.0f;
    float btnGap = 5.0f;
    
    for (int i = 0; i < 6; ++i)
    {
        auto btnGroup = bandsArea.removeFromLeft(static_cast<int>(btnW));
        bandsArea.removeFromLeft(static_cast<int>(btnGap));
        bandButtons[i].setBounds(btnGroup.removeFromTop(26));
        btnGroup.removeFromTop(2);
        enableButtons[i].setBounds(btnGroup.removeFromTop(16));
    }
}

void HighPrecisionEQAudioProcessorEditor::selectBand(SelectedBand band)
{
    currentBand = band;
    int bandIdx = static_cast<int>(band);
    bandButtons[bandIdx].setToggleState(true, juce::dontSendNotification);
    
    bool isCut = (band == SelectedBand::LowCut || band == SelectedBand::HighCut);
    slopeSlider.setVisible(isCut);
    slopeLabel.setVisible(isCut);
    qSlider.setVisible(!isCut);
    qLabel.setVisible(!isCut);
    
    // HighCut のときは Gain は意味を持たないので隠すか無効にする
    bool isHighCut = (band == SelectedBand::HighCut);
    gainSlider.setVisible(!isHighCut);
    gainLabel.setVisible(!isHighCut);
    
    updateComponentColors();
    
    freqDisplay.setSelectedBand(bandIdx);
    updateAttachments();
    resized();
}

void HighPrecisionEQAudioProcessorEditor::updateAttachments()
{
    // アタッチメント変更時の不要な/不正な updateGraph() 呼び出しを防ぐ
    cutoffSlider.onValueChange = nullptr;
    gainSlider.onValueChange = nullptr;
    slopeSlider.onValueChange = nullptr;
    qSlider.onValueChange = nullptr;

    cutoffAttachment.reset();
    gainAttachment.reset();
    slopeAttachment.reset();
    qAttachment.reset();
    
    if (currentBand == SelectedBand::LowCut)
    {
        cutoffAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "cutoffHz", cutoffSlider);
        gainAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "gainDb", gainSlider);
        slopeAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "slopeDbOct", slopeSlider);
    }
    else if (currentBand == SelectedBand::HighCut)
    {
        cutoffAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "highcut_freq", cutoffSlider);
        slopeAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "highcut_slope", slopeSlider);
    }
    else
    {
        int idx = static_cast<int>(currentBand) - 1; // Bell1..4 maps to idx 1..4 in SelectedBand
        juce::String idSuffix = juce::String(idx);
        cutoffAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "bell_freq_" + idSuffix, cutoffSlider);
        gainAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "bell_gain_" + idSuffix, gainSlider);
        qAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "bell_q_" + idSuffix, qSlider);
    }

    // コールバックを再接続
    cutoffSlider.onValueChange = [this]() { updateGraph(); };
    gainSlider.onValueChange = [this]() { updateGraph(); };
    slopeSlider.onValueChange = [this]() { updateGraph(); };
    qSlider.onValueChange = [this]() { updateGraph(); };
}

void HighPrecisionEQAudioProcessorEditor::updateComponentColors()
{
    const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
    
    juce::Colour activeColor = pal.lowcut;
    switch (currentBand) {
        case SelectedBand::HighCut: activeColor = pal.bell1; break; // HighCut用にシアンや青系を流用
        case SelectedBand::Bell1: activeColor = pal.bell1; break;
        case SelectedBand::Bell2: activeColor = pal.bell2; break;
        case SelectedBand::Bell3: activeColor = pal.bell3; break;
        case SelectedBand::Bell4: activeColor = pal.bell4; break;
        default: break;
    }
    
    cutoffSlider.setColour(juce::Slider::rotarySliderFillColourId, activeColor);
    gainSlider.setColour(juce::Slider::rotarySliderFillColourId, activeColor);
    qSlider.setColour(juce::Slider::rotarySliderFillColourId, activeColor);
    slopeSlider.setColour(juce::Slider::rotarySliderFillColourId, activeColor);
    
    // バンドボタン色の割り当て
    juce::Colour bandColors[6] = { pal.lowcut, pal.bell1, pal.bell1, pal.bell2, pal.bell3, pal.bell4 };
    for (int i = 0; i < 6; ++i)
    {
        bandButtons[i].setColour(juce::TextButton::buttonOnColourId, bandColors[i].withAlpha(0.6f));
        bandButtons[i].setColour(juce::TextButton::textColourOffId, pal.text);
        
        bool isON = enableButtons[i].getToggleState();
        enableButtons[i].setButtonText(isON ? "ON" : "OFF");
        
        if (isON)
        {
            enableButtons[i].setColour(juce::TextButton::buttonColourId, bandColors[i].withAlpha(0.8f));
            enableButtons[i].setColour(juce::TextButton::buttonOnColourId, bandColors[i]);
            enableButtons[i].setColour(juce::TextButton::textColourOnId, juce::Colours::black);
            enableButtons[i].setColour(juce::TextButton::textColourOffId, juce::Colours::black);
        }
        else
        {
            enableButtons[i].setColour(juce::TextButton::buttonColourId, juce::Colour(0xff20202a));
            enableButtons[i].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff20202a));
            enableButtons[i].setColour(juce::TextButton::textColourOnId, pal.text.withAlpha(0.3f));
            enableButtons[i].setColour(juce::TextButton::textColourOffId, pal.text.withAlpha(0.3f));
        }
    }
    
    analyzeButton.setColour(juce::TextButton::textColourOffId, pal.text);
    diffButton.setColour(juce::TextButton::textColourOffId, pal.text);
    bypassButton.setColour(juce::TextButton::textColourOffId, pal.text);
    colorButton.setColour(juce::TextButton::textColourOffId, pal.text);
    
    diffButton.setColour(juce::TextButton::buttonOnColourId, pal.bell1.withAlpha(0.6f));
}

void HighPrecisionEQAudioProcessorEditor::updateGraph()
{
    double lcCutoff = processorRef.apvts.getRawParameterValue("cutoffHz")->load();
    int lcSlope = static_cast<int>(processorRef.apvts.getRawParameterValue("slopeDbOct")->load());
    int lcOrder = std::clamp(lcSlope / 12, 1, 8);
    double lcGain = processorRef.apvts.getRawParameterValue("gainDb")->load();
    bool lcEnable = processorRef.apvts.getRawParameterValue("lowcut_enable")->load() > 0.5f;

    double hcFreq = processorRef.apvts.getRawParameterValue("highcut_freq")->load();
    int hcSlope = static_cast<int>(processorRef.apvts.getRawParameterValue("highcut_slope")->load());
    int hcOrder = std::clamp(hcSlope / 12, 1, 8);
    bool hcEnable = processorRef.apvts.getRawParameterValue("highcut_enable")->load() > 0.5f;

    std::array<FreqResponseDisplay::BellParam, 4> bells;
    for (int i = 0; i < 4; ++i)
    {
        juce::String suffix = juce::String(i + 1);
        bells[i].freq = processorRef.apvts.getRawParameterValue("bell_freq_" + suffix)->load();
        bells[i].gain = processorRef.apvts.getRawParameterValue("bell_gain_" + suffix)->load();
        bells[i].q = processorRef.apvts.getRawParameterValue("bell_q_" + suffix)->load();
        bells[i].active = processorRef.apvts.getRawParameterValue("bell_enable_" + suffix)->load() > 0.5f;
    }

    freqDisplay.updateParameters(lcCutoff, lcOrder, lcGain, lcEnable,
                                 hcFreq, hcOrder, hcEnable,
                                 processorRef.getSampleRate(), bells);
                                 
    phaseDisplay.updateParameters(lcCutoff, lcOrder, lcGain, lcEnable,
                                  hcFreq, hcOrder, hcEnable,
                                  processorRef.getSampleRate(), bells);

    updateComponentColors();
}
