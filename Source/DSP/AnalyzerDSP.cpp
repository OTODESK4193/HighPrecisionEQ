#include "AnalyzerDSP.h"

AnalyzerDSP::AnalyzerDSP()
    : juce::Thread("AnalyzerDSP")
{
    bands.resize(NumBands);
    peaks = std::make_unique<std::atomic<float>[]>(NumBands);
    for (int i = 0; i < NumBands; ++i)
        peaks[i].store(-180.0f, std::memory_order_relaxed);

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
    
    double fmin = 10.0;
    double fmax = std::min(24000.0, sampleRate * 0.49);
    double Q = 24.0; // ユーザー指定の Q = 24.0
    
    for (int i = 0; i < NumBands; ++i)
    {
        double fc = fmin * std::pow(fmax / fmin, static_cast<double>(i) / (NumBands - 1));
        
        // ナイキスト限界近くの安定性ガード
        double maxFc = sampleRate * 0.45;
        if (fc > maxFc) fc = maxFc;
        
        bands[static_cast<size_t>(i)].updateCoeffs(fc, Q, sampleRate);
        bands[static_cast<size_t>(i)].ic1eq = 0.0;
        bands[static_cast<size_t>(i)].ic2eq = 0.0;
        bands[static_cast<size_t>(i)].env = 0.0;
    }
    
    // エンベロープフォロワー時定数 (アタック10ms, リリース150ms)
    attackCoef = std::exp(-1.0 / (0.010 * sampleRate));
    releaseCoef = std::exp(-1.0 / (0.150 * sampleRate));

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
        
        for (int b = 0; b < NumBands; ++b)
        {
            double bp = bands[static_cast<size_t>(b)].process(x);
            double absVal = std::abs(bp);
            
            double currentEnv = bands[static_cast<size_t>(b)].env;
            if (absVal > currentEnv)
                currentEnv = attackCoef * currentEnv + (1.0 - attackCoef) * absVal;
            else
                currentEnv = releaseCoef * currentEnv + (1.0 - releaseCoef) * absVal;
                
            bands[static_cast<size_t>(b)].env = currentEnv;
            
            // CPU節約のため時々アトミックに書き込む
            if ((i & 255) == 0)
            {
                float db = static_cast<float>(20.0 * std::log10(std::max(1e-9, currentEnv)));
                peaks[static_cast<size_t>(b)].store(db, std::memory_order_relaxed);
            }
        }
    }
    
    // このチャンクの最終値をアトミックに反映
    for (int b = 0; b < NumBands; ++b)
    {
        float db = static_cast<float>(20.0 * std::log10(std::max(1e-9, bands[static_cast<size_t>(b)].env)));
        peaks[static_cast<size_t>(b)].store(db, std::memory_order_relaxed);
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
