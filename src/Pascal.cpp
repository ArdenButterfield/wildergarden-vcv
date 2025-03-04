#include "plugin.hpp"
#include <array>

struct Pascal : Module {
	enum ParamId {
		CLOCK_PARAM,
		INSERT_PARAM,
		FREEZE_PARAM,
		LENGTH_PARAM,
		COUNTER1_DIVISIONS_PARAM,
		COUNTER3_DIVISIONS_PARAM,
		COUNTER2_DIVISIONS_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_CV_INPUT,
		INSERT_CV_INPUT,
		FREEZE_CV_INPUT,
		LENGTH_CV_INPUT,
		COUNTER3_DIVISIONS_CV_INPUT,
		COUNTER1_DIVISIONS_CV_INPUT,
		COUNTER2_DIVISIONS_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		COUNTER1_TRIGGER_OUTPUT,
		COUNTER1_PITCH_OUTPUT,
		COUNTER3_TRIGGER_OUTPUT,
		COUNTER3_PITCH_OUTPUT,
		COUNTER2_TRIGGER_OUTPUT,
		COUNTER3_TRIG1_OUTPUT,
		COUNTER3_TRIG2_OUTPUT,
		COUNTER3_TRIG3_OUTPUT,
		COUNTER3_TRIG4_OUTPUT,
		COUNTER3_TRIG5_OUTPUT,
		COUNTER3_TRIG6_OUTPUT,
		COUNTER2_PITCH_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		CLOCK_INDICATOR_LIGHT,
		INSERT_INDICATOR_LIGHT,
		FREEZE_INDICATOR_LIGHT,
		COUNTER1_INDICATOR_LIGHT,
		COUNTER3_INDICATOR_LIGHT,
		COUNTER2_INDICATOR_LIGHT,
		COUNTER3_INDICATOR1_LIGHT,
		COUNTER3_INDICATOR2_LIGHT,
		COUNTER3_INDICATOR3_LIGHT,
		COUNTER3_INDICATOR4_LIGHT,
		COUNTER3_INDICATOR5_LIGHT,
		COUNTER3_INDICATOR6_LIGHT,
        ENUMS(STATE_LIGHT, 384), // 32 * 4 * 3
		LIGHTS_LEN
	};

    dsp::SchmittTrigger clockTrigger;
    dsp::BooleanTrigger clockGoingHighTrigger;
    dsp::BooleanTrigger clockGoingLowTrigger;

    int column;
    int step;


    Pascal() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(CLOCK_PARAM, 0.f, 1.f, 0.f, "Clock");
		configParam(INSERT_PARAM, 0.f, 1.f, 0.f, "Insert");
		configParam(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze");
		configParam(LENGTH_PARAM, 1.f, 32.f, 16.f, "Length");
		configParam(COUNTER1_DIVISIONS_PARAM, 1.f, 32.f, 4.f, "Divisions 1");
		configParam(COUNTER3_DIVISIONS_PARAM, 1.f, 32.f, 4.f, "Divisions 3");
		configParam(COUNTER2_DIVISIONS_PARAM, 1.f, 32.f, 4.f, "Divisions 2");
		configInput(CLOCK_CV_INPUT, "Clock input");
		configInput(INSERT_CV_INPUT, "Insert input");
		configInput(FREEZE_CV_INPUT, "Freeze Input");
		configInput(LENGTH_CV_INPUT, "Length input");
		configInput(COUNTER3_DIVISIONS_CV_INPUT, "Divisions 3 input");
		configInput(COUNTER1_DIVISIONS_CV_INPUT, "Divisions 1 input");
		configInput(COUNTER2_DIVISIONS_CV_INPUT, "Divisions 2 input");
		configOutput(COUNTER1_TRIGGER_OUTPUT, "Trigger 1 output");
		configOutput(COUNTER1_PITCH_OUTPUT, "Pitch 1 output");
		configOutput(COUNTER3_TRIGGER_OUTPUT, "Trigger 3 output");
		configOutput(COUNTER3_PITCH_OUTPUT, "Pitch 3 output");
		configOutput(COUNTER2_TRIGGER_OUTPUT, "Trigger 2 output");
		configOutput(COUNTER3_TRIG1_OUTPUT, "Individual trigger output 1");
		configOutput(COUNTER3_TRIG2_OUTPUT, "Individual trigger output 2");
		configOutput(COUNTER3_TRIG3_OUTPUT, "Individual trigger output 3");
		configOutput(COUNTER3_TRIG4_OUTPUT, "Individual trigger output 4");
		configOutput(COUNTER3_TRIG5_OUTPUT, "Individual trigger output 5");
		configOutput(COUNTER3_TRIG6_OUTPUT, "Individual trigger output 6");
		configOutput(COUNTER2_PITCH_OUTPUT, "Pitch 2 outut");

        column = 0;
        step = 0;

        for (auto& col : state) {
            for (auto& i : col) {
                i = 0;
            }
        }

    }

