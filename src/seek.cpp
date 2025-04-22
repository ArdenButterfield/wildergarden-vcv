#include "plugin.hpp"
#include <vector>
#include <map>
#include <array>
#include <cmath>
#include <iterator>
#include <iostream>

#define NUM_SESSIONS 4

struct Note {
    float start;
    float end;
    float pitch;
    float velocity;
};

struct Track {
    Track() : noteIsInHand(false), previousPosition(0) {
    }
    std::map<float, Note> noteDeck;
    dsp::BooleanTrigger recordTrigger;
    dsp::SchmittTrigger gateTrigger;
    bool wasRecording;
    Note noteInHand;
    bool noteIsInHand;
    float previousPosition;

    void clearAll() {
        noteIsInHand = false;
        noteDeck.clear();
    }

    bool hasNotesInRange(float low, float high) {
        auto note = noteDeck.upper_bound(low);
        return (note != noteDeck.end() && note->first < high);
    }

    void eraseNotesInRange(float start, float end) {
        if (start <= end) {
            for (auto it = noteDeck.lower_bound(start);
                 it != noteDeck.end() && it->first < end;
                 it = noteDeck.erase(it)) { }
        } else {
            for (auto it = noteDeck.lower_bound(start);
                it != noteDeck.end();
                it = noteDeck.erase(it)) {}
            for (auto it = noteDeck.begin();
                it != noteDeck.end() && it->first < end;
                it = noteDeck.erase(it)) {}
        }
    }

    void getNotesAtCurrentPostion(float position, bool clear, float& pitchOut, float& gateOut) {
        auto upper = noteDeck.upper_bound(position);
        gateOut = 0;
        pitchOut = 0;
        if (upper == noteDeck.begin()) {
            // no notes
            return;
        }
        auto current = std::prev(upper);
        if (current->second.end < position) {
            // note has already ended
            return;
        }
        if (clear) {
            if (position >= previousPosition) {
                if ((current->first >= previousPosition) && (current->first <= position)) {
                    noteDeck.erase(current);
                } else {
                    current->second.end = previousPosition;
                }
            } else if (position + 0.5f < previousPosition) {
                // we're not scrubbing backwards-- we're overflowing and looping back
                if ((current->first >= previousPosition) || (current->first <= position)) {
                    noteDeck.erase(current);
                } else {
                    current->second.end = previousPosition;
                }
            } else {
                if ((current->first <= previousPosition) && (current->first >= position)) {
                    noteDeck.erase(current);
                } else {
                    current->second.end = position;
                }
            }
            return;
        }
        pitchOut = current->second.pitch;
        gateOut = current->second.velocity;
    }

    void process(bool record, bool clear, float position,
                 float pitch, float gate,
                 float& pitchOut, float& gateOut) {
        if (record && !wasRecording) {
            noteIsInHand = false;
            gateTrigger.reset();
        }

        getNotesAtCurrentPostion(position, clear, pitchOut, gateOut);

        if (record) {
            if (noteIsInHand) {
                noteInHand.end = position;
                noteInHand.velocity = std::max(gate, noteInHand.velocity);
            }
            bool gateOpening = gateTrigger.process(gate, 0.1f, 1.f);
            if (gateOpening) {
                noteInHand.start = position;
                noteInHand.end = position;
                noteInHand.pitch = pitch;
                noteInHand.velocity = gate;
                noteIsInHand = true;
            } else if (noteIsInHand && !gateTrigger.isHigh()) {
                eraseNotesInRange(noteInHand.start, noteInHand.end);
                std::cout << "note added " << noteInHand.start << " " << noteInHand.end << " " << noteInHand.pitch << "\n";
                noteDeck[noteInHand.start] = noteInHand;
                noteIsInHand = false;
            } else if (noteIsInHand && std::abs(pitch - noteInHand.pitch) > 0.01) {
                eraseNotesInRange(noteInHand.start, noteInHand.end);
                std::cout << "note added " << noteInHand.start << " " << noteInHand.end << " " << noteInHand.pitch << "\n";
                noteDeck[noteInHand.start] = noteInHand;
                noteInHand.start = position;
                noteInHand.end = position;
                noteInHand.pitch = pitch;
                noteInHand.velocity = gate;
            }
        }
        wasRecording = record;

        if (gate > 0.1f) {
            pitchOut = pitch;
            gateOut = gate;
        }
        previousPosition = position;
    }
};

