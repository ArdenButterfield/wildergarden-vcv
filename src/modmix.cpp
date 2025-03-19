#include "plugin.hpp"
#include <array>
#include <cmath>

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
		LOW_CV_INPUT,
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
		ENUMS(NORM_INDICATOR_LIGHT, 3),
		ENUMS(OUT_INDICATOR_LIGHT, 3),
		LIGHTS_LEN
	};

    std::array<float, 4> channelVoltages;

	Modmix() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(IN_4_PARAM, -10.f, 10.f, 0.f, "Input Offset");
		configParam(LOW_PARAM, -10.f, 10.f, -5.f, "Low Mod Cutoff");
		configParam(HIGH_PARAM, -10.f, 10.f, 5.f, "High Mod Cutoff");
		configParam(LOW_TRIM_PARAM, -1.f, 1.f, 0.f, "Low CV Trim");
		configParam(HIGH_TRIM_PARAM, -1.f, 1.f, 0.f, "High CV Trim");
		configParam(SAW_SQUARE_SELECTOR_PARAM, 0.f, 1.f, 0.f, "Saw/Square Waveshape selector");
		configParam(NORM_LEVEL_PARAM, -10.f, 10.f, 0.f, "Normalized Output Level");

        configInput(IN_1_INPUT, "Mix Channel 1");
		configInput(IN_2_INPUT, "Mix Channel 2");
		configInput(IN_3_INPUT, "Mix Channel 3");
		configInput(LOW_CV_INPUT, "Low CV");
		configInput(HIGH_CV_INPUT, "High CV");
		configInput(NORM_LEVEL_INPUT, "Normalized Output Level CV");

        configOutput(NORM_OUT_OUTPUT, "Normalized Mix");
		configOutput(OUT_OUTPUT, "Mix");
	}

    float tame(const float voltage) {
        return std::min(std::max(-10.f, std::isfinite(voltage) ? voltage : 0.f), 10.f);
    }

	void process(const ProcessArgs& args) override {
        channelVoltages[0] = inputs[IN_1_INPUT].getVoltage();
        channelVoltages[1] = inputs[IN_2_INPUT].getVoltage();
        channelVoltages[2] = inputs[IN_3_INPUT].getVoltage();
        channelVoltages[3] = params[IN_4_PARAM].getValue();

        float mix = 0;
        for (auto& i : channelVoltages) {
            if (!std::isfinite(i)) {
                i = 0;
            }
            mix += i;
        }

        auto lowCV = tame(inputs[LOW_CV_INPUT].getVoltage());

        auto highCV = tame(inputs[HIGH_CV_INPUT].getVoltage());

        auto low = params[LOW_PARAM].getValue() + params[LOW_TRIM_PARAM].getValue() * lowCV;
        auto high = params[HIGH_PARAM].getValue() + params[HIGH_TRIM_PARAM].getValue() * highCV;

        bool isTriangle = params[SAW_SQUARE_SELECTOR_PARAM].getValue() < 0.5f;

        float modMix;
        if (std::abs(high - low) < 0.001) {
            modMix = 0;
        } else if (isTriangle) {
            modMix = mix - low;
            modMix /= (high - low);
            modMix = std::abs(modMix - 2 * std::floor((modMix + 1) * 0.5f));
        } else {
            modMix = mix - low;
            modMix /= (high - low);
            modMix -= std::floor(modMix);
        }

        auto normCV = tame(inputs[NORM_LEVEL_INPUT].getVoltage());

        auto normedLevel = tame(params[NORM_LEVEL_PARAM].getValue() + normCV);
        auto normOutput = modMix * abs(normedLevel) + (normedLevel < 0 ? normedLevel * 0.5f : 0);

        outputs[OUT_OUTPUT].setVoltage(modMix * (high - low) + low);
        outputs[NORM_OUT_OUTPUT].setVoltage(normOutput);

        lights[NORM_INDICATOR_LIGHT + 0].setBrightnessSmooth(std::max(normedLevel * 0.1f, 0.f), args.sampleTime);
        lights[NORM_INDICATOR_LIGHT + 2].setBrightnessSmooth(std::max(-normedLevel * 0.1f, 0.f), args.sampleTime);

        lights[OUT_INDICATOR_LIGHT + 0].setBrightnessSmooth(std::max(normOutput * 0.1f, 0.f), args.sampleTime);
        lights[OUT_INDICATOR_LIGHT + 2].setBrightnessSmooth(std::max(-normOutput * 0.1f, 0.f), args.sampleTime);
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
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.465, 74.372)), module, Modmix::LOW_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(17.974, 74.372)), module, Modmix::HIGH_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(17.955, 99.814)), module, Modmix::NORM_LEVEL_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.465, 111.948)), module, Modmix::NORM_OUT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(17.974, 111.948)), module, Modmix::OUT_OUTPUT));

		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(mm2px(Vec(12.7, 102.873)), module, Modmix::NORM_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(mm2px(Vec(20.61, 119.375)), module, Modmix::OUT_INDICATOR_LIGHT));
	}
};


Model* modelModmix = createModel<Modmix, ModmixWidget>("modmix");