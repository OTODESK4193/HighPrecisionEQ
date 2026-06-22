void LowCutPoliceAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);

    // ヘッダー空間を確保 (Analyzeボタンを右上に配置)
    auto headerArea = area.removeFromTop(40);
    analyzeButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));
    headerArea.removeFromRight(10); // spacer
    bypassButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));
    headerArea.removeFromRight(10); // spacer
    colorButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));
    headerArea.removeFromRight(10); // spacer
    diffButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));

    // 下部コントロール行を先に下から確保
    auto controlRow = area.removeFromBottom(120);
    area.removeFromBottom(20); // マージン

    // 残りの領域すべてをディスプレイに割り当てる
    auto displayArea = area;
    
    if (analyzeMode == AnalyzeMode::Waveform)
    {
        waveformDisplay.setBounds(displayArea);
    }
    else if (analyzeMode == AnalyzeMode::Phase)
    {
        phaseDisplay.setBounds(displayArea);
    }
    else
    {
        freqDisplay.setBounds(displayArea);
    }
    
    // 画面幅に応じてサイズや余白を動的に調整 (重なりを防止)
    int totalWidth = controlRow.getWidth();
    int knobW = 150;
    int gap = 40;
    int bandsW = 250;
    
    if (totalWidth < 750)
    {
        knobW = 105;
        gap = 15;
        bandsW = 200;
    }
    else if (totalWidth < 850)
    {
        knobW = 125;
        gap = 25;
        bandsW = 230;
    }
    
    auto cutoffArea = controlRow.removeFromLeft(knobW);
    cutoffLabel.setBounds(cutoffArea.removeFromTop(20));
    cutoffSlider.setBounds(cutoffArea);
    
    controlRow.removeFromLeft(gap);
    
    auto gainArea = controlRow.removeFromLeft(knobW);
    gainLabel.setBounds(gainArea.removeFromTop(20));
    gainSlider.setBounds(gainArea);
    
    controlRow.removeFromLeft(gap);
    
    if (currentBand == SelectedBand::LowCut) {
        auto slopeArea = controlRow.removeFromLeft(knobW);
        slopeLabel.setBounds(slopeArea.removeFromTop(20));
        slopeSlider.setBounds(slopeArea);
    } else {
        auto qArea = controlRow.removeFromLeft(knobW);
        qLabel.setBounds(qArea.removeFromTop(20));
        qSlider.setBounds(qArea);
    }

    // 右側にバンド選択ボタンを並べる
    auto bandsArea = controlRow.removeFromRight(bandsW).withSizeKeepingCentre(bandsW, 44);
    
    float btnW = (bandsW - 20) / 5.0f;
    float btnGap = 5.0f;
    if (bandsW < 220) {
        btnW = (bandsW - 12) / 5.0f;
        btnGap = 3.0f;
    }
    
    for (int i = 0; i < 5; ++i) {
        auto btnGroup = bandsArea.removeFromLeft(static_cast<int>(btnW));
        bandsArea.removeFromLeft(static_cast<int>(btnGap));
        bandButtons[i].setBounds(btnGroup.removeFromTop(26));
        btnGroup.removeFromTop(2);
        enableButtons[i].setBounds(btnGroup.removeFromTop(16));
    }
}

