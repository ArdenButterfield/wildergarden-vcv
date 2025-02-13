//
// Created by arden on 2/12/25.
//

#ifndef WILDERGARDEN_VCV_SAWTOOTHOSCILLATOR_H
#define WILDERGARDEN_VCV_SAWTOOTHOSCILLATOR_H
#include "plugin.hpp"
#include "Filter.h"

class SawtoothOscillator {
public:
    SawtoothOscillator() {
        period = 1. / 440.;
        counter = 0;
        filter.initialize(44100);
        filter.setParameters(18000, 0.71, Filter::filter_lopass);
    }

    void setFrequency(double freq) {
        period = 1. / freq;
    }

    double process(double timeIncrement) {
        counter += timeIncrement;
        while (counter > period) {
            counter -= period;
        }
        return filter.process((counter * 10 / period) - 5);
    }

private:
    double period;
    double counter;
    Filter filter;
};


#endif //WILDERGARDEN_VCV_SAWTOOTHOSCILLATOR_H