	void process(const ProcessArgs& args) override {


        clockTrigger.process(inputs[CLOCK_CV_INPUT].getVoltage(), 0.1f, 1.f);
        bool clockState =
                clockTrigger.isHigh() ||
                (params[CLOCK_PARAM].getValue() > 0.5f);
        bool clockGoingHigh = clockGoingHighTrigger.process(clockState);
        bool clockGoingLow = clockGoingLowTrigger.process(!clockState);

        lights[CLOCK_INDICATOR_LIGHT].setBrightnessSmooth(clockState ? 1.f : 0.f, args.sampleTime);

        if (clockGoingLow) {
            ++step;
            column += step / 32;
            step %= 32;
            column %= 4;
        }

        for (int i = 0; i < 32 * 4; ++i) {
            if (column * 32 + step == i) {
                // white
                lights[STATE_LIGHT + i * 3 + 0].setBrightnessSmooth(1.f, args.sampleTime);
                lights[STATE_LIGHT + i * 3 + 1].setBrightnessSmooth(1.f, args.sampleTime);
                lights[STATE_LIGHT + i * 3 + 2].setBrightnessSmooth(1.f, args.sampleTime);
            } else {
                lights[STATE_LIGHT + i * 3 + 0].setBrightnessSmooth(0.f, args.sampleTime);
                lights[STATE_LIGHT + i * 3 + 1].setBrightnessSmooth(0.f, args.sampleTime);
                lights[STATE_LIGHT + i * 3 + 2].setBrightnessSmooth(0.f, args.sampleTime);
            }
        }
    }
};


struct PascalWidget : ModuleWidget {
	PascalWidget(Pascal* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Pascal.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<VCVButton>(mm2px(Vec(13.831, 20.423)), module, Pascal::CLOCK_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(36.282, 20.457)), module, Pascal::INSERT_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(58.68, 20.457)), module, Pascal::FREEZE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(84.348, 20.457)), module, Pascal::LENGTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(15.131, 49.774)), module, Pascal::COUNTER1_DIVISIONS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(70.314, 54.858)), module, Pascal::COUNTER3_DIVISIONS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(15.17, 88.851)), module, Pascal::COUNTER2_DIVISIONS_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(13.825, 33.022)), module, Pascal::CLOCK_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(36.231, 33.022)), module, Pascal::INSERT_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(58.682, 33.022)), module, Pascal::FREEZE_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(84.348, 33.026)), module, Pascal::LENGTH_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(87.608, 54.858)), module, Pascal::COUNTER3_DIVISIONS_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.067, 66.01)), module, Pascal::COUNTER1_DIVISIONS_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.107, 105.088)), module, Pascal::COUNTER2_DIVISIONS_CV_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.294, 53.787)), module, Pascal::COUNTER1_TRIGGER_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.294, 72.094)), module, Pascal::COUNTER1_PITCH_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(70.764, 78.999)), module, Pascal::COUNTER3_TRIGGER_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(88.539, 78.999)), module, Pascal::COUNTER3_PITCH_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.334, 92.864)), module, Pascal::COUNTER2_TRIGGER_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(68.636, 93.836)), module, Pascal::COUNTER3_TRIG1_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(79.606, 93.836)), module, Pascal::COUNTER3_TRIG2_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(90.575, 93.836)), module, Pascal::COUNTER3_TRIG3_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(68.636, 107.173)), module, Pascal::COUNTER3_TRIG4_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(79.606, 107.173)), module, Pascal::COUNTER3_TRIG5_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(90.575, 107.173)), module, Pascal::COUNTER3_TRIG6_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.334, 111.171)), module, Pascal::COUNTER2_PITCH_OUTPUT));

		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(20.033, 38.778)), module, Pascal::CLOCK_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(42.45, 38.797)), module, Pascal::INSERT_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(64.856, 38.797)), module, Pascal::FREEZE_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(32.579, 60.888)), module, Pascal::COUNTER1_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(80.085, 72.995)), module, Pascal::COUNTER3_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(32.618, 99.965)), module, Pascal::COUNTER2_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(68.636, 99.965)), module, Pascal::COUNTER3_INDICATOR1_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(79.606, 99.965)), module, Pascal::COUNTER3_INDICATOR2_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(90.575, 99.965)), module, Pascal::COUNTER3_INDICATOR3_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(68.636, 113.301)), module, Pascal::COUNTER3_INDICATOR4_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(79.606, 113.301)), module, Pascal::COUNTER3_INDICATOR5_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(90.575, 113.301)), module, Pascal::COUNTER3_INDICATOR6_LIGHT));

        const auto stateWidth = 18.136;
        const auto stateHeight = 74.382;
        const auto stateX = 41.758;
        const auto stateY = 43.504;

        auto lightAreaMargin = 4;
        auto xStep = (stateWidth - 2 * lightAreaMargin) / (4 - 1);
        auto yStep = (stateHeight - 2 * lightAreaMargin) / (32 - 1);

        for (int i = 0; i < 4 * 32; ++i) {
            auto row = i % 32;
            auto col = i / 32;
            auto x = stateX + lightAreaMargin + col * xStep;
            auto y = stateY + lightAreaMargin + row * yStep;
            auto lightIndex = Pascal::STATE_LIGHT + i * 3;
            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(x, y)), module, lightIndex));
        }
    }
};


Model* modelPascal = createModel<Pascal, PascalWidget>("Pascal");