#include "AnalyzerDSP.h"

AnalyzerDSP::AnalyzerDSP()
    : juce::Thread("AnalyzerDSP")
{
    bands.resize(NumBands);
    peaks = std::make_unique<std::atomic<float>[]>(NumBands);
    for (int i = 0; i < NumBands; ++i)
        peaks[i].store(-180.0f, std::memory_order_relaxed);

    ringBuffer.resize(BufferSize, 0.0f);
    
    // Start thread
    startThread();
}

AnalyzerDSP::~AnalyzerDSP()
{
    stopThread(2000);
}

void AnalyzerDSP::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
    
    // Logarithmic frequency spacing
    double fmin = 10.0;
    double fmax = 24000.0;
    
    for (int i = 0; i < NumBands; ++i)
    {
        double fc = fmin * std::pow(fmax / fmin, static_cast<double>(i) / (NumBands - 1));
        double Q = 12.0; // Q roughly corresponds to 1/24th octave resonance
        
        // Prevent SVF instability near Nyquist
        double maxFc = sampleRate * 0.40;
        if (fc > maxFc) fc = maxFc;
        
        bands[i].updateCoeffs(fc, Q, sampleRate);
        bands[i].ic1eq = 0.0;
        bands[i].ic2eq = 0.0;
        bands[i].env = 0.0;
    }
    
    // Envelope follower (fast attack, medium release for smooth visual)
    attackCoef = std::exp(-1.0 / (0.010 * sampleRate)); // 10ms
    releaseCoef = std::exp(-1.0 / (0.150 * sampleRate)); // 150ms

    writePos.store(0, std::memory_order_relaxed);
    readPos = 0;
    std::fill(ringBuffer.begin(), ringBuffer.end(), 0.0f);
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
    // Removed notify() to prevent excessive thread wake-ups. 
    // The thread uses wait(30) so it will naturally process blocks every 30ms,
    // dramatically reducing CPU load from thread switching.
}

void AnalyzerDSP::run()
{
    while (!threadShouldExit())
    {
        wait(30); // Max ~33fps refresh processing
        
        int currentWrite = writePos.load(std::memory_order_acquire);
        int available = currentWrite - readPos;
        
        if (available > BufferSize)
        {
            readPos = currentWrite - BufferSize; // Handle overflow
            available = BufferSize;
        }
        
        if (available > 0)
        {
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
            double bp = bands[b].process(x);
            double absVal = std::abs(bp);
            
            double currentEnv = bands[b].env;
            if (absVal > currentEnv)
                currentEnv = attackCoef * currentEnv + (1.0 - attackCoef) * absVal;
            else
                currentEnv = releaseCoef * currentEnv + (1.0 - releaseCoef) * absVal;
                
            bands[b].env = currentEnv;
            
            // Only update atomics occasionally to save CPU
            if ((i & 511) == 0)
            {
                // Convert to dB
                float db = static_cast<float>(20.0 * std::log10(std::max(1e-9, currentEnv)));
                peaks[b].store(db, std::memory_order_relaxed);
            }
        }
    }
    
    // Final store for this chunk
    for (int b = 0; b < NumBands; ++b)
    {
        float db = static_cast<float>(20.0 * std::log10(std::max(1e-9, bands[b].env)));
        peaks[b].store(db, std::memory_order_relaxed);
    }
}

std::vector<float> AnalyzerDSP::getEnergies()
{
    std::vector<float> res(NumBands);
    for (int i = 0; i < NumBands; ++i)
    {
        res[i] = peaks[i].load(std::memory_order_relaxed);
    }
    return res;
}
