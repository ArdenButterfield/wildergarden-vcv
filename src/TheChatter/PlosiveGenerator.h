//
// Created by arden on 2/16/25.
//

#ifndef WILDERGARDEN_VCV_PLOSIVEGENERATOR_H
#define WILDERGARDEN_VCV_PLOSIVEGENERATOR_H

#include <random>
#include "EnvelopeHandler.h"

// Basically noise -> vca -> resonant lo and hi pass filters

class PlosiveGenerator {
public:
    PlosiveGenerator() {
        srand (static_cast <unsigned> (time(0)));
    }
    ~PlosiveGenerator() {}

    float process(EnvelopeHandler::EnvelopeSample envelopeSample, float sampleRate) {
        float noise = static_cast <float> (rand()) / static_cast <float> (RAND_MAX / 2) - 1; // noise between -1 and 1

        if (envelopeSample.noteStart) {
            noise = 1; // always start with an impulse, for snappier sound
        }

        noise *= envelopeSample.noiseAmp;

        filters[0].setParameters(dsp::BiquadFilter::HIGHPASS, envelopeSample.noiseLowerFrequency / sampleRate, 3.0, 0);
        filters[1].setParameters(dsp::BiquadFilter::LOWPASS, envelopeSample.noiseUpperFrequency / sampleRate, 3.0, 0);

        return filters[1].process(filters[0].process(noise));
    }

private:
    std::array<dsp::BiquadFilter, 2> filters;
};


#endif //WILDERGARDEN_VCV_PLOSIVEGENERATOR_H
