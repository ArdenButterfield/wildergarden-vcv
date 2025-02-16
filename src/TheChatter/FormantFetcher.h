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

private:
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
