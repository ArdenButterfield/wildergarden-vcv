//
// Created by arden on 2/13/25.
//

#ifndef WILDERGARDEN_VCV_EXTERNALINPUTPROCESSOR_H
#define WILDERGARDEN_VCV_EXTERNALINPUTPROCESSOR_H

#include "EnvelopeHandler.h"
#include <array>

class ExternalInputProcessor {
public:
    ExternalInputProcessor() {}
    ~ExternalInputProcessor() {}

    float process(EnvelopeHandler::EnvelopeSample envelopeSample, float sampleRate, float input) {
        for (int i = 0; i < 2; ++i) {
            formantFilters[i].setParameters(
                    dsp::BiquadFilter::BANDPASS,
                    envelopeSample.formants[i] / sampleRate,
                    2.5, 0);

        }

        return (formantFilters[0].process(input) + formantFilters[1].process(input)) * envelopeSample.amp;
    }

    float sampleRate;

    std::array<dsp::BiquadFilter, 2> formantFilters;
};


#endif //WILDERGARDEN_VCV_EXTERNALINPUTPROCESSOR_H