struct Seek : Module {
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
		PITCH_INPUT,
		POSITION_INPUT,
		RECORD_INPUT,
		GATE_INPUT,
		CLEAR_INPUT,
		SELECT_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		PITCH_OUTPUT,
		GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		INPUT_LIGHT,
		POSITION_LIGHT,
		RECORD_LIGHT,
		CLEAR_LIGHT,
		OUTPUT_LIGHT,
        ENUMS(SESSION_INDICATOR, NUM_SESSIONS),
        ENUMS(VISUALIZER, NUM_SESSIONS * 8 * 3),
		LIGHTS_LEN
	};

    dsp::SchmittTrigger recordTrigger, trackUpTrigger, trackDownTrigger, clearTrigger;
    std::array<Track, NUM_SESSIONS> tracks;
    int currentTrack;

	Seek() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(BIPOLAR_UNIPOLAR_PARAM, 0.f, 1.f, 0.f, "");
		configParam(RECORD_PARAM, 0.f, 1.f, 0.f, "");
		configParam(CLEAR_MODE_PARAM, 0.f, 1.f, 0.f, "");
		configParam(SELECT_PARAM, 0.f, 1.f, 0.f, "");
		configInput(PREV_SESSION_INPUT, "");
		configInput(NEXT_SESSION_INPUT, "");
		configInput(PITCH_INPUT, "");
		configInput(POSITION_INPUT, "");
		configInput(RECORD_INPUT, "");
		configInput(GATE_INPUT, "");
		configInput(CLEAR_INPUT, "");
		configInput(SELECT_CV_INPUT, "");
		configOutput(PITCH_OUTPUT, "");
		configOutput(GATE_OUTPUT, "");
        currentTrack = 0;
	}

	void process(const ProcessArgs& args) override {
        bool isBipolar = params[BIPOLAR_UNIPOLAR_PARAM].getValue() < 0.5f;
        auto position = inputs[POSITION_INPUT].getVoltage() + (isBipolar ? 5.f : 0.f);
        lights[POSITION_LIGHT].setBrightnessSmooth(position * 0.1f, args.sampleTime);

        recordTrigger.process(inputs[RECORD_INPUT].getVoltage(), 0.1f, 1.f);
        bool isRecording = recordTrigger.isHigh() || (params[RECORD_PARAM].getValue() > 0.5f);
        lights[RECORD_LIGHT].setBrightnessSmooth(isRecording ? 1.f : 0.f, args.sampleTime);

        if (trackUpTrigger.process(inputs[PREV_SESSION_INPUT].getVoltage(), 0.1f, 1.f)) {
            currentTrack = (currentTrack + 3) % NUM_SESSIONS;
        }
        if (trackDownTrigger.process(inputs[NEXT_SESSION_INPUT].getVoltage(), 0.1f, 1.f)) {
            currentTrack = (currentTrack + 1) % NUM_SESSIONS;
        }

        bool clearIsTrigger = params[CLEAR_MODE_PARAM].getValue() < 0.5f;
        if (clearIsTrigger && clearTrigger.process(inputs[CLEAR_INPUT].getVoltage())) {
            for (auto& track : tracks) {
                track.clearAll();
            }
        }

        float pitchOut, gateOut;
        tracks[currentTrack].process(isRecording, (!clearIsTrigger && inputs[CLEAR_INPUT].getVoltage()), position,
                                     inputs[PITCH_INPUT].getVoltage(), inputs[GATE_INPUT].getVoltage(),
                                     pitchOut, gateOut);

        outputs[PITCH_OUTPUT].setVoltage(pitchOut);
        outputs[GATE_OUTPUT].setVoltage(gateOut);

        for (auto i = 0; i < NUM_SESSIONS; ++i) {
            lights[SESSION_INDICATOR + i].setBrightnessSmooth((i == currentTrack) ? 1.f : 0.f, args.sampleTime);
        }

        for (auto session = 0; session < NUM_SESSIONS; ++session) {
            for (auto step = 0; step < 8; ++step) {
                if ((session == currentTrack) && (step == std::floor(position * 8.f / 10.f))) {
                    lights[VISUALIZER + session * 8 * 3 + step * 3].setBrightnessSmooth(1.f, args.sampleTime);
                    lights[VISUALIZER + session * 8 * 3 + step * 3 + 1].setBrightnessSmooth(1.f, args.sampleTime);
                    lights[VISUALIZER + session * 8 * 3 + step * 3 + 2].setBrightnessSmooth(1.f, args.sampleTime);
                } else if (tracks[session].hasNotesInRange(std::floor(step * 8.f / 10.f) * 10.f / 8.f,
                                                                (std::floor(step * 8.f / 10.f) + 1) * 10.f / 8.f)) {
                    lights[VISUALIZER + session * 8 * 3 + step * 3].setBrightnessSmooth(0.f, args.sampleTime);
                    lights[VISUALIZER + session * 8 * 3 + step * 3 + 1].setBrightnessSmooth(0.f, args.sampleTime);
                    lights[VISUALIZER + session * 8 * 3 + step * 3 + 2].setBrightnessSmooth(1.f, args.sampleTime);

                } else {
                    lights[VISUALIZER + session * 8 * 3 + step * 3].setBrightnessSmooth(0.f, args.sampleTime);
                    lights[VISUALIZER + session * 8 * 3 + step * 3 + 1].setBrightnessSmooth(0.f, args.sampleTime);
                    lights[VISUALIZER + session * 8 * 3 + step * 3 + 2].setBrightnessSmooth(0.f, args.sampleTime);
                }
            }
        }
	}
};


