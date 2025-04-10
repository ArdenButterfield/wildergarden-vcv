#include "plugin.hpp"
#include <iostream>

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
        ENUMS(LENGTH_BINARY_COUNTER_LIGHT, 5),
        ENUMS(SUBDIVISIONS_BINARY_COUNTER_LIGHT, 5),
		OUT_LIGHT,
		LIGHTS_LEN
	};

    dsp::SchmittTrigger clockTrigger;
    int clockCounter;
    int clockLength;
    int beatCounter;

    Scrub() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(LENGTH_PARAM, 1.f, 32.f, 8.f, "Length");
		configParam(SUBDIVISIONS_PARAM, 1.f, 32.f, 4.f, "Subdivisions");
		configParam(QUANTIZE_PARAM, 0.f, 1.f, 0.f, "Quantize");
		configInput(CLOCK_INPUT, "Clock");
		configInput(LENGTH_CV_INPUT, "Length CV");
		configInput(SUBDIVISIONS_CV_INPUT, "Subdivisions CV");
		configInput(QUANTIZE_CV_INPUT, "Quantize CV");
		configOutput(STEP_OUTPUT, "Step");
		configOutput(TRIG_OUTPUT, "Trigger");
        clockLength = 0;
        clockCounter = 0;
        beatCounter = 0;
	}

    void setBinaryIndicatorLight(int light, int val, float sampleTime) {
        for (int i = 0; i < 5; ++i) {
            lights[light + 4 - i].setBrightnessSmooth((val & (1 << i)) ? 1.f : 0.f, sampleTime);
        }
    }


    void process(const ProcessArgs& args) override {
        auto length = static_cast<int>(std::round(params[LENGTH_PARAM].getValue() + inputs[LENGTH_CV_INPUT].getVoltage() * 32 / 10));
        auto subdivisions = static_cast<int>(std::round(params[SUBDIVISIONS_PARAM].getValue() + inputs[SUBDIVISIONS_CV_INPUT].getVoltage() * 32 / 10));
        auto quantized = std::min(std::max(0.f, params[QUANTIZE_PARAM].getValue() + inputs[QUANTIZE_CV_INPUT].getVoltage() * 0.1f), 1.f);
        auto clockGoingHigh = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), args.sampleTime);
        if (clockGoingHigh) {
            if (clockCounter < args.sampleRate * 10) {
                clockLength = clockCounter;
            }
            clockCounter = 0;
            beatCounter++;
            beatCounter %= length;
        }

        lights[CLOCK_LIGHT].setBrightnessSmooth(clockGoingHigh ? 1.f : 0.f, args.sampleTime);

        setBinaryIndicatorLight(LENGTH_BINARY_COUNTER_LIGHT, length, args.sampleTime);
        setBinaryIndicatorLight(SUBDIVISIONS_BINARY_COUNTER_LIGHT, subdivisions, args.sampleTime);

        if (clockLength <= subdivisions || subdivisions <= 0) {
            clockCounter++;
            return;
        }
        auto onSubdivision = (clockCounter < clockLength) && ((clockCounter % (clockLength / subdivisions)) == 0);
        lights[OUT_LIGHT].setBrightnessSmooth(onSubdivision ? 1.f : 0.f, args.sampleTime);

        auto totalNumDivisions = length * subdivisions;
        auto rescale = totalNumDivisions > 0 ? 10.f / static_cast<float>(totalNumDivisions) : 0.f;
        auto stepLength = clockLength / subdivisions;
        auto currentStep = std::min(clockCounter / stepLength, totalNumDivisions - 1);
        auto elapsedWithinCurrentStep = clockCounter - (currentStep * stepLength);
        outputs[STEP_OUTPUT].setVoltage((currentStep + quantized * static_cast<float>(elapsedWithinCurrentStep) / static_cast<float>(stepLength)) * rescale);
        clockCounter++;
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
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(17.78, 105.78)), module, Scrub::OUT_LIGHT));

        addBinaryCounter(module, Scrub::LENGTH_BINARY_COUNTER_LIGHT, 17.78, 38.881);
        addBinaryCounter(module, Scrub::SUBDIVISIONS_BINARY_COUNTER_LIGHT, 17.78, 61.22);
	}

    void addBinaryCounter(Scrub* module, int enumIndex, double centerX, double centerY) {
        const auto step = 3.1;
        const auto numSteps = 5;
        const auto offset = centerX - 0.5 * (numSteps - 1) * step;
        for (int i = 0; i < numSteps; ++i) {
            auto x = step * i + offset;
            addChild(createLightCentered<SmallLight<BlueLight>>(mm2px(Vec(x, centerY)), module, enumIndex + i));
        }
    }

};


Model* modelScrub = createModel<Scrub, ScrubWidget>("Scrub");