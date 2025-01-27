#include "plugin.hpp"


struct Dimit : Module {
	enum ParamId {
		THRESHOLD_PARAM,
		GRITKNOB_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		GRITCV_INPUT,
		INPUT_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		PATH10_LIGHT,
		LIGHTS_LEN
	};

    dsp::TExponentialSlewLimiter<float> envelopeFollower;
    float smoothedOverage;


	Dimit() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(THRESHOLD_PARAM, 0.f, 10.f, 5.f, "Threshold");
		configParam(GRITKNOB_PARAM, 0.f, 1.f, 0.f, "Grit");
		configInput(GRITCV_INPUT, "Grit input");
		configInput(INPUT_INPUT, "Audio input");
		configOutput(OUTPUT_OUTPUT, "Compressed output");

        envelopeFollower.reset();
        smoothedOverage = 0;
	}

	void process(const ProcessArgs& args) override {
        const auto threshold = params[THRESHOLD_PARAM].getValue();
        auto gritCombined = params[GRITKNOB_PARAM].getValue() + inputs[GRITCV_INPUT].getVoltage();
        gritCombined = std::min(std::max(0.f, gritCombined), 1.f);
        envelopeFollower.setRiseFall(1000 + gritCombined * 10000, 10 + gritCombined * 1000);
        auto in = inputs[INPUT_INPUT].getVoltage();
        auto envelope = envelopeFollower.process(args.sampleTime, std::abs(in));
        auto overage = std::max(envelope - threshold, 0.f) / std::max(threshold, 0.0001f);
        in /= 1 + overage;

        outputs[OUTPUT_OUTPUT].setVoltage(in);
        // outputs[OUTPUT_OUTPUT].setVoltage(in * gainScaling);
/*

        auto out = in;
        out = std::max(out, -params[THRESHOLD_PARAM].getValue());
        out = std::min(out, params[THRESHOLD_PARAM].getValue());
        auto light = 0.f;
        light = std::min(std::max(0.f, (std::abs(in) - std::abs(out)) / params[THRESHOLD_PARAM].getValue()), 1.f);
        lights[PATH10_LIGHT].setBrightnessSmooth(light, args.sampleTime);
*/

	}
};


struct DimitWidget : ModuleWidget {
	DimitWidget(Dimit* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Dimit.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.16, 17.433)), module, Dimit::THRESHOLD_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.16, 41.874)), module, Dimit::GRITKNOB_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.16, 63.5)), module, Dimit::GRITCV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.16, 80.211)), module, Dimit::INPUT_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(10.16, 102.376)), module, Dimit::OUTPUT_OUTPUT));

		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(17.417, 92.58)), module, Dimit::PATH10_LIGHT));
	}
};


Model* modelDimit = createModel<Dimit, DimitWidget>("Dimit");