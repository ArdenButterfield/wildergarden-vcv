#include "plugin.hpp"
#include <array>

struct Pascal : Module {
	enum ParamId {
		CLOCK_PARAM,
		INSERT_PARAM,
		FREEZE_PARAM,
        RESET_MODE_PARAM,
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
        RESET_INPUT,
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
        ENUMS(STATE_LIGHT, 384),
        ENUMS(LENGTH_BINARY_COUNTER_LIGHT, 5),
        ENUMS(DIVISIONS1_BINARY_COUNTER_LIGHT, 5),
        ENUMS(DIVISIONS2_BINARY_COUNTER_LIGHT, 5),
        ENUMS(DIVISIONS3_BINARY_COUNTER_LIGHT, 5),
		LIGHTS_LEN
	};

    dsp::SchmittTrigger clockTrigger;
    dsp::SchmittTrigger insertTrigger;
    dsp::SchmittTrigger freezeTrigger;
    dsp::BooleanTrigger clockGoingHighTrigger;
    dsp::BooleanTrigger clockGoingLowTrigger;

    std::array<int, 3> divisions;
    std::array<float, 3> pitch;
    std::array<int, 3> divisionsParams = {
            COUNTER1_DIVISIONS_PARAM,
            COUNTER2_DIVISIONS_PARAM,
            COUNTER3_DIVISIONS_PARAM
    };

    std::array<int, 3> divisionInputs = {
            COUNTER1_DIVISIONS_CV_INPUT,
            COUNTER2_DIVISIONS_CV_INPUT,
            COUNTER3_DIVISIONS_CV_INPUT
    };

    std::array<int, 3> divisionIndicators = {
            DIVISIONS1_BINARY_COUNTER_LIGHT,
            DIVISIONS2_BINARY_COUNTER_LIGHT,
            DIVISIONS3_BINARY_COUNTER_LIGHT
    };

    std::array<std::array<int, 32>, 4> state;
    int column;
    int step;


    Pascal() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(CLOCK_PARAM, 0.f, 1.f, 0.f, "Clock");
		configParam(INSERT_PARAM, 0.f, 1.f, 0.f, "Insert");
		configParam(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze");
        configParam(RESET_MODE_PARAM, 0.f, 2.f, 0.f, "Reset mode");
		configParam(LENGTH_PARAM, 1.f, 32.f, 16.f, "Length");
		configParam(COUNTER1_DIVISIONS_PARAM, 1.f, 32.f, 4.f, "Divisions 1");
		configParam(COUNTER3_DIVISIONS_PARAM, 1.f, 32.f, 4.f, "Divisions 3");
		configParam(COUNTER2_DIVISIONS_PARAM, 1.f, 32.f, 4.f, "Divisions 2");
		configInput(CLOCK_CV_INPUT, "Clock input");
		configInput(INSERT_CV_INPUT, "Insert input");
		configInput(FREEZE_CV_INPUT, "Freeze Input");
        configInput(RESET_INPUT, "Reset input");
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

        for (auto& p : pitch) {
            p = 0;
        }
    }

    void setBinaryIndicatorLight(int light, int val, float sampleTime) {
        for (int i = 0; i < 5; ++i) {
            lights[light + 4 - i].setBrightnessSmooth((val & (1 << i)) ? 1.f : 0.f, sampleTime);
        }
    }

    void getColor(int val, float& r, float& g, float& b) {
        if (val == 0) {
            r = 0; g = 0; b = 0;
        } else {
            r = val % divisions[0] ? 1.0 : 0.0;
            g = val % divisions[1] ? 1.0 : 0.0;
            b = val % divisions[2] ? 1.0 : 0.0;

            auto totalBrightness = r + g + b;

            if (totalBrightness > 0) {
                r /= totalBrightness;
                g /= totalBrightness;
                b /= totalBrightness;
            }
        }
    }

