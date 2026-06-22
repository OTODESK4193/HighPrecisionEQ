#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <array>
#include <numbers>

// ============================================================================
//  TPTSVFBellFilter.h
//  Topology-Preserving Transform State Variable Filter for Peak (Bell) Cut
// ============================================================================

class TPTSVFBellFilter
{
public:
    TPTSVFBellFilter() = default;

    void prepare(double sampleRate, int /*maxBlockSize*/)
    {
        currentSampleRate = sampleRate;
        reset();
        active = false;
    }

    void reset()
    {
        for (auto& s : s1) s = 0.0;
        for (auto& s : s2) s = 0.0;
    }

    void setActive(bool isActive)
    {
        active = isActive;
    }

    void updateParameters(double cutoffHz, double gainDb, double Q)
    {
        if (cutoffHz < 20.0) cutoffHz = 20.0;
        if (cutoffHz > currentSampleRate * 0.49) cutoffHz = currentSampleRate * 0.49;
        if (Q < 0.1) Q = 0.1;

        double V = std::pow(10.0, gainDb / 20.0);
        double wd = 2.0 * std::numbers::pi * cutoffHz;
        double T = 1.0 / currentSampleRate;
        g = std::tan(wd * T / 2.0);
        double k_raw = 1.0 / Q;
        
        double V_safe = std::max(V, 1e-6);
        if (gainDb >= 0.0)
        {
            // Boost
            D = 1.0 / (1.0 + g * k_raw + g * g);
            kV = k_raw;
            k = k_raw * V_safe;
        }
        else
        {
            // Cut
            D = 1.0 / (1.0 + g * (k_raw / V_safe) + g * g);
            kV = k_raw / V_safe;
            k = k_raw;
        }
        
        lastFreq = cutoffHz;
        lastGain = gainDb;
        lastQ = Q;
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        if (!active || std::abs(lastGain) < 0.01) return;

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        for (int ch = 0; ch < std::min(numChannels, 2); ++ch)
        {
            float* channelData = buffer.getWritePointer(ch);
            double state1 = s1[static_cast<size_t>(ch)];
            double state2 = s2[static_cast<size_t>(ch)];

            for (int i = 0; i < numSamples; ++i)
            {
                double x = channelData[i];
                
                // TPT SVF Processing
                double hp = (x - (kV + g) * state1 - state2) * D;
                double v1 = g * hp;
                double bp = v1 + state1;
                state1 = bp + v1;
                
                double v2 = g * bp;
                double lp = v2 + state2;
                state2 = lp + v2;
                
                double y = lp + k * bp + hp;
                
                // Denormal protection removed for auto-vectorization
                channelData[i] = static_cast<float>(y);
            }
            
            s1[static_cast<size_t>(ch)] = state1;
            s2[static_cast<size_t>(ch)] = state2;
        }
    }

    // Magnitude response calculation
    double getMagnitudeForFrequency(double freq, double sampleRate) const
    {
        if (!active || std::abs(lastGain) < 0.01) return 1.0;
        
        double A = std::pow(10.0, lastGain / 40.0);
        double w0 = 2.0 * std::numbers::pi * lastFreq / sampleRate;
        double alpha = std::sin(w0) / (2.0 * lastQ);
        
        double a0 = 1.0 + alpha / A;
        double b0 = (1.0 + alpha * A) / a0;
        double b1 = (-2.0 * std::cos(w0)) / a0;
        double b2 = (1.0 - alpha * A) / a0;
        double a1 = (-2.0 * std::cos(w0)) / a0;
        double a2 = (1.0 - alpha / A) / a0;
        
        double w = 2.0 * std::numbers::pi * freq / sampleRate;
        double cosw = std::cos(w);
        double cos2w = std::cos(2.0 * w);
        
        double numRe = b0 + b1 * cosw + b2 * cos2w;
        double numIm = -b1 * std::sin(w) - b2 * std::sin(2.0 * w);
        double numMagSq = numRe * numRe + numIm * numIm;
        
        double denRe = 1.0 + a1 * cosw + a2 * cos2w;
        double denIm = -a1 * std::sin(w) - a2 * std::sin(2.0 * w);
        double denMagSq = denRe * denRe + denIm * denIm;
        
        if (denMagSq == 0.0) return 1.0;
        return std::sqrt(numMagSq / denMagSq);
    }

private:
    double currentSampleRate = 44100.0;
    bool active = false;
    
    // TPT SVF State & Coefficients
    std::array<double, 2> s1 = {0.0, 0.0};
    std::array<double, 2> s2 = {0.0, 0.0};
    
    double g = 0.0;
    double k = 0.0;
    double kV = 0.0;
    double D = 0.0;

    double lastFreq = 1000.0;
    double lastGain = 0.0;
    double lastQ = 1.0;
};
