#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

class AnalyzerDSP;
class HighPrecisionEQAudioProcessor;
class HighPrecisionEQAudioProcessorEditor;

class FreqResponseDisplay : public juce::Component
{
public:
    FreqResponseDisplay();
    ~FreqResponseDisplay() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    struct BellParam {
        double freq = 1000.0;
        double gain = 0.0;
        double q = 1.0;
        bool active = false;
    };

    void updateParameters(double cutoffHz, int order, double gainDb, bool lowcutEnable,
                          double highCutFreq, int highCutOrder, double highCutGainDb, bool highCutEnable,
                          double sampleRate, const std::array<BellParam, 4>& bells);

    void setColorPaletteIndex(int index);
    void setAnalyzer(AnalyzerDSP* analyzer);
    void setProcessorAndEditor(HighPrecisionEQAudioProcessor* proc, HighPrecisionEQAudioProcessorEditor* ed);
    void setSelectedBand(int bandIdx);

    float getMinF() const noexcept { return currentMinF; }
    float getMaxF() const noexcept { return currentMaxF; }
    HighPrecisionEQAudioProcessor* getProcessor() const noexcept { return processor; }

    float logFToX(float f) const
    {
        float logMin = std::log10(currentMinF);
        float logMax = std::log10(currentMaxF);
        float val = (std::log10(f) - logMin) / (logMax - logMin);
        return val * static_cast<float>(getWidth());
    }

    float xToLogF(float x) const
    {
        float logMin = std::log10(currentMinF);
        float logMax = std::log10(currentMaxF);
        float val = x / static_cast<float>(getWidth());
        return std::pow(10.0f, logMin + val * (logMax - logMin));
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    AnalyzerDSP* analyzer = nullptr;
    HighPrecisionEQAudioProcessor* processor = nullptr;
    HighPrecisionEQAudioProcessorEditor* editor = nullptr;

    int activeDragBand = -1; // -1:なし, 0:LowCut, 1:HighCut, 2..5:Bell1..4
    int selectedBandIdx = 0;

    bool isHovering = false;
    juce::String hoverText;
    int mouseX = -1;
    int mouseY = -1;

    std::unique_ptr<juce::VBlankAttachment> vblankAttachment;
    void vblankUpdate();

    double currentCutoffHz = 80.0;
    int    currentOrder = 2;
    double currentGainDb     = 0.0;
    double currentSampleRate = 44100.0;
    bool   currentLowcutEnable = true;
    double currentHighCutFreq = 20000.0;
    int    currentHighCutOrder = 2;
    double currentHighCutGainDb = -10.0;
    bool   currentHighCutEnable = false;
    std::array<BellParam, 4> bellParams;

    int currentPaletteIdx = 0;

    juce::Path cachedResponsePath;
    juce::ColourGradient cachedLineGrad;
    juce::ColourGradient cachedFillGrad;
    bool pathNeedsRecalculation = true;

    struct PrecomputedFreq {
        float f;
        double cosw, sinw, cos2w, sin2w;
    };
    std::vector<PrecomputedFreq> precomputedFreqs;
    void precomputeFrequencies();

    float gainToY(float gainDecibels) const;
    float yToGain(float y) const;
    float analyzerGainToY(float gainDecibels) const;

    // LC/HCポイントのY座標 (実際のEQカーブ上に配置。カットオフでは約-3dB)
    float cutPointY(double freqHz) const;

    // カーブ再計算用スクラッチ (再確保を避けるためメンバー化)
    std::vector<double> curveFreqs, curveMags;

    void drawEQPoints(juce::Graphics& g);

    // (mx,my) から半径 radius 以内で最も近いEQポイントのバンド番号を返す (なければ-1)。
    // includeDisabled=false なら有効なバンドのみ対象 (ドラッグ/ホイール用)、
    // true なら無効(Off)なバンドも対象 (ダブルクリックのOn/Offトグル用)。
    // preferredBand を指定すると、そのバンドは判定半径を preferredExtra 分広げ、
    // かつ距離を preferredExtra 分優遇する (選択中の点を掴みやすくするため)。
    int findNearestBand(float mx, float my, float radius, bool includeDisabled,
                        int preferredBand = -1, float preferredExtra = 0.0f) const;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;

    float currentMinF = 1.0f, currentMaxF = 25000.0f;
    float currentMinDb = -12.0f, currentMaxDb = 12.0f;
    float analyzerGainOffsetDb = 0.0f;
    uint64_t lastSeenUpdateCount = 0;

    float dragStartMinF = 1.0f, dragStartMaxF = 25000.0f;
    float dragStartMinDb = -12.0f;

    // --- 縦軸表示モード ---
    // autoFitV      : 可視域オートフィット (見えている帯域のmin/maxに縦軸を合わせる)。
    //                 ONのときアナライザー/EQカーブ/Hold/グリッドを同一の統一窓で描画する。
    // relativeMode  : 相対表示 (可視スペクトルの平滑トレンドを引いて残差を強調)。
    bool autoFitV = false;
    bool relativeMode = false;

    // オートフィットの目標窓 (時間方向に平滑化して適用し、ジッターを抑える)
    float viewMinDb = -12.0f, viewMaxDb = 12.0f;

    // 実際に各変換で使う「有効な縦窓」(モードに応じて paint 冒頭で更新)
    float effCurveMinDb = -12.0f, effCurveMaxDb = 12.0f; // gainToY/yToGain/cutPointY 用
    float effAnaMinDb = -70.0f, effAnaMaxDb = 10.0f;     // analyzerGainToY 用

    // アナライザーのピクセル単位dB配列 (paint冒頭で事前計算し描画で再利用)
    std::vector<float> anaDbPerX, holdDbPerX;
    bool haveAnaData = false;

    // 周波数fにおけるアナライザーdBをバンド配列から補間
    float interpAnalyzerDb(double f, const std::vector<float>& src) const;

    // paint 内で縦窓を更新するヘルパー (relativeMode 適用後の可視dB配列を渡す)
    void updateVerticalWindows(const std::vector<float>& analyzerDbPerX);

    juce::TextButton zoomInXBtn{ "H+" }, zoomOutXBtn{ "H-" };
    juce::TextButton autoFitButton{ "Auto V" }, relativeButton{ "Flat" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreqResponseDisplay)
};
