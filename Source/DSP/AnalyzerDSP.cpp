#include "AnalyzerDSP.h"

AnalyzerDSP::AnalyzerDSP()
    : juce::Thread("AnalyzerDSP")
{
    bands.resize(NumBands);
    peaks = std::make_unique<std::atomic<float>[]>(NumBands);
    holdPeaks = std::make_unique<std::atomic<float>[]>(NumBands);
    for (int i = 0; i < NumBands; ++i)
    {
        peaks[i].store(-180.0f, std::memory_order_relaxed);
        holdPeaks[i].store(-180.0f, std::memory_order_relaxed);
    }

    ringBuffer.resize(BufferSize, 0.0f);

    startThread();
}

AnalyzerDSP::~AnalyzerDSP()
{
    signalThreadShouldExit();
    notify();
    stopThread(3000);
}

void AnalyzerDSP::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;

    // サンプルレートに応じてミッドレートを約11-12kHzに揃える
    if (sampleRate < 60000.0)       midDecimationRatio = 4;
    else if (sampleRate < 120000.0) midDecimationRatio = 8;
    else                            midDecimationRatio = 16;

    midSampleRate = sampleRate / midDecimationRatio;      // 約 11-12 kHz
    lowSampleRate = midSampleRate / LowSubRatio;          // 約 1.4-1.5 kHz

    // アンチエイリアスフィルタ設計
    // aaMid: ミッドバンド上限2kHzに対し fc=2.6kHz。ミッド帯へ折り返す成分 (>= midSR-2kHz ≈ 9kHz)
    //        を約86dB以上抑制。2kHzでの通過帯域落ち込みは -0.07dB 未満。
    // aaLow: ローバンド上限200Hzに対し fc=270Hz。ロー帯へ折り返す成分 (>= lowSR-200Hz ≈ 1.2kHz)
    //        を約100dB以上抑制。200Hzでの落ち込みは -0.04dB 未満。
    aaMid.design(2600.0, sampleRate);
    aaLow.design(270.0, midSampleRate);
    midCounter = 0;
    lowCounter = 0;

    // 1. 低域フィルタバンク (1-200Hz) -> ローレート信号で処理
    // - 1.0Hz 〜 60.0Hz まで 0.2Hzステップ: 296バンド (インデックス 0 〜 295)
    for (int i = 0; i < 296; ++i)
    {
        double fc = 1.0 + i * 0.2;
        double Q = std::clamp(fc / 4.0, 4.0, 24.0);

        bands[static_cast<size_t>(i)].updateCoeffs(fc, Q, lowSampleRate);
        bands[static_cast<size_t>(i)].ic1eq = 0.0;
        bands[static_cast<size_t>(i)].ic2eq = 0.0;
        bands[static_cast<size_t>(i)].env = 0.0;
    }

    // - 61.0Hz 〜 200.0Hz まで 1.0Hzステップ: 140バンド (インデックス 296 〜 435)
    for (int i = 296; i < 436; ++i)
    {
        double fc = 61.0 + (i - 296) * 1.0;
        double Q = std::clamp(fc / 4.0, 4.0, 24.0);

        bands[static_cast<size_t>(i)].updateCoeffs(fc, Q, lowSampleRate);
        bands[static_cast<size_t>(i)].ic1eq = 0.0;
        bands[static_cast<size_t>(i)].ic2eq = 0.0;
        bands[static_cast<size_t>(i)].env = 0.0;
    }

    // 2. 中高域フィルタバンク (200Hz-25kHz、対数等間隔)
    //    fc < 2kHz のバンドはミッドレートで、それ以上はフルレートで処理する
    double fmin_high = 200.0;
    double fmax_high = std::min(25000.0, sampleRate * 0.45);
    double Q_high = 24.0;

    highBandStart = NumBands;
    for (int i = 436; i < NumBands; ++i)
    {
        double proportion = static_cast<double>(i - 436) / (NumBands - 1 - 436);
        double fc = fmin_high * std::pow(fmax_high / fmin_high, proportion);

        bool isMidRate = (fc < MidBandMaxFreq);
        if (!isMidRate && highBandStart == NumBands)
            highBandStart = i;

        double bandRate = isMidRate ? midSampleRate : sampleRate;
        bands[static_cast<size_t>(i)].updateCoeffs(fc, Q_high, bandRate);
        bands[static_cast<size_t>(i)].ic1eq = 0.0;
        bands[static_cast<size_t>(i)].ic2eq = 0.0;
        bands[static_cast<size_t>(i)].env = 0.0;
    }

    writePos.store(0, std::memory_order_relaxed);
    readPos = 0;
    std::fill(ringBuffer.begin(), ringBuffer.end(), 0.0f);
    localBuf.clear();
    localBuf.reserve(BufferSize);
}

void AnalyzerDSP::pushAudio(const float* data, int numSamples)
{
    int wp = writePos.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i)
    {
        ringBuffer[(wp + i) & BufferMask] = data[i];
    }
    writePos.store(wp + numSamples, std::memory_order_release);
}

