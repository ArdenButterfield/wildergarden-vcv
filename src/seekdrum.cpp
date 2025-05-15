#include "plugin.hpp"
#include <vector>
#include <map>
#include <array>
#include <cmath>
#include <iterator>
#include <iostream>

#define NUM_SESSIONS 4
#define NUM_CHANNELS 8

struct Hit {
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

struct Track {
    enum MotionState {
        FORWARD_MOTION,
        BACKWARDS_MOTION,
        WRAPAROUND_MOTION,
        NO_MOTION,
        JUMPING_MOTION
    };

    const float MOTION_EPSILON = 1.0f;

    Track() : previousPosition(0), nextHit(hitDeck.end()) {

    }
    std::map<float, Hit> hitDeck;
    std::array<dsp::SchmittTrigger, NUM_CHANNELS> gateTriggers;
    float previousPosition;
    std::map<float, Hit>::iterator nextHit;

    void clearAll() {
        hitDeck.clear();
        previousPosition = 0;
        nextHit = hitDeck.end();
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
            auto hit = Hit();
            hit.position = position;
            bool notesAdded;
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
                hitDeck[position] = hit;
            }
        }
    }
};

struct SeekDrum : Module {
    enum ParamId {
        BIPOLAR_UNIPOLAR_PARAM,
        RECORD_PARAM,
        CLEAR_MODE_PARAM,
        SELECT_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        PREV_SESSION_INPUT,
        NEXT_SESSION_INPUT,
        POSITION_INPUT,
        RECORD_INPUT,
        CLEAR_INPUT,
        SELECT_CV_INPUT,
        ENUMS(CHANNEL_TRIGGER_INPUT, NUM_CHANNELS),
        INPUTS_LEN
    };
    enum OutputId {
        ENUMS(CHANNEL_TRIGGER_OUTPUT, NUM_CHANNELS),
        OUTPUTS_LEN
    };
    enum LightId {
        ENUMS(INPUT_TRIGGER_INDICATOR, NUM_CHANNELS),
        ENUMS(OUTPUT_TRIGGER_INDICATOR, NUM_CHANNELS),
        ENUMS(SESSION_INDICATOR, NUM_SESSIONS),
        ENUMS(VISUALIZER, NUM_SESSIONS * 8 * 3),
        LIGHTS_LEN
    };

    Track track;
    std::array<float, NUM_CHANNELS> track_inputs{0.f};
    std::array<float, NUM_CHANNELS> track_outputs{0.f};

    dsp::SchmittTrigger recordTrigger;
    dsp::SchmittTrigger clearTrigger;

    SeekDrum() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(BIPOLAR_UNIPOLAR_PARAM, 0.f, 1.f, 0.f, "");
        configParam(RECORD_PARAM, 0.f, 1.f, 0.f, "");
        configParam(CLEAR_MODE_PARAM, 0.f, 1.f, 0.f, "");
        configParam(SELECT_PARAM, 0.f, 1.f, 0.f, "");
        configInput(PREV_SESSION_INPUT, "");
        configInput(NEXT_SESSION_INPUT, "");
        configInput(POSITION_INPUT, "");
        configInput(RECORD_INPUT, "");
        configInput(CLEAR_INPUT, "");
        configInput(SELECT_CV_INPUT, "");
        for (auto i = 0; i < NUM_CHANNELS; ++i) {
            configInput(CHANNEL_TRIGGER_INPUT + i, "");
            configOutput(CHANNEL_TRIGGER_OUTPUT + i, "");
        }
    }

    void process(const ProcessArgs& args) override {
        for (int channel = 0; channel < NUM_CHANNELS; ++channel) {
            auto in = inputs[CHANNEL_TRIGGER_INPUT + channel].getVoltage();
            lights[INPUT_TRIGGER_INDICATOR + channel].setBrightnessSmooth(std::max(0.f, in) * 0.1, args.sampleTime);
            track_inputs[channel] = in;
        }

        recordTrigger.process(inputs[RECORD_INPUT].getVoltage() + (params[RECORD_PARAM].getValue() ? 10 : 0), 0.1f, 1.5f);
        bool clearGoingHigh = clearTrigger.process(inputs[CLEAR_INPUT].getVoltage(), 0.1f, 1.5f);

        bool clearTriggerMode = (params[CLEAR_MODE_PARAM].getValue() > 0.5f);
        if (clearTriggerMode && clearGoingHigh) {
            track.clearAll();
        }

        auto position = inputs[POSITION_INPUT].getVoltage();
        if (params[BIPOLAR_UNIPOLAR_PARAM].getValue() < 0.5f) {
            position += 5.f;
        }

        track.process(recordTrigger.isHigh(), clearTrigger.isHigh() && !clearTriggerMode, position, track_inputs, track_outputs);

        for (auto i = 0; i < NUM_CHANNELS; ++i) {
            outputs[CHANNEL_TRIGGER_OUTPUT + i].setVoltage(track_outputs[i]);
            lights[OUTPUT_TRIGGER_INDICATOR + i].setBrightnessSmooth(track_outputs[i] * 0.1f, args.sampleTime);
        }
        /*process(bool record, bool clear, float position,
                std::array<float, NUM_CHANNELS>& inputs,
                std::array<float, NUM_CHANNELS>& outputs)*/
    }
};


