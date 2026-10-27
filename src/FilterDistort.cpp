#include "plugin.hpp"

struct FilterDistort : Module {
	enum ParamId {
		FREQ_PARAM,
		Q_PARAM,
		FILTER_MODE_PARAM,
		FEEDBACK_PARAM,
		SHAPE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		SIGNAL_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		SIGNAL_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// Knob positions map exponentially: freq 20 Hz - 20 kHz, Q 0.707 - 20
	static constexpr float MIN_FREQ = 20.f;
	static constexpr float FREQ_RANGE = 1000.f;
	static constexpr float MIN_Q = 0.70710678f;
	static constexpr float Q_RANGE = 28.284271f;

	dsp::BiquadFilter filter;

	// Last values used to compute coefficients, so we only recompute on change
	float lastFreqParam = -1.f;
	float lastQParam = -1.f;
	int lastMode = -1;
	float lastSampleRate = -1.f;
	float feedbackState = 0;

	FilterDistort() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(FREQ_PARAM, 0.f, 1.f, 0.5f, "Filter Frequency", " Hz", FREQ_RANGE, MIN_FREQ);
		configParam(Q_PARAM, 0.f, 1.f, 0.f, "Filter Q", "", Q_RANGE, MIN_Q);
		configSwitch(FILTER_MODE_PARAM, 0.f, 2.f, 0.f, "Filter Mode", {"Lowpass", "Bandpass", "Highpass"});
		configParam(FEEDBACK_PARAM, 0.f, 5.f, 0.f, "Distortion Feedback Gain");
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.f, "Distortion Shape");

		configInput(SIGNAL_INPUT, "Signal");

		configOutput(SIGNAL_OUTPUT, "Signal");
	}

	void onReset() override {
		filter.reset();
		feedbackState = 0;
	}

	void updateFilter(float sampleRate) {
		const auto freqParam = params[FREQ_PARAM].getValue();
		const auto qParam = params[Q_PARAM].getValue();
		const auto mode = (int) std::round(params[FILTER_MODE_PARAM].getValue());
		if (freqParam == lastFreqParam && qParam == lastQParam && mode == lastMode && sampleRate == lastSampleRate) {
			return;
		}
		lastFreqParam = freqParam;
		lastQParam = qParam;
		lastMode = mode;
		lastSampleRate = sampleRate;

		const auto freq = MIN_FREQ * std::pow(FREQ_RANGE, freqParam);
		const auto q = MIN_Q * std::pow(Q_RANGE, qParam);
		// Keep normalized frequency below Nyquist, where tan() in the biquad blows up
		const auto normalizedFreq = std::min(freq / sampleRate, 0.45f);

		dsp::BiquadFilter::Type type;
		switch (mode) {
			case 1: type = dsp::BiquadFilter::BANDPASS; break;
			case 2: type = dsp::BiquadFilter::HIGHPASS; break;
			default: type = dsp::BiquadFilter::LOWPASS; break;
		}
		filter.setParameters(type, normalizedFreq, q, 1.f);
	}

	float distort(const float in, const float shape) {
		auto a = sin(in + (shape - 0.5f) * M_PI);
		auto b = tanh(in);
		auto blend = std::min(shape * 2, 1.f);
		return blend * a + (1 - blend) * b;
	}

	void process(const ProcessArgs& args) override {
		updateFilter(args.sampleRate);
		const auto in = inputs[SIGNAL_INPUT].getVoltage() + feedbackState;
		const auto shapeParam = params[SHAPE_PARAM].getValue();
		const auto feedbackParam = params[FEEDBACK_PARAM].getValue();
		auto filtered = filter.process(in);
		auto distorted = distort(filtered * 0.2f, shapeParam) * 5.f;
		feedbackState = distorted * feedbackParam;
		outputs[SIGNAL_OUTPUT].setVoltage(distorted);
	}
};


struct FilterDistortWidget : ModuleWidget {
	FilterDistortWidget(FilterDistort* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/FilterDistort.svg")));

		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(15.24, 24.0)), module, FilterDistort::FREQ_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(9.0, 44.0)), module, FilterDistort::Q_PARAM));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(21.48, 44.0)), module, FilterDistort::FILTER_MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(9.0, 68.0)), module, FilterDistort::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(21.48, 68.0)), module, FilterDistort::SHAPE_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.0, 111.948)), module, FilterDistort::SIGNAL_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(21.48, 111.948)), module, FilterDistort::SIGNAL_OUTPUT));
	}
};


Model* modelFilterDistort = createModel<FilterDistort, FilterDistortWidget>("FilterDistort");
