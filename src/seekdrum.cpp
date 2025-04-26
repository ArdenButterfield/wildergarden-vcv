#include "plugin.hpp"

#define NUM_SESSIONS 4
#define NUM_CHANNELS 8

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