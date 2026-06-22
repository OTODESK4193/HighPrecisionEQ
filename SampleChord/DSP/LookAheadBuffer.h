#pragma once
#include <JuceHeader.h>
#include <vector>

// ============================================================================
//  LookAheadBuffer.h
//  ゼロ位相 filtfilt のためのルックアヘッドバッファ管理
//
//  ゼロ位相 IIR は非因果フィルタのため、リアルタイム処理では
//  固定のルックアヘッド（先読み）が必要。
//
//  レイテンシー設計（20Hz, 4次Butterworth の場合）:
//    44.1 kHz → ~3,300 samples (~74.8 ms)
//    48  kHz  → ~3,600 samples (~75.0 ms)
//    96  kHz  → ~7,200 samples (~75.0 ms)
//    192 kHz  → ~14,400 samples (~75.0 ms)
//
//  Ableton Live PDC: setLatencySamples() で自動補正される
// ============================================================================

// フェーズ 2 で実装予定（スタブ）
class LookAheadBuffer
{
public:
    LookAheadBuffer() = default;

    // TODO: フェーズ2で実装
    // void prepare(double sampleRate, int maxBlockSize, int filterOrder, double cutoffHz);
    // int  getLatencySamples() const noexcept { return latencySamples; }
    // void pushSamples(const juce::AudioBuffer<float>& input);
    // bool popSamples(juce::AudioBuffer<float>& output);

private:
    int latencySamples = 0;
};