	void process(const ProcessArgs& args) override {
        bool isFrozen = inputs[FREEZE_CV_INPUT].getVoltage() > 1.0f
                        || params[FREEZE_PARAM].getValue() > 0.5f;

        lights[FREEZE_INDICATOR_LIGHT].setBrightnessSmooth(isFrozen ? 1.f : 0.f, args.sampleTime);

        clockTrigger.process(inputs[CLOCK_CV_INPUT].getVoltage(), 0.1f, 1.f);
        bool clockState =
                clockTrigger.isHigh() ||
                (params[CLOCK_PARAM].getValue() > 0.5f);
        bool clockGoingHigh = clockGoingHighTrigger.process(clockState);
        bool clockGoingLow = clockGoingLowTrigger.process(!clockState);

        lights[CLOCK_INDICATOR_LIGHT].setBrightnessSmooth(clockState ? 1.f : 0.f, args.sampleTime);

        auto length = static_cast<int>(std::round(params[LENGTH_PARAM].getValue() + inputs[LENGTH_CV_INPUT].getVoltage() * 32 / 10));
        length = std::min(std::max(1, length), 32);
        setBinaryIndicatorLight(LENGTH_BINARY_COUNTER_LIGHT, length, args.sampleTime);

        int globalDivisions = 1;
        for (int div = 0; div < 3; ++div) {
            divisions[div] = static_cast<int>(std::round(params[divisionsParams[div]].getValue() + inputs[divisionInputs[div]].getVoltage() * 32 / 10));
            divisions[div] = std::min(std::max(1, divisions[div]), 32);
            setBinaryIndicatorLight(divisionIndicators[div], divisions[div], args.sampleTime);
            globalDivisions *= divisions[div];
        }

        if (clockGoingLow) {
            ++step;
            auto advance = isFrozen ? 0 : (step / length);
            column += advance;
            step %= length;
            column %= 4;
            if (step == 0) {
                state[column][step] = state[(column + 3) % 4][step] % globalDivisions;
            } else {
                state[column][step] = (state[(column + 3) % 4][step] + state[(column + 3) % 4][step - 1]) % globalDivisions;
            }

            for (int i = length; i < 32; ++i) {
                state[column][i] = 0;
            }
        }


        auto insertVolt = inputs[INSERT_CV_INPUT].getVoltage();
        if (insertTrigger.process(insertVolt, 0.1f, 1.f) || params[INSERT_PARAM].getValue() > 0.5f) {
            state[column][step] = 1;
            lights[INSERT_INDICATOR_LIGHT].setBrightnessSmooth(1.f, args.sampleTime);
        } else {
            lights[INSERT_INDICATOR_LIGHT].setBrightnessSmooth(0.f, args.sampleTime);
        }

        auto val = state[column][step];

        if (clockGoingHigh) {
            for (int i = 0; i < 3; ++i) {
                if (val % divisions[i]) {
                    pitch[i] = static_cast<float>(state[column][step] % divisions[i] - 1) * 10.f / divisions[i];
                }
            }
        }

        for (int i = 0; i < 32 * 4; ++i) {
            if (column * 32 + step == i) {
                // white
                lights[STATE_LIGHT + i * 3 + 0].setBrightnessSmooth(1.f, args.sampleTime);
                lights[STATE_LIGHT + i * 3 + 1].setBrightnessSmooth(1.f, args.sampleTime);
                lights[STATE_LIGHT + i * 3 + 2].setBrightnessSmooth(1.f, args.sampleTime);
            } else {
                float r, g, b;
                getColor(state[i / 32][i % 32], r, g, b);

                lights[STATE_LIGHT + i * 3 + 0].setBrightnessSmooth(r, args.sampleTime);
                lights[STATE_LIGHT + i * 3 + 1].setBrightnessSmooth(g, args.sampleTime);
                lights[STATE_LIGHT + i * 3 + 2].setBrightnessSmooth(b, args.sampleTime);
            }
        }


        {
            bool trig1 = clockState && (val % divisions[0]);
            lights[COUNTER1_INDICATOR_LIGHT].setBrightnessSmooth(trig1 ? 1.0 : 0.0, args.sampleTime);
            outputs[COUNTER1_TRIGGER_OUTPUT].setVoltage(trig1 ? 10.0 : 0.0);
        }

        {
            bool trig2 = clockState && (val % divisions[1]);
            lights[COUNTER2_INDICATOR_LIGHT].setBrightnessSmooth(trig2 ? 1.0 : 0.0, args.sampleTime);
            outputs[COUNTER2_TRIGGER_OUTPUT].setVoltage(trig2 ? 10.0 : 0.0);
        }

        {
            bool trig3 = clockState && (val % divisions[2]);
            outputs[COUNTER3_TRIGGER_OUTPUT].setVoltage(trig3 ? 10.0 : 0.0);
            outputs[COUNTER3_TRIG1_OUTPUT].setVoltage(trig3 && (val % divisions[2] == 1) ? 10.0 : 0.0);
            outputs[COUNTER3_TRIG2_OUTPUT].setVoltage(trig3 && (val % divisions[2] == 2) ? 10.0 : 0.0);
            outputs[COUNTER3_TRIG3_OUTPUT].setVoltage(trig3 && (val % divisions[2] == 3) ? 10.0 : 0.0);
            outputs[COUNTER3_TRIG4_OUTPUT].setVoltage(trig3 && (val % divisions[2] == 4) ? 10.0 : 0.0);
            outputs[COUNTER3_TRIG5_OUTPUT].setVoltage(trig3 && (val % divisions[2] == 5) ? 10.0 : 0.0);
            outputs[COUNTER3_TRIG6_OUTPUT].setVoltage(trig3 && (val % divisions[2] == 6) ? 10.0 : 0.0);

            lights[COUNTER3_INDICATOR_LIGHT].setBrightnessSmooth(trig3 ? 1.0 : 0.0, args.sampleTime);
            lights[COUNTER3_INDICATOR1_LIGHT].setBrightnessSmooth(trig3 && (val % divisions[2] == 6) ? 1.0 : 0.0, args.sampleTime);
            lights[COUNTER3_INDICATOR1_LIGHT].setBrightnessSmooth(trig3 && (val % divisions[2] == 1) ? 1.0 : 0.0, args.sampleTime);
            lights[COUNTER3_INDICATOR2_LIGHT].setBrightnessSmooth(trig3 && (val % divisions[2] == 2) ? 1.0 : 0.0, args.sampleTime);
            lights[COUNTER3_INDICATOR3_LIGHT].setBrightnessSmooth(trig3 && (val % divisions[2] == 3) ? 1.0 : 0.0, args.sampleTime);
            lights[COUNTER3_INDICATOR4_LIGHT].setBrightnessSmooth(trig3 && (val % divisions[2] == 4) ? 1.0 : 0.0, args.sampleTime);
            lights[COUNTER3_INDICATOR5_LIGHT].setBrightnessSmooth(trig3 && (val % divisions[2] == 5) ? 1.0 : 0.0, args.sampleTime);
            lights[COUNTER3_INDICATOR6_LIGHT].setBrightnessSmooth(trig3 && (val % divisions[2] == 6) ? 1.0 : 0.0, args.sampleTime);
        }

        outputs[COUNTER1_PITCH_OUTPUT].setVoltage(pitch[0]);
        outputs[COUNTER2_PITCH_OUTPUT].setVoltage(pitch[1]);
        outputs[COUNTER3_PITCH_OUTPUT].setVoltage(pitch[2]);
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

        const float TOP_ROW_Y = 13.455;
        const float ITEM_HEIGHT = 28.316;
        const float ITEM_WIDTH = 17;
        const float CLOCK_X = 4.628;
        const float INSERT_X = 22.657;
        const float FREEZE_X = 40.642;
        const float RESET_X = 58.605;
        const float RESET_WIDTH = 15.716;
        const float LENGTH_X = 75.399;
        const float LENGTH_WIDTH = 21.464;
        const float PARAM_OFFSET = 20.4 - TOP_ROW_Y;
        const float INPUT_OFFSET = 34.022 - TOP_ROW_Y;

		addParam(createParamCentered<VCVButton>(mm2px(Vec(CLOCK_X + ITEM_WIDTH * 0.5, TOP_ROW_Y + PARAM_OFFSET)), module, Pascal::CLOCK_PARAM));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(INSERT_X + ITEM_WIDTH * 0.5, TOP_ROW_Y + PARAM_OFFSET)), module, Pascal::INSERT_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(FREEZE_X + ITEM_WIDTH * 0.5, TOP_ROW_Y + PARAM_OFFSET)), module, Pascal::FREEZE_PARAM));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(RESET_X + RESET_WIDTH * 0.55, TOP_ROW_Y + PARAM_OFFSET)), module, Pascal::RESET_MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(LENGTH_X + LENGTH_WIDTH * 0.5, TOP_ROW_Y + PARAM_OFFSET)), module, Pascal::LENGTH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(15.131, 49.774)), module, Pascal::COUNTER1_DIVISIONS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(70.314, 54.858)), module, Pascal::COUNTER3_DIVISIONS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(15.17, 88.851)), module, Pascal::COUNTER2_DIVISIONS_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(CLOCK_X + ITEM_WIDTH * 0.5, TOP_ROW_Y + INPUT_OFFSET)), module, Pascal::CLOCK_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(INSERT_X + ITEM_WIDTH * 0.5, TOP_ROW_Y + INPUT_OFFSET)), module, Pascal::INSERT_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(FREEZE_X + ITEM_WIDTH * 0.5, TOP_ROW_Y + INPUT_OFFSET)), module, Pascal::FREEZE_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(RESET_X + ITEM_WIDTH * 0.5, TOP_ROW_Y + INPUT_OFFSET)), module, Pascal::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(LENGTH_X + LENGTH_WIDTH * 0.5, TOP_ROW_Y + INPUT_OFFSET)), module, Pascal::LENGTH_CV_INPUT));
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

        const float LIGHT_OFFSET = 2.5;
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(CLOCK_X + ITEM_WIDTH - LIGHT_OFFSET, TOP_ROW_Y + ITEM_HEIGHT - LIGHT_OFFSET)), module, Pascal::CLOCK_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(INSERT_X + ITEM_WIDTH - LIGHT_OFFSET, TOP_ROW_Y + ITEM_HEIGHT - LIGHT_OFFSET)), module, Pascal::INSERT_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(FREEZE_X + ITEM_WIDTH - LIGHT_OFFSET, TOP_ROW_Y + ITEM_HEIGHT - LIGHT_OFFSET)), module, Pascal::FREEZE_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(32.579, 60.888)), module, Pascal::COUNTER1_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(80.085, 72.995)), module, Pascal::COUNTER3_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(32.618, 99.965)), module, Pascal::COUNTER2_INDICATOR_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(68.636, 99.965)), module, Pascal::COUNTER3_INDICATOR1_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(79.606, 99.965)), module, Pascal::COUNTER3_INDICATOR2_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(90.575, 99.965)), module, Pascal::COUNTER3_INDICATOR3_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(68.636, 113.301)), module, Pascal::COUNTER3_INDICATOR4_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(79.606, 113.301)), module, Pascal::COUNTER3_INDICATOR5_LIGHT));
		addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(90.575, 113.301)), module, Pascal::COUNTER3_INDICATOR6_LIGHT));

		// mm2px(Vec(24.665, 4.142))
		// addChild(createWidget<Widget>(mm2px(Vec(72.015, 37.498))));
        addBinaryCounter(module, Pascal::LENGTH_BINARY_COUNTER_LIGHT, LENGTH_X + LENGTH_WIDTH * 0.5, 37.498 + 4.142 * 0.5);

        // mm2px(Vec(35.367, 7.613))
		// addChild(createWidget<Widget>(mm2px(Vec(61.591, 59.585))));
        addBinaryCounter(module, Pascal::DIVISIONS3_BINARY_COUNTER_LIGHT, 61.591 + 35.367 * 0.5, 59.585 + 7.613 * 0.5);

        // mm2px(Vec(20.488, 8.154))
		// addChild(createWidget<Widget>(mm2px(Vec(4.823, 70.483))));
        addBinaryCounter(module, Pascal::DIVISIONS1_BINARY_COUNTER_LIGHT, 4.823 + 20.488 * 0.5, 70.483 + 8.154 * 0.5);

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

		// mm2px(Vec(20.488, 8.154))
		addChild(createWidget<Widget>(mm2px(Vec(4.863, 109.56))));
        addBinaryCounter(module, Pascal::DIVISIONS2_BINARY_COUNTER_LIGHT, 4.823 + 20.488 * 0.5, 109.56 + 8.154 * 0.5);

    }

    void addBinaryCounter(Pascal* module, int enumIndex, double centerX, double centerY) {
        const auto step = 3.1;
        const auto numSteps = 5;
        const auto offset = centerX - 0.5 * (numSteps - 1) * step;
        for (int i = 0; i < numSteps; ++i) {
            auto x = step * i + offset;
            addChild(createLightCentered<SmallLight<BlueLight>>(mm2px(Vec(x, centerY)), module, enumIndex + i));
        }
    }
};


Model* modelPascal = createModel<Pascal, PascalWidget>("Pascal");