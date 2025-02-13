//
// Created by arden on 2/13/25.
//

#ifndef WILDERGARDEN_VCV_OSCILLATORBANK_H
#define WILDERGARDEN_VCV_OSCILLATORBANK_H

#include <array>
#include <cmath>
#include <iostream>

struct CyleOscillator {
    float x;
    float y;
    float m00, m01, m10, m11;
    float frequency;
    float startingPhase;
    float magnitude;
    float initialFormantGain;
    float finalFormantGain;

    float tick() {
        float newX, newY;
        newX = x * m00 + y * m01;
        newY = x * m10 + y * m11;
        x = newX;
        y = newY;
        return y;
    }
};

class OscillatorBank {
public:
    OscillatorBank() {
        reset();
        fs = 0;
        setFrequency(0, 44100);
        formantCurvesNeedRecalculating = true;
    }

    ~OscillatorBank() { }

    void reset() {
        for (int i = 0; i < oscillators.size(); ++i) {
            oscillators[i].startingPhase = 0;
            oscillators[i].magnitude = 1.f / static_cast<float>(i + 1);
            oscillators[i].x = oscillators[i].magnitude * cosf(oscillators[i].startingPhase);
            oscillators[i].y = oscillators[i].magnitude * sinf(oscillators[i].startingPhase);
        }
    }

    void setFormants(float f1_initial, float f2_initial, float f1_final, float f2_final) {
        if (f1_initial == initialFormants[0] && f2_initial == initialFormants[1]
            && f1_final == finalFormants[0] && f2_final == finalFormants[1]) {
            return;
        }
        initialFormants[0] = f1_initial;
        initialFormants[1] = f2_initial;
        finalFormants[0] = f1_final;
        finalFormants[1] = f2_final;
        formantCurvesNeedRecalculating = true;
    }

    void setFrequency(float frequency, float sampleRate) {
        if (sampleRate == fs && frequency == oscillators[0].frequency) {
            return;
        }
        fs = sampleRate;
        nyquist = fs * 0.45;
        float overtone = 1;
        float freqScaler = twoPi / sampleRate;
        for (auto& oscillator : oscillators) {
            oscillator.frequency = overtone * frequency;
            float angleIncrement = oscillator.frequency * freqScaler;
            oscillator.m00 = cosf(angleIncrement);
            oscillator.m01 = -sinf(angleIncrement);
            oscillator.m10 = sinf(angleIncrement);
            oscillator.m11 = cosf(angleIncrement);
            overtone += 1;
        }
        formantCurvesNeedRecalculating = true;
    }

    float tick() {
        if (formantCurvesNeedRecalculating) {
            recalculateFormantCurves();
        }

        auto out = 0.f;
        for (auto& oscillator : oscillators) {
            auto oscVal = oscillator.tick();
            // std::cout << oscVal << "\n";
            if (oscillator.frequency < nyquist) {
                out += oscVal * oscillator.initialFormantGain;
            }
        }
        return out;
    }

private:
    void recalculateFormantCurves() {
        const float scalingPerOctave = 0.1f; // TODO: get rid of log and exp using math

        for (auto& oscillator : oscillators) {
            auto f = oscillator.frequency;
            oscillator.initialFormantGain = 0;
            oscillator.finalFormantGain = 0;

            for (auto formant : initialFormants) {
                auto diff = std::abs(std::log2(f / formant));
                oscillator.initialFormantGain += std::pow(scalingPerOctave, diff);
            }
            for (auto formant : finalFormants) {
                auto diff = std::abs(std::log2(f / formant));
                oscillator.finalFormantGain += std::pow(scalingPerOctave, diff);
            }
        }
        formantCurvesNeedRecalculating = false;
    }

    static const int NUM_OSCILLATORS = 256;
    std::array<CyleOscillator, NUM_OSCILLATORS> oscillators;
    float fs;
    float nyquist;
    const float twoPi = 2 * 3.141592653589793238;

    bool formantCurvesNeedRecalculating;

    std::array<float, 2> initialFormants;
    std::array<float, 2> finalFormants;
};


#endif //WILDERGARDEN_VCV_OSCILLATORBANK_H
