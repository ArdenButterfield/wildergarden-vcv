//
// Created by arden on 5/15/25.
//

#ifndef WILDERGARDEN_VCV_SEEKDRUMTRACK_H
#define WILDERGARDEN_VCV_SEEKDRUMTRACK_H

#include <array>
#include <map>
#include <iterator>
#include "../plugin.hpp"
#include <iostream>

template <unsigned NUM_CHANNELS> struct Hit {
    Hit() {
        for (auto& v : velocity) { v = 0; }
    }
    float position;
    std::array<float, NUM_CHANNELS> velocity;

    void addNote(int channel) {
        velocity[channel] = 10.f;
    }

    bool hasNotes() {
        for (auto v : velocity) {
            if (v > 0) {
                return true;
            }
        }
        return false;
    }

    void mergeWith(const Hit& other) {
        for (auto i = 0; i < NUM_CHANNELS; ++i) {
            velocity[i] = std::max(velocity[i], other.velocity[i]);
        }
    }
};

template <unsigned NUM_CHANNELS> struct Track {
    enum MotionState {
        FORWARD_MOTION,
        BACKWARDS_MOTION,
        WRAPAROUND_MOTION,
        NO_MOTION,
        JUMPING_MOTION
    };

    const float MOTION_EPSILON = 1.0f;

    std::array<float, 8> overview;

    Track() : previousPosition(0), nextHit(hitDeck.end()) {
        for (auto i = 0; i < 8; ++i) { overview[i] = 0; }
    }
    std::map<float, Hit<NUM_CHANNELS>> hitDeck;
    std::array<dsp::SchmittTrigger, NUM_CHANNELS> gateTriggers;
    float previousPosition;
    typename std::map<float, Hit<NUM_CHANNELS>>::iterator nextHit;

    void buildOverview() {
        for (auto i = 0; i < 8; ++i) { overview[i] = 0; }
        for (auto& hit : hitDeck) {
            int frame = std::floor(hit.first * 0.8f);
            overview[frame] = 1.f;
        }
    }

    void clearAll() {
        hitDeck.clear();
        previousPosition = 0;
        nextHit = hitDeck.end();
        buildOverview();
    }

/*
    std::map<float, Hit>::iterator getNextHit(bool goingForwards) {
        if (previousHit == hitDeck.end()) {
            return previousHit;
        }
        std::map<float, Hit>::iterator nextHit;
        if (goingForwards) {
            nextHit = std::next(previousHit);
            if (nextHit == hitDeck.end()) {
                nextHit = hitDeck.begin();
            }
            return nextHit;
        } else {
            if (previousHit == hitDeck.begin()) {
                nextHit = std::prev(hitDeck.end());
            } else {
                nextHit = std::prev(previousHit);
            }
            return nextHit;
        }
    }
*/

    MotionState getMotion(float prev, float curr) {
        auto diff = std::abs(prev - curr);
        if ((diff < MOTION_EPSILON) && (prev > curr)) {
            return BACKWARDS_MOTION;
        } else if ((diff < MOTION_EPSILON) && (prev < curr)) {
            return FORWARD_MOTION;
        } else if (diff < MOTION_EPSILON) {
            return NO_MOTION;
        } else if (curr < MOTION_EPSILON) {
            return WRAPAROUND_MOTION;
        } else {
            return JUMPING_MOTION;
        }
    }


    void process(bool record, bool clear, float position,
                 std::array<float, NUM_CHANNELS>& inputs,
                 std::array<float, NUM_CHANNELS>& outputs) {
        for (auto& o : outputs) { o = 0; }

        if (hitDeck.begin() != hitDeck.end()) {
            auto motion = getMotion(previousPosition, position);

            if (motion == WRAPAROUND_MOTION) {
                nextHit = hitDeck.begin();
                motion = FORWARD_MOTION;
            }

            if (motion == JUMPING_MOTION) {
                nextHit = hitDeck.lower_bound(position);
            }

            if (motion == FORWARD_MOTION) {
                while ((nextHit != hitDeck.end()) && (nextHit->second.position <= position)) {
                    for (auto i = 0; i < NUM_CHANNELS; ++i) {
                        outputs[i] = std::max(outputs[i], nextHit->second.velocity[i]);
                    }
                    nextHit = std::next(nextHit);
                }
            }
            if (motion == BACKWARDS_MOTION && !(nextHit == hitDeck.end() && std::prev(nextHit)->second.position < position)) {
                while ((nextHit != hitDeck.begin() && nextHit->second.position >= previousPosition)) {
                    nextHit = std::prev(nextHit);
                }
                while (nextHit->second.position >= position) {
                    for (auto i = 0; i < NUM_CHANNELS; ++i) {
                        outputs[i] = std::max(outputs[i], nextHit->second.velocity[i]);
                    }
                    if (nextHit == hitDeck.begin()) {
                        break;
                    } else {
                        nextHit = std::prev(nextHit);
                    }
                }
            }
        }

        if (record) {
            auto hit = Hit<NUM_CHANNELS>();
            hit.position = position;
            bool notesAdded = false;
            for (auto channel = 0; channel < NUM_CHANNELS; ++channel) {
                auto hitInChannel = gateTriggers[channel].process(inputs[channel], 0.1f, 1.5f);
                if (hitInChannel) {
                    hit.addNote(channel);
                    notesAdded = true;
                }
            }
            if (notesAdded) {
                auto hitAlready = hitDeck.find(position);
                if (hitAlready != hitDeck.end()) {
                    hit.mergeWith(hitAlready->second);
                }
                std::cout << "note added ";
                for (auto& i : hit.velocity) {
                    std::cout << i << " ";
                }
                std::cout << "\n";
                hitDeck[position] = hit;
            buildOverview();
            }
        }
    }
};


#endif //WILDERGARDEN_VCV_SEEKDRUMTRACK_H
