#include "plugin.hpp"
#include "WildergardensWidgets.h"
#include "SawtoothOscillator.h"

#include <cmath>
#include <algorithm>
#include "OscillatorBank.h"

#define MIDDLE_C 261.626

struct TheChatter : Module {
	enum ParamId {
		PLOSIVE_PARAM,
		PLOSIVE_MODULATION_PARAM,
		VOWEL_START_PARAM,
		VOWEL_START_MODULATION_PARAM,
		VOWEL_END_PARAM,
		VOWEL_END_MODULATION_PARAM,
		MODE_PARAM,
		ROOT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		PLOSIVE_CV_INPUT,
		VOWEL_START_CV_INPUT,
		VOWEL_END_CV_INPUT,
		GATE_INPUT,
		FREQUENCY_INPUT,
		EXTERNAL_INPUT,
		MODE_MODULATION_INPUT,
		ROOT_MODULATION_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		VOWEL_ONLY_OUTPUT,
		PLOSIVE_ONLY_OUTPUT,
		MIX_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		PLOSIVE_ACTIVE_LIGHT,
		VOWEL_START_ACTIVE_LIGHT,
		VOWEL_END_ACTIVE_LIGHT,
		LIGHTS_LEN
	};

    OscillatorBank oscillatorBank;

	TheChatter() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(PLOSIVE_PARAM, 0.f, 1.f, 0.f, "Plosive");
		configParam(PLOSIVE_MODULATION_PARAM, -1.f, 1.f, 0.f, "Plosive modulation");
		configParam(VOWEL_START_PARAM, 0.f, 1.f, 0.f, "Vowel start");
		configParam(VOWEL_START_MODULATION_PARAM, -1.f, 1.f, 0.f, "Vowel start modulation");
		configParam(VOWEL_END_PARAM, 0.f, 1.f, 0.f, "Vowel end");
		configParam(VOWEL_END_MODULATION_PARAM, -1.f, 1.f, 0.f, "Vowel end modulation");
		configParam(MODE_PARAM, 0.f, 1.f, 0.f, "Speak/Sung mode");
		configParam(ROOT_PARAM, -5.f, 5.f, 0.f, "Root (V/oct)");
		configInput(PLOSIVE_CV_INPUT, "");
		configInput(VOWEL_START_CV_INPUT, "");
		configInput(VOWEL_END_CV_INPUT, "");
		configInput(GATE_INPUT, "");
		configInput(FREQUENCY_INPUT, "");
		configInput(EXTERNAL_INPUT, "");
		configInput(MODE_MODULATION_INPUT, "");
		configInput(ROOT_MODULATION_INPUT, "");
		configOutput(VOWEL_ONLY_OUTPUT, "");
		configOutput(PLOSIVE_ONLY_OUTPUT, "");
		configOutput(MIX_OUTPUT, "");
	}

	void process(const ProcessArgs& args) override {
        auto root = std::min(std::max(-5.f, params[ROOT_PARAM].getValue() + inputs[ROOT_MODULATION_INPUT].getVoltage()), 5.f);
        auto pitch = root + inputs[FREQUENCY_INPUT].getVoltage();
        oscillatorBank.setFrequency(MIDDLE_C * std::pow(2, pitch), args.sampleRate);
        outputs[VOWEL_ONLY_OUTPUT].setVoltage(oscillatorBank.tick());
	}
};


struct TheChatterWidget : ModuleWidget {
	TheChatterWidget(TheChatter* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/TheChatter.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<FourmsSliderHorizontal>(mm2px(Vec(25.28, 17.74)), module, TheChatter::PLOSIVE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(45.313, 23.107)), module, TheChatter::PLOSIVE_MODULATION_PARAM));
		addParam(createParamCentered<FourmsSliderHorizontal>(mm2px(Vec(25.43, 34.397)), module, TheChatter::VOWEL_START_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(45.313, 40.47)), module, TheChatter::VOWEL_START_MODULATION_PARAM));
		addParam(createParamCentered<FourmsSliderHorizontal>(mm2px(Vec(25.4, 50.224)), module, TheChatter::VOWEL_END_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(45.313, 55.908)), module, TheChatter::VOWEL_END_MODULATION_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.89, 87.157)), module, TheChatter::MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.832, 113.324)), module, TheChatter::ROOT_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(38.83, 23.107)), module, TheChatter::PLOSIVE_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(38.83, 40.47)), module, TheChatter::VOWEL_START_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(38.83, 55.908)), module, TheChatter::VOWEL_END_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.06, 65.375)), module, TheChatter::GATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.344, 65.375)), module, TheChatter::FREQUENCY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(38.449, 65.375)), module, TheChatter::EXTERNAL_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(23.901, 93.36)), module, TheChatter::MODE_MODULATION_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(23.554, 117.253)), module, TheChatter::ROOT_MODULATION_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(40.0, 82.129)), module, TheChatter::VOWEL_ONLY_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(39.861, 96.325)), module, TheChatter::PLOSIVE_ONLY_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(39.861, 110.521)), module, TheChatter::MIX_OUTPUT));

		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(32.348, 23.107)), module, TheChatter::PLOSIVE_ACTIVE_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(32.348, 40.47)), module, TheChatter::VOWEL_START_ACTIVE_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(32.348, 55.908)), module, TheChatter::VOWEL_END_ACTIVE_LIGHT));
	}
};


Model* modelTheChatter = createModel<TheChatter, TheChatterWidget>("TheChatter");