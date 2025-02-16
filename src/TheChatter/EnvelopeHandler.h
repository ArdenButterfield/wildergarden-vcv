//
// Created by arden on 2/13/25.
//

#ifndef WILDERGARDEN_VCV_ENVELOPEHANDLER_H
#define WILDERGARDEN_VCV_ENVELOPEHANDLER_H

#include "../plugin.hpp"
#include "FormantFetcher.h"

class EnvelopeHandler {
public:
    struct EnvelopeSample {
        std::array<float, 2> formants;
        float amp;
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
        }
        std::array<float, 2> startFormants;
        std::array<float, 2> endFormants;

        formantFetcher.fetchStartVowel(vowelStartParam, startFormants);
        formantFetcher.fetchEndVowel(vowelEndParam, endFormants);
        auto formantScaling = std::pow(2.f, root * 0.3f);


        out.amp = gate > 0 ? 1.0 : 0.0;
        out.noteStart = false;
        out.frequency = MIDDLE_C * std::pow(2, root + voltsPerOctave);
        auto formantProgression = 1 - std::pow(formantMorphCounter, 2);
        for (int i = 0; i < 2; ++i) {
            out.formants[i] = formantProgression * endFormants[i] + (1 - formantProgression) * startFormants[i];
            out.formants[i] *= formantScaling;
        }
        formantMorphCounter = std::max(0.f, formantMorphCounter - formantMorphStep);
    }

private:
    dsp::SchmittTrigger noteOnTrigger;
    const float MIDDLE_C = 261.626;

    const float formantMorphTime = 0.5;
    float formantMorphStep;
    float formantMorphCounter;

    FormantFetcher formantFetcher;
};


#endif //WILDERGARDEN_VCV_ENVELOPEHANDLER_H
