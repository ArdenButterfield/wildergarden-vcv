//
// Created by arden on 2/13/25.
//

#ifndef WILDERGARDEN_VCV_FILTER_H
#define WILDERGARDEN_VCV_FILTER_H

#include <array>
#include <cmath>

// basic biquad filter. should experiment with other filter algorithms later.
// source: https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
class Filter {
public:
    enum Mode {
        filter_lopass,
        filter_hipass,
        filter_bandpass
    };

    Filter() {
        initialized = false;
    }

    void initialize(float fs) {
        samplerate = fs;
        x.fill(0);
        y.fill(0);
        a.fill(0);
        b.fill(0);
        mode = filter_lopass;
        initialized = true;
    }



    void setParameters(float _centerFreq, float _q, Mode _mode) {
        if (mode == _mode && q == _q && centerFreq == _centerFreq) {
            return;
        }
        mode = _mode; q = _q; centerFreq = _centerFreq;

        float omega = 6.28318530718 * (centerFreq / samplerate);
        float cosOmega = std::cos(omega);
        float sinOmega = std::sin(omega);
        float alpha = sinOmega / (2 * q);

        if (mode == filter_lopass) {
            b[0] = (1 - cosOmega) * 0.5;
            b[1] = (1 - cosOmega);
            b[2] = (1 - cosOmega) * 0.5;
            a[0] = 1 + alpha;
            a[1] = -2 * cosOmega;
            a[2] = 1 - alpha;
        } else if (mode == filter_hipass) {
            b[0] = (1 + cosOmega) * 0.5;
            b[1] = -(1 + cosOmega);
            b[2] = (1 + cosOmega) * 0.5;
            a[0] = 1 + alpha;
            a[1] = -2 * cosOmega;
            a[2] = 1 - alpha;
        } else if (mode == filter_bandpass) {
            b[0] = q * alpha;
            b[1] = 0;
            b[2] = -q * alpha;
            a[0] = 1 + alpha;
            a[1] = -2 * cosOmega;
            a[2] = 1 - alpha;
        }

        float inverseA0 = 1 / a[0];
        for (auto& i : b) {
            i *= inverseA0;
        }
        for (auto& i : a) {
            i *= inverseA0;
        }
    }

    float process(float sample) {
        float out = b[0] * sample + b[1] * x[0] + b[2] * x[1] - a[1] * y[0] - a[2] * y[1];
        y[1] = y[0]; y[0] = out;
        x[1] = x[0]; x[0] = sample;
        return out;
    }

    bool isInitialized() {
        return initialized;
    }
private:
    float samplerate;
    std::array<float, 2> x;
    std::array<float, 2> y;
    float centerFreq;
    float q;
    Mode mode;
    std::array<float, 3> a;
    std::array<float, 3> b;
    bool initialized;
};


#endif //WILDERGARDEN_VCV_FILTER_H
