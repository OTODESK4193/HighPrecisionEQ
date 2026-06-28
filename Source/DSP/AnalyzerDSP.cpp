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
    
    // サンプルレートに応じてデシメーション比を動的に決定
    if (sampleRate < 60000.0)      decimationRatio = 32;
    else if (sampleRate < 120000.0) decimationRatio = 64;
    else                           decimationRatio = 128;
    
    lowSampleRate = sampleRate / decimationRatio;
    
    decimationAccumulator = 0.0;
    decimationCounter = 0;
    
    // 1. 低域フィルタバンク (1-200Hz) -> デシメーション信号で処理
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

    // 2. 中高域フィルタバンク (200Hz-25kHz) -> フルレート信号で処理 (インデックス 436 〜 NumBands-1)
    double fmin_high = 200.0;
    double fmax_high = std::min(25000.0, sampleRate * 0.45);
    double Q_high = 24.0;
    
    for (int i = 436; i < NumBands; ++i)
    {
        double proportion = static_cast<double>(i - 436) / (NumBands - 1 - 436);
        double fc = fmin_high * std::pow(fmax_high / fmin_high, proportion);
        
        bands[static_cast<size_t>(i)].updateCoeffs(fc, Q_high, sampleRate);
        bands[static_cast<size_t>(i)].ic1eq = 0.0;
        bands[static_cast<size_t>(i)].ic2eq = 0.0;
        bands[static_cast<size_t>(i)].env = 0.0;
    }
    
    // エンベロープフォロワー時定数 (アタック10ms, リリース150ms)
    attackCoef = std::exp(-1.0 / (0.010 * sampleRate));
    releaseCoef = std::exp(-1.0 / (0.150 * sampleRate));
    
    attackCoefLow = std::exp(-1.0 / (0.010 * lowSampleRate));
    releaseCoefLow = std::exp(-1.0 / (0.150 * lowSampleRate));

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
    for (int i = 0; i < numSamples; ++i)
    {
        double x = static_cast<double>(data[i]);
        
        // 1. 中高域フィルタ (200Hz-25kHz) の処理 (フルサンプルレート)
        for (int b = 436; b < NumBands; ++b)
        {
            double bp = bands[static_cast<size_t>(b)].process(x);
            double absVal = std::abs(bp);
            
            double currentEnv = bands[static_cast<size_t>(b)].env;
            if (absVal > currentEnv)
                currentEnv = attackCoef * currentEnv + (1.0 - attackCoef) * absVal;
            else
                currentEnv = releaseCoef * currentEnv + (1.0 - releaseCoef) * absVal;
                
            bands[static_cast<size_t>(b)].env = currentEnv;
        }

        // 2. 移動平均デシメーションの処理
        decimationAccumulator += x;
        decimationCounter++;
        
        if (decimationCounter >= decimationRatio)
        {
            double x_low = decimationAccumulator / decimationRatio;
            decimationAccumulator = 0.0;
            decimationCounter = 0;
            
            // 3. 低域フィルタ (1-200Hz) の処理 (ダウンサンプルレート)
            for (int b = 0; b < 436; ++b)
            {
                double bp = bands[static_cast<size_t>(b)].process(x_low);
                double absVal = std::abs(bp);
                
                double currentEnv = bands[static_cast<size_t>(b)].env;
                if (absVal > currentEnv)
                    currentEnv = attackCoefLow * currentEnv + (1.0 - attackCoefLow) * absVal;
                else
                    currentEnv = releaseCoefLow * currentEnv + (1.0 - releaseCoefLow) * absVal;
                    
                bands[static_cast<size_t>(b)].env = currentEnv;
            }
        }
        
        // アトミック更新 (CPU負荷軽減のため定期的に反映)
        if ((i & 255) == 0)
        {
            for (int b = 0; b < NumBands; ++b)
            {
                float db = static_cast<float>(20.0 * std::log10(std::max(1e-9, bands[static_cast<size_t>(b)].env)));
                peaks[static_cast<size_t>(b)].store(db, std::memory_order_relaxed);
            }
        }
    }
    
    // 最終ピーク値とホールド値の更新
    bool isHold = holdEnabled.load(std::memory_order_relaxed);
    for (int b = 0; b < NumBands; ++b)
    {
        float db = static_cast<float>(20.0 * std::log10(std::max(1e-9, bands[static_cast<size_t>(b)].env)));
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
