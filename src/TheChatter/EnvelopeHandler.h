//
// Created by arden on 2/13/25.
//

#ifndef WILDERGARDEN_VCV_ENVELOPEHANDLER_H
#define WILDERGARDEN_VCV_ENVELOPEHANDLER_H

#include "../plugin.hpp"

class EnvelopeHandler {
public:
    struct EnvelopeSample {
        float formantProgression;
        float amp;
        float frequency;
        bool noteStart;
    };

    EnvelopeHandler() {}
    ~EnvelopeHandler() {}

    void process(float sampleRate, float gate, float root, float voltsPerOctave, EnvelopeSample& out) {
        if (noteOnTrigger.process(gate, 0.1f, 1.f)) {
            formantMorphStep = 1.f / (formantMorphTime * sampleRate);
            formantMorphCounter = 1.f;
        }

        out.amp = gate > 0 ? 1.0 : 0.0;
        out.noteStart = false;
        out.frequency = MIDDLE_C * std::pow(2, root + voltsPerOctave);
        out.formantProgression = 1 - std::pow(formantMorphCounter, 2);
        formantMorphCounter = std::max(0.f, formantMorphCounter - formantMorphStep);
    }

private:
    dsp::SchmittTrigger noteOnTrigger;
    const float MIDDLE_C = 261.626;

    const float formantMorphTime = 0.5;
    float formantMorphStep;
    float formantMorphCounter;
};


#endif //WILDERGARDEN_VCV_ENVELOPEHANDLER_H
