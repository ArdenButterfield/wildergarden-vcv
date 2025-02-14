//
// Created by arden on 2/13/25.
//

#ifndef WILDERGARDEN_VCV_EXTERNALINPUTPROCESSOR_H
#define WILDERGARDEN_VCV_EXTERNALINPUTPROCESSOR_H

#include "EnvelopeHandler.h"

class ExternalInputProcessor {
public:
    ExternalInputProcessor() {}
    ~ExternalInputProcessor() {}

    void setFormants(float f1_initial, float f2_initial, float f1_final, float f2_final) {
        f1[0] = f1_initial;
        f1[1] = f1_final;
        f2[0] = f2_initial;
        f2[1] = f2_final;
    }

    void setFrequency(float frequency, float _sampleRate) {
        sampleRate = _sampleRate;
    }

    float process(EnvelopeHandler::EnvelopeSample envelopeSample, float input) {
        formantFilters[0].setParameters(
                dsp::BiquadFilter::BANDPASS,
                (f1[0] * (1 - envelopeSample.formantProgression) + f1[1] * envelopeSample.formantProgression) / sampleRate,
                2.5, 0);
        formantFilters[1].setParameters(
                dsp::BiquadFilter::BANDPASS,
                (f2[0] * (1 - envelopeSample.formantProgression) + f2[1] * envelopeSample.formantProgression) / sampleRate,
                2.5, 0);

        return (formantFilters[0].process(input) + formantFilters[1].process(input)) * envelopeSample.amp;
    }

    std::array<float, 2> f1;
    std::array<float, 2> f2;

    float sampleRate;

    std::array<dsp::BiquadFilter, 2> formantFilters;
};


#endif //WILDERGARDEN_VCV_EXTERNALINPUTPROCESSOR_H
