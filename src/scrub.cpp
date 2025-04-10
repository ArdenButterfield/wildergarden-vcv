#include "plugin.hpp"


struct Scrub : Module {
	enum ParamId {
		LENGTH_PARAM,
		SUBDIVISIONS_PARAM,
		QUANTIZE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		LENGTH_CV_INPUT,
		SUBDIVISIONS_CV_INPUT,
		QUANTIZE_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		STEP_OUTPUT,
		TRIG_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		CLOCK_LIGHT,
		LENGTH_LIGHT,
		SUBDIVISIONS_LIGHT,
		OUT_LIGHT,
		LIGHTS_LEN
	};

	Scrub() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(LENGTH_PARAM, 0.f, 1.f, 0.f, "");
		configParam(SUBDIVISIONS_PARAM, 0.f, 1.f, 0.f, "");
		configParam(QUANTIZE_PARAM, 0.f, 1.f, 0.f, "");
		configInput(CLOCK_INPUT, "");
		configInput(LENGTH_CV_INPUT, "");
		configInput(SUBDIVISIONS_CV_INPUT, "");
		configInput(QUANTIZE_CV_INPUT, "");
		configOutput(STEP_OUTPUT, "");
		configOutput(TRIG_OUTPUT, "");
	}

	void process(const ProcessArgs& args) override {
	}
};


struct ScrubWidget : ModuleWidget {
	ScrubWidget(Scrub* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Scrub.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.077, 45.203)), module, Scrub::LENGTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.077, 67.66)), module, Scrub::SUBDIVISIONS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.077, 88.285)), module, Scrub::QUANTIZE_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(17.78, 24.459)), module, Scrub::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.483, 45.203)), module, Scrub::LENGTH_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.483, 67.66)), module, Scrub::SUBDIVISIONS_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(25.483, 88.285)), module, Scrub::QUANTIZE_CV_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(10.049, 111.319)), module, Scrub::STEP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(25.511, 111.319)), module, Scrub::TRIG_OUTPUT));

		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(17.78, 18.375)), module, Scrub::CLOCK_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(17.78, 39.881)), module, Scrub::LENGTH_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(17.78, 62.22)), module, Scrub::SUBDIVISIONS_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(17.78, 105.78)), module, Scrub::OUT_LIGHT));
	}
};


Model* modelScrub = createModel<Scrub, ScrubWidget>("Scrub");