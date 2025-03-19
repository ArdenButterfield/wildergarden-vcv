#include "plugin.hpp"


struct Modmix : Module {
	enum ParamId {
		IN_4_PARAM,
		LOW_PARAM,
		HIGH_PARAM,
		LOW_TRIM_PARAM,
		HIGH_TRIM_PARAM,
		SAW_SQUARE_SELECTOR_PARAM,
		NORM_LEVEL_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_1_INPUT,
		IN_2_INPUT,
		IN_3_INPUT,
		PATH1_4_3_INPUT,
		HIGH_CV_INPUT,
		NORM_LEVEL_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		NORM_OUT_OUTPUT,
		OUT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		NORM_INDICATOR_LIGHT,
		OUT_INDICATOR_LIGHT,
		LIGHTS_LEN
	};

	Modmix() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(IN_4_PARAM, 0.f, 1.f, 0.f, "");
		configParam(LOW_PARAM, 0.f, 1.f, 0.f, "");
		configParam(HIGH_PARAM, 0.f, 1.f, 0.f, "");
		configParam(LOW_TRIM_PARAM, 0.f, 1.f, 0.f, "");
		configParam(HIGH_TRIM_PARAM, 0.f, 1.f, 0.f, "");
		configParam(SAW_SQUARE_SELECTOR_PARAM, 0.f, 1.f, 0.f, "");
		configParam(NORM_LEVEL_PARAM, 0.f, 1.f, 0.f, "");
		configInput(IN_1_INPUT, "");
		configInput(IN_2_INPUT, "");
		configInput(IN_3_INPUT, "");
		configInput(PATH1_4_3_INPUT, "");
		configInput(HIGH_CV_INPUT, "");
		configInput(NORM_LEVEL_INPUT, "");
		configOutput(NORM_OUT_OUTPUT, "");
		configOutput(OUT_OUTPUT, "");
	}

	void process(const ProcessArgs& args) override {
	}
};


struct ModmixWidget : ModuleWidget {
	ModmixWidget(Modmix* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/modmix.svg")));

		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(17.969, 36.257)), module, Modmix::IN_4_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(7.465, 56.518)), module, Modmix::LOW_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(17.974, 56.518)), module, Modmix::HIGH_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(7.465, 65.974)), module, Modmix::LOW_TRIM_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(17.974, 65.974)), module, Modmix::HIGH_TRIM_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(16.7, 84.0)), module, Modmix::SAW_SQUARE_SELECTOR_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(7.445, 99.814)), module, Modmix::NORM_LEVEL_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.469, 27.318)), module, Modmix::IN_1_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(17.969, 27.318)), module, Modmix::IN_2_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.469, 36.257)), module, Modmix::IN_3_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.465, 74.372)), module, Modmix::PATH1_4_3_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(17.974, 74.372)), module, Modmix::HIGH_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(17.955, 99.814)), module, Modmix::NORM_LEVEL_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.465, 111.948)), module, Modmix::NORM_OUT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(17.974, 111.948)), module, Modmix::OUT_OUTPUT));

		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(12.7, 102.873)), module, Modmix::NORM_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(20.61, 119.375)), module, Modmix::OUT_INDICATOR_LIGHT));
	}
};


Model* modelModmix = createModel<Modmix, ModmixWidget>("modmix");