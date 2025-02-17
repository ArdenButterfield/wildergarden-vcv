//
// Created by arden on 2/13/25.
//

#ifndef WILDERGARDEN_VCV_FORMANTFETCHER_H
#define WILDERGARDEN_VCV_FORMANTFETCHER_H

#include <array>
#include <vector>
#include <cmath>
#include <iostream>

struct FormantInfo {
    float f1;
    float f2;
};

struct PlosiveInfo {
    float length;
    float vowelDelayLength;
    float gain;
    float loFreq;
    float hiFreq;
    bool voiced;
};

class FormantFetcher {
public:
    void fetchStartVowel(float position, std::array<float, 2>& startFormants) {
        position = std::min(std::max(0.f, position), 1.f);
        float scaledPositon = position * (formantInfo.size() - 1.f);
        int indexLow = std::floor(scaledPositon);
        int indexHigh = std::ceil(scaledPositon);
        float amountHigh = std::fmod(scaledPositon, 1.f);
        float amountLow = 1 - amountHigh;

        startFormants[0] = formantInfo[indexLow].f1 * amountLow + formantInfo[indexHigh].f1 * amountHigh;
        startFormants[1] = formantInfo[indexLow].f2 * amountLow + formantInfo[indexHigh].f2 * amountHigh;
    }

    void fetchEndVowel(float position, std::array<float, 2>& endFormants) {
        fetchStartVowel(position, endFormants); // This will be different if we have sounds that are only accessible
        // on one or the other
    }

    void fetchPlosive(float position, struct PlosiveInfo& plosive) {
        auto positionIndex = static_cast<int>(position * plosiveInfo.size());
        int upperBound = plosiveInfo.size() - 1;
        plosive = plosiveInfo[std::min(std::max(0, positionIndex), upperBound)];
    }

private:
    std::vector<PlosiveInfo> plosiveInfo {
            {0.06, 0.10, 10.0, 30, 70, false}, // p
            {0.02, 0.08, 1.0, 1100, 2000, false}, // k
            {0.04, 0.08, 0.8, 1100, 5500, false}, // ch
            {0.11, 0.12, 0.5, 7500, 10000, false}, // s
    };

    // obviously not all the vowels, but a selection of vowels that will morph smoothly
    const std::vector<FormantInfo> formantInfo {
        {240, 2400}, // i
        {235, 2100}, // y
        {390, 2300}, // e
        {610, 1900}, // ɛ
        {850, 1610}, // a
        {750, 940}, // ɑ
        {700, 760}, // ɒ
        {500, 700}, // ɔ
        {360, 640}, // o
        {250, 595}, // u
    };
};


#endif //WILDERGARDEN_VCV_FORMANTFETCHER_H
