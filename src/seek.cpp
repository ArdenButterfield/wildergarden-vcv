#include "plugin.hpp"


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
		LIGHTS_LEN
	};

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
	}

	void process(const ProcessArgs& args) override {
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

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(26.546, 69.695)), module, Seek::BIPOLAR_UNIPOLAR_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(49.415, 69.517)), module, Seek::RECORD_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(6.421, 108.993)), module, Seek::CLEAR_MODE_PARAM));
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


        // session indicator
		addChild(createWidget<Widget>(mm2px(Vec(18.509, 19.168))));

        // session visualizer
        addChild(createWidget<Widget>(mm2px(Vec(23.397, 19.168))));
	}
};


Model* modelSeek = createModel<Seek, SeekWidget>("Seek");