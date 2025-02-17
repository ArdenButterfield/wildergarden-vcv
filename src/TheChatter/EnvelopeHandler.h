//
// Created by arden on 2/13/25.
//

#ifndef WILDERGARDEN_VCV_ENVELOPEHANDLER_H
#define WILDERGARDEN_VCV_ENVELOPEHANDLER_H

#include "../plugin.hpp"
#include "FormantFetcher.h"
#include <memory>

class EnvelopeHandler {
public:
    struct EnvelopeSample {
        std::array<float, 2> formants;
        float vowelAmp;

        float noiseAmp;
        float noiseLowerFrequency;
        float noiseUpperFrequency;

        float frequency;
        bool noteStart;
    };

    EnvelopeHandler() {}
    ~EnvelopeHandler() {}

    void process(float sampleRate,
                 float plosiveParam,
                 float vowelStartParam,
                 float vowelEndParam,
                 float gate,
                 float root,
                 float voltsPerOctave,
                 EnvelopeSample& out) {
        if (noteOnTrigger.process(gate, 0.1f, 1.f)) {
            formantMorphStep = 1.f / (formantMorphTime * sampleRate);
            formantMorphCounter = 1.f;
            out.noteStart = true;
            formantFetcher.fetchPlosive(plosiveParam, plosiveInfo);
            plosivePulse.trigger(plosiveInfo.length);
            vowelDelay.trigger(plosiveInfo.vowelDelayLength);
        } else {
            out.noteStart = false;
        }

        std::array<float, 2> startFormants;
        std::array<float, 2> endFormants;

        formantFetcher.fetchStartVowel(vowelStartParam, startFormants);
        formantFetcher.fetchEndVowel(vowelEndParam, endFormants);
        auto formantScaling = std::pow(2.f, root * 0.3f);

        out.noiseAmp = plosivePulse.process(1 / sampleRate) ? plosiveInfo.gain : 0.0; // TODO: real envelope
        out.noiseUpperFrequency = plosiveInfo.hiFreq;
        out.noiseLowerFrequency = plosiveInfo.loFreq;

        auto vowelOn = !vowelDelay.process(1 / sampleRate);
        out.vowelAmp = gate > 0 && vowelOn ? 1.0 : 0.0;
        out.frequency = MIDDLE_C * std::pow(2, root + voltsPerOctave);
        auto formantProgression = 1 - std::pow(formantMorphCounter, 2);
        for (int i = 0; i < 2; ++i) {
            out.formants[i] = formantProgression * endFormants[i] + (1 - formantProgression) * startFormants[i];
            out.formants[i] *= formantScaling;
        }
        if (vowelOn) {
            formantMorphCounter = std::max(0.f, formantMorphCounter - formantMorphStep);
        }
    }

private:
    dsp::SchmittTrigger noteOnTrigger;
    const float MIDDLE_C = 261.626;

    const float formantMorphTime = 0.5;
    float formantMorphStep;
    float formantMorphCounter;

    FormantFetcher formantFetcher;
    float noiseCounter;
    PlosiveInfo plosiveInfo;
    dsp::PulseGenerator plosivePulse;
    dsp::PulseGenerator vowelDelay;
};


#endif //WILDERGARDEN_VCV_ENVELOPEHANDLER_H