void LowCutPoliceAudioProcessorEditor::updateComponentColors()
{
    const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
    
    // 選択中のバンドに合わせた色
    juce::Colour activeColor = pal.lowcut;
    switch (currentBand) {
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
    
    // バンド選択ボタンとONボタンの色をパレットに合わせる
    juce::Colour bandColors[5] = { pal.lowcut, pal.bell1, pal.bell2, pal.bell3, pal.bell4 };
    for (int i = 0; i < 5; ++i) {
        bandButtons[i].setColour(juce::TextButton::buttonOnColourId, bandColors[i].withAlpha(0.6f));
        bandButtons[i].setColour(juce::TextButton::textColourOffId, pal.text);
        enableButtons[i].setColour(juce::TextButton::textColourOffId, pal.text);
    }
    
    // 各種ヘッダーボタン
    analyzeButton.setColour(juce::TextButton::textColourOffId, pal.text);
    diffButton.setColour(juce::TextButton::textColourOffId, pal.text);
    bypassButton.setColour(juce::TextButton::textColourOffId, pal.text);
    colorButton.setColour(juce::TextButton::textColourOffId, pal.text);
    
    // DiffのON色をパレットのbell1(シアン)やlowcutなどに合わせると調和が良い
    diffButton.setColour(juce::TextButton::buttonOnColourId, pal.bell1.withAlpha(0.6f));
}

            bells,
            lcEnable
        );
        
        phaseDisplay.updateParameters(
            static_cast<double>(processorRef.apvts.getRawParameterValue("cutoffHz")->load()),
            order,
            processorRef.getSampleRate(),
            static_cast<double>(lcGain),
            bells,
            lcEnable
        );
    };

    cutoffSlider.onValueChange = updateGraph;
    gainSlider.onValueChange = updateGraph;
    qSlider.onValueChange = updateGraph;
    slopeSlider.onValueChange = updateGraph;
    for (int i = 0; i < 5; ++i) {
        enableButtons[i].onClick = updateGraph;
    }

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

    // Diff ボタン
    diffButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a4e));
    diffButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    diffButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xaa00ccff)); // 差分リスニング時は青く光る
    diffButton.setClickingTogglesState(true);

    // Bypass ボタン
    bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a4e));
    bypassButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe0e0e0));
    bypassButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffff4444)); // Bypass時は赤く光る
    bypassButton.setClickingTogglesState(true);

    // AnalyzerをFreqResponseDisplayおよびPhaseDisplayにセット、位相画面にレスポンス画面のポインタをセット
    freqDisplay.setAnalyzer(&processorRef.getAnalyzer());
    freqDisplay.setProcessorAndEditor(&processorRef, this);
    phaseDisplay.setAnalyzer(&processorRef.getAnalyzer());
    phaseDisplay.setResponseDisplay(&freqDisplay);

    // 同期と初期設定
    currentPaletteIdx = processorRef.currentPaletteIdx;
    freqDisplay.setColorPaletteIndex(currentPaletteIdx);
    phaseDisplay.setColorPaletteIndex(currentPaletteIdx);
    waveformDisplay.setColorPaletteIndex(currentPaletteIdx);

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

    // GUI部品を可視化
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

    // 初期バンド選択
    bandButtons[0].setToggleState(true, juce::NotificationType::sendNotification);

    // 初回描画の初期化
    updateGraph();
    updateComponentColors();
}

void LowCutPoliceAudioProcessorEditor::selectBand(SelectedBand band)
{
    currentBand = band;
    
    int bandIdx = static_cast<int>(band);
    bandButtons[bandIdx].setToggleState(true, juce::dontSendNotification);
    
    bool isLowCut = (band == SelectedBand::LowCut);
    slopeSlider.setVisible(isLowCut);
    slopeLabel.setVisible(isLowCut);
    qSlider.setVisible(!isLowCut);
    qLabel.setVisible(!isLowCut);
    
    updateComponentColors();
    
    freqDisplay.setSelectedBand(bandIdx);
    updateAttachments();
    resized();
}

void LowCutPoliceAudioProcessorEditor::updateAttachments()
{
    cutoffAttachment.reset();
    gainAttachment.reset();
    slopeAttachment.reset();
    qAttachment.reset();
    
    if (currentBand == SelectedBand::LowCut) {
        cutoffAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "cutoffHz", cutoffSlider);
        gainAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "gainDb", gainSlider);
        slopeAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "slopeDbOct", slopeSlider);
    } else {
        int idx = static_cast<int>(currentBand);
        juce::String idSuffix = juce::String(idx);
        cutoffAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "bell_freq_" + idSuffix, cutoffSlider);
        gainAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "bell_gain_" + idSuffix, gainSlider);
        qAttachment = std::make_unique<SliderAttachment>(processorRef.apvts, "bell_q_" + idSuffix, qSlider);
    }
}

LowCutPoliceAudioProcessorEditor::~LowCutPoliceAudioProcessorEditor()
{
    openGLContext.detach();
    cutoffSlider.setLookAndFeel(nullptr);
    gainSlider.setLookAndFeel(nullptr);
    qSlider.setLookAndFeel(nullptr);
}

void LowCutPoliceAudioProcessorEditor::paint(juce::Graphics& g)
{
    // 暗いネイビー背景
    g.fillAll(juce::Colour(0xff12121e));

    // ヘッダーテキスト (MATT ORANGE にインスパイアされた白+オレンジ)
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font(juce::FontOptions("Outfit", 18.0f, juce::Font::bold)));
    g.drawText("LOWCUT POLICE", 15, 10, 200, 25, juce::Justification::left);
    
    g.setColour(juce::Colour(0xffff8800));
    g.setFont(juce::Font(juce::FontOptions("Outfit", 11.0f, juce::Font::plain)));
    g.drawText("ZERO-PHASE IIR", 15, 28, 200, 15, juce::Justification::left);

    // バージョン情報
    g.setColour(juce::Colour(0xff50506f));
    g.setFont(10.0f);
    g.drawText("v1.0.0", getWidth() - 65, 15, 50, 15, juce::Justification::right);
}

void LowCutPoliceAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);

    // ヘッダー空間を確保 (Analyzeボタンを右上に配置)
    auto headerArea = area.removeFromTop(40);
    analyzeButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));
    headerArea.removeFromRight(10); // spacer
    bypassButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));
    headerArea.removeFromRight(10); // spacer
    colorButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));
    headerArea.removeFromRight(10); // spacer
    diffButton.setBounds(headerArea.removeFromRight(80).withSizeKeepingCentre(80, 24));

    // 下部コントロール行を先に下から確保
    auto controlRow = area.removeFromBottom(120);
    area.removeFromBottom(20); // マージン

    // 残りの領域すべてをディスプレイに割り当てる
    auto displayArea = area;
    
    if (analyzeMode == AnalyzeMode::Waveform)
    {
        waveformDisplay.setBounds(displayArea);
    }
    else if (analyzeMode == AnalyzeMode::Phase)
    {
        phaseDisplay.setBounds(displayArea);
    }
    else
    {
        freqDisplay.setBounds(displayArea);
    }
    
    // 画面幅に応じてサイズや余白を動的に調整 (重なりを防止)
    int totalWidth = controlRow.getWidth();
    int knobW = 150;
    int gap = 40;
    int bandsW = 250;
    
    if (totalWidth < 750)
    {
        knobW = 105;
        gap = 15;
        bandsW = 200;
    }
    else if (totalWidth < 850)
    {
        knobW = 125;
        gap = 25;
        bandsW = 230;
    }
    
    auto cutoffArea = controlRow.removeFromLeft(knobW);
    cutoffLabel.setBounds(cutoffArea.removeFromTop(20));
    cutoffSlider.setBounds(cutoffArea);
    
    controlRow.removeFromLeft(gap);
    
    auto gainArea = controlRow.removeFromLeft(knobW);
    gainLabel.setBounds(gainArea.removeFromTop(20));
    gainSlider.setBounds(gainArea);
    
    controlRow.removeFromLeft(gap);
    
    if (currentBand == SelectedBand::LowCut) {
        auto slopeArea = controlRow.removeFromLeft(knobW);
        slopeLabel.setBounds(slopeArea.removeFromTop(20));
        slopeSlider.setBounds(slopeArea);
    } else {
        auto qArea = controlRow.removeFromLeft(knobW);
        qLabel.setBounds(qArea.removeFromTop(20));
        qSlider.setBounds(qArea);
    }

    // 右側にバンド選択ボタンを並べる
    auto bandsArea = controlRow.removeFromRight(bandsW).withSizeKeepingCentre(bandsW, 44);
    
    float btnW = (bandsW - 20) / 5.0f;
    float btnGap = 5.0f;
    if (bandsW < 220) {
        btnW = (bandsW - 12) / 5.0f;
        btnGap = 3.0f;
    }
    
    for (int i = 0; i < 5; ++i) {
        auto btnGroup = bandsArea.removeFromLeft(static_cast<int>(btnW));
        bandsArea.removeFromLeft(static_cast<int>(btnGap));
        bandButtons[i].setBounds(btnGroup.removeFromTop(26));
        btnGroup.removeFromTop(2);
        enableButtons[i].setBounds(btnGroup.removeFromTop(16));
    }
}

void LowCutPoliceAudioProcessorEditor::updateComponentColors()
{
    const auto& pal = getPalettes()[static_cast<size_t>(currentPaletteIdx)];
    
    // 選択中のバンドに合わせた色
    juce::Colour activeColor = pal.lowcut;
    switch (currentBand) {
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
    
    // バンド選択ボタンとONボタンの色をパレットに合わせる
    juce::Colour bandColors[5] = { pal.lowcut, pal.bell1, pal.bell2, pal.bell3, pal.bell4 };
    for (int i = 0; i < 5; ++i) {
        bandButtons[i].setColour(juce::TextButton::buttonOnColourId, bandColors[i].withAlpha(0.6f));
        bandButtons[i].setColour(juce::TextButton::textColourOffId, pal.text);
        enableButtons[i].setColour(juce::TextButton::textColourOffId, pal.text);
    }
    
    // 各種ヘッダーボタン
    analyzeButton.setColour(juce::TextButton::textColourOffId, pal.text);
    diffButton.setColour(juce::TextButton::textColourOffId, pal.text);
    bypassButton.setColour(juce::TextButton::textColourOffId, pal.text);
    colorButton.setColour(juce::TextButton::textColourOffId, pal.text);
    
    // DiffのON色をパレットのbell1(シアン)やlowcutなどに合わせると調和が良い
    diffButton.setColour(juce::TextButton::buttonOnColourId, pal.bell1.withAlpha(0.6f));
}