void AnalyzerDSP::run()
{
    while (!threadShouldExit())
    {
        wait(5); // 滑らかな追従のためにスリープを 5ms に設定

        if (threadShouldExit())
            break;

        int currentWrite = writePos.load(std::memory_order_acquire);
        int available = currentWrite - readPos;

        if (available > BufferSize)
        {
            readPos = currentWrite - BufferSize; // オーバーフロー処理
            available = BufferSize;
        }

        if (available > 0)
        {
            if (threadShouldExit())
                break;

            localBuf.resize(static_cast<size_t>(available));
            for (int i = 0; i < available; ++i)
            {
                localBuf[static_cast<size_t>(i)] = ringBuffer[(readPos + i) & BufferMask];
            }
            readPos = currentWrite;

            processInternal(localBuf.data(), available);
        }
    }
}

void AnalyzerDSP::processInternal(const float* data, int numSamples)
{
    // バンドパス出力 -> エンベロープフォロワー更新
    auto runBand = [](AnalyzerBand& band, double in)
    {
        double bp = band.process(in);
        double absVal = std::abs(bp);

        double currentEnv = band.env;
        if (absVal > currentEnv)
            currentEnv = band.attackCoef * currentEnv + (1.0 - band.attackCoef) * absVal;
        else
            currentEnv = band.releaseCoef * currentEnv + (1.0 - band.releaseCoef) * absVal;

        band.env = currentEnv;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        double x = static_cast<double>(data[i]);

        // 1. 高域バンド (2kHz-25kHz) -> フルレート処理
        for (int b = highBandStart; b < NumBands; ++b)
            runBand(bands[static_cast<size_t>(b)], x);

        // 2. アンチエイリアス -> ミッドレートへデシメーション
        double xm = aaMid.process(x);
        if (++midCounter >= midDecimationRatio)
        {
            midCounter = 0;

            // 中域バンド (200Hz-2kHz) -> ミッドレート処理
            for (int b = 436; b < highBandStart; ++b)
                runBand(bands[static_cast<size_t>(b)], xm);

            // 3. アンチエイリアス -> ローレートへさらにデシメーション
            double xl = aaLow.process(xm);
            if (++lowCounter >= LowSubRatio)
            {
                lowCounter = 0;

                // 低域バンド (1-200Hz) -> ローレート処理
                for (int b = 0; b < 436; ++b)
                    runBand(bands[static_cast<size_t>(b)], xl);
            }
        }
    }

    // 表示キャリブレーション: エンベロープフォロワーは0dBFSサインに対して約-1dBを
    // 指すため (整流平均とアタック/リリース特性による系統誤差)、+1dB補正して
    // 「0dBFSサイン = 0dB表示」に合わせる。実測誤差は全帯域で±0.25dB以内。
    constexpr double CalibrationDb = 1.0;

    // 最終ピーク値とホールド値の更新 (run()の起床ごと ≒ 5ms 間隔で十分)
    bool isHold = holdEnabled.load(std::memory_order_relaxed);
    for (int b = 0; b < NumBands; ++b)
    {
        float db = static_cast<float>(20.0 * std::log10(std::max(1e-9, bands[static_cast<size_t>(b)].env)) + CalibrationDb);
        peaks[static_cast<size_t>(b)].store(db, std::memory_order_relaxed);

        if (isHold)
        {
            float currentHold = holdPeaks[static_cast<size_t>(b)].load(std::memory_order_relaxed);
            if (db > currentHold)
                holdPeaks[static_cast<size_t>(b)].store(db, std::memory_order_relaxed);
        }
        else
        {
            // ホールドOFFのときは現在の値で上書き（またはリセット）
            holdPeaks[static_cast<size_t>(b)].store(db, std::memory_order_relaxed);
        }
    }

    updateCount.fetch_add(1, std::memory_order_release);
}

std::vector<float> AnalyzerDSP::getEnergies()
{
    std::vector<float> res(NumBands);
    for (int i = 0; i < NumBands; ++i)
    {
        res[static_cast<size_t>(i)] = peaks[static_cast<size_t>(i)].load(std::memory_order_relaxed);
    }
    return res;
}

std::vector<float> AnalyzerDSP::getHoldEnergies()
{
    std::vector<float> res(NumBands);
    for (int i = 0; i < NumBands; ++i)
    {
        res[static_cast<size_t>(i)] = holdPeaks[static_cast<size_t>(i)].load(std::memory_order_relaxed);
    }
    return res;
}

void AnalyzerDSP::setHold(bool shouldHold)
{
    bool wasHold = holdEnabled.exchange(shouldHold);
    if (!wasHold && shouldHold)
    {
        // OFFからONになった瞬間、現在のピーク値でホールドを初期化する
        for (int i = 0; i < NumBands; ++i)
        {
            float db = peaks[i].load(std::memory_order_relaxed);
            holdPeaks[i].store(db, std::memory_order_relaxed);
        }
    }
}