struct SeekWidget : ModuleWidget {
	SeekWidget(Seek* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Seek.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<CKSS>(mm2px(Vec(26.546, 69.695)), module, Seek::BIPOLAR_UNIPOLAR_PARAM));
		addParam(createParamCentered<CKD6>(mm2px(Vec(49.415, 69.517)), module, Seek::RECORD_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(6.421, 108.993)), module, Seek::CLEAR_MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(30.178, 108.993)), module, Seek::SELECT_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(13.664, 23.851)), module, Seek::PREV_SESSION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(13.664, 32.497)), module, Seek::NEXT_SESSION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.039, 52.396)), module, Seek::PITCH_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.227, 52.396)), module, Seek::POSITION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(49.501, 52.396)), module, Seek::RECORD_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.198, 70.027)), module, Seek::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.039, 91.803)), module, Seek::CLEAR_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.227, 91.803)), module, Seek::SELECT_CV_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(49.529, 90.419)), module, Seek::PITCH_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(49.415, 109.543)), module, Seek::GATE_OUTPUT));

		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(16.366, 47.268)), module, Seek::INPUT_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(35.376, 47.268)), module, Seek::POSITION_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(55.021, 47.268)), module, Seek::RECORD_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(16.366, 87.203)), module, Seek::CLEAR_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(55.021, 86.874)), module, Seek::OUTPUT_LIGHT));


        for (auto i = 0; i < NUM_SESSIONS; ++i) {
            addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(19.998, 28.508 + (i * 4.f) - 4.f * 1.5f)), module, Seek::SESSION_INDICATOR + i));
        }

        const float centerX = 23.397 + 31.334 / 2;
        const float centerY = 19.168 + 18.68 / 2;
        for (auto row = 0; row < NUM_SESSIONS; ++row) {
            for (auto i = 0; i < 8; ++i) {
                auto x = centerX + (i - 3.5f) * 4.f;
                auto y = centerY + (row - 1.5f) * 4.f;
                addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(mm2px(Vec(x, y)), module, Seek::VISUALIZER + row * 8 * 3 + i * 3));
            }
        }


        // session visualizer
        addChild(createWidget<Widget>(mm2px(Vec(23.397, 19.168))));
	}
};


Model* modelSeek = createModel<Seek, SeekWidget>("Seek");