struct SeekDrumWidget : ModuleWidget {
	SeekDrumWidget(SeekDrum* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SeekDrum.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<CKSS>(mm2px(Vec(33.772, 48.143)), module, SeekDrum::BIPOLAR_UNIPOLAR_PARAM));
		addParam(createParamCentered<CKD6>(mm2px(Vec(37.886, 67.392)), module, SeekDrum::RECORD_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(32.227, 89.68)), module, SeekDrum::CLEAR_MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(36.47, 108.66)), module, SeekDrum::SELECT_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(13.664, 23.851)), module, SeekDrum::PREV_SESSION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(13.664, 32.497)), module, SeekDrum::NEXT_SESSION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.232, 47.537)), module, SeekDrum::POSITION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.232, 66.683)), module, SeekDrum::RECORD_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.232, 87.507)), module, SeekDrum::CLEAR_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.232, 108.593)), module, SeekDrum::SELECT_CV_INPUT));

        for (auto i = 0; i < NUM_SESSIONS; ++i) {
            addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(19.998, 28.508 + (i * 4.f) - 4.f * 1.5f)), module, SeekDrum::SESSION_INDICATOR + i));
        }

        {
            // grid visualizer
            const float centerX = 23.397 + 31.334 / 2;
            const float centerY = 19.168 + 18.68 / 2;
            for (auto row = 0; row < NUM_SESSIONS; ++row) {
                for (auto i = 0; i < 8; ++i) {
                    auto x = centerX + (i - 3.5f) * 4.f;
                    auto y = centerY + (row - 1.5f) * 4.f;
                    addChild(createLightCentered<MediumLight<RedGreenBlueLight>>
                        (mm2px(Vec(x, y)), module, SeekDrum::VISUALIZER + row * 8 * 3 + i * 3));
                }
            }
        }

        {
            // inputs and outputs
            auto inputCenterX = 7.8f;
            auto outputCenterX = 52.654f;
            auto centerY = 80.271f;
            auto lightOffset = 4.5;
            for (auto i = 0; i < NUM_CHANNELS; ++i) {
                auto y = centerY + (i - (NUM_CHANNELS - 1.f) * 0.5f) * 9.7f;
                addChild(createInputCentered<PJ301MPort>(mm2px(Vec(inputCenterX, y)), module, SeekDrum::CHANNEL_TRIGGER_INPUT + i));
                addChild(createOutputCentered<PJ301MPort>(mm2px(Vec(outputCenterX, y)), module, SeekDrum::CHANNEL_TRIGGER_OUTPUT + i));
                addChild(createLightCentered<MediumLight<RedLight>>
                    (mm2px(Vec(inputCenterX - lightOffset, y + lightOffset)), module, SeekDrum::INPUT_TRIGGER_INDICATOR + i));
                addChild(createLightCentered<MediumLight<RedLight>>
                    (mm2px(Vec(outputCenterX + lightOffset, y + lightOffset)), module, SeekDrum::OUTPUT_TRIGGER_INDICATOR + i));
            }
        }
	}
};


Model* modelSeekDrum = createModel<SeekDrum, SeekDrumWidget>("SeekDrum");