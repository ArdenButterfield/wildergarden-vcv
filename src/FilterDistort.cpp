#include "plugin.hpp"
#include "feedback_solvers/FeedbackLoop.hpp"
#include "feedback_solvers/NaiveSolver.hpp"
#include "feedback_solvers/NewtonZonesSolver.hpp"
#include "feedback_solvers/SafeguardedNewtonSolver.hpp"

struct FilterDistort : Module {
	enum ParamId {
		FREQ_PARAM,
		Q_PARAM,
		FILTER_MODE_PARAM,
		FEEDBACK_PARAM,
		SHAPE_PARAM,
		SOLVER_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		SIGNAL_INPUT,
		FREQ_CV_INPUT,
		Q_CV_INPUT,
		FEEDBACK_CV_INPUT,
		SHAPE_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		SIGNAL_OUTPUT,
		STATE_OUTPUT,
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

	enum Solver {
		SOLVER_NAIVE,
		SOLVER_SAFEGUARDED_NEWTON,
		SOLVER_NEWTON_ZONES
	};

	dsp::BiquadFilter filter;

	// Last values used to compute coefficients, so we only recompute on change
	float lastFreqParam = -1.f;
	float lastQParam = -1.f;
	int lastMode = -1;
	float lastSampleRate = -1.f;

	NaiveSolver naiveSolver;
	SafeguardedNewtonSolver safeguardedNewtonSolver;
	NewtonZonesSolver newtonZonesSolver;

	FilterDistort() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(FREQ_PARAM, 0.f, 1.f, 0.5f, "Filter Frequency", " Hz", FREQ_RANGE, MIN_FREQ);
		configParam(Q_PARAM, 0.f, 1.f, 0.f, "Filter Q", "", Q_RANGE, MIN_Q);
		configSwitch(FILTER_MODE_PARAM, 0.f, 2.f, 0.f, "Filter Mode", {"Lowpass", "Bandpass", "Highpass"});
		configParam(FEEDBACK_PARAM, 0.f, 5.f, 0.f, "Distortion Feedback Gain");
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.f, "Distortion Shape");
		configSwitch(SOLVER_PARAM, 0.f, 2.f, 0.f, "Feedback Loop Solver", {"Naive (unit delay)", "Safeguarded Newton (delay-free)", "Newton Zones (delay-free)"});

		configInput(SIGNAL_INPUT, "Signal");
		configInput(FREQ_CV_INPUT, "Filter Frequency CV");
		configInput(Q_CV_INPUT, "Filter Q CV");
		configInput(FEEDBACK_CV_INPUT, "Distortion Feedback Gain CV");
		configInput(SHAPE_CV_INPUT, "Distortion Shape CV");

		configOutput(SIGNAL_OUTPUT, "Signal");
		configOutput(STATE_OUTPUT, "Filter state contribution (s)");
	}

	void onReset() override {
		filter.reset();
		naiveSolver.reset();
	}

	// Knob position plus CV, clamped to the knob's range
	float modulatedParam(const ParamId param, const InputId cvInput) {
		auto* quantity = paramQuantities[param];
		return clamp(params[param].getValue() + inputs[cvInput].getVoltage(), quantity->getMinValue(), quantity->getMaxValue());
	}

	void updateFilter(float sampleRate) {
		const auto freqParam = modulatedParam(FREQ_PARAM, FREQ_CV_INPUT);
		const auto qParam = modulatedParam(Q_PARAM, Q_CV_INPUT);
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

	void process(const ProcessArgs& args) override {
		updateFilter(args.sampleRate);
		const auto input = inputs[SIGNAL_INPUT].getVoltage();
		const auto shapeParam = modulatedParam(SHAPE_PARAM, SHAPE_CV_INPUT);
		const auto feedbackParam = modulatedParam(FEEDBACK_PARAM, FEEDBACK_CV_INPUT);

		const FeedbackLoop loop(filter, input, shapeParam, feedbackParam);
		int iterations;
		float feedback;
		switch ((int) std::round(params[SOLVER_PARAM].getValue())) {
			case SOLVER_SAFEGUARDED_NEWTON: feedback = safeguardedNewtonSolver.solve(loop, iterations); break;
			case SOLVER_NEWTON_ZONES: feedback = newtonZonesSolver.solve(loop, iterations); break;
			default: feedback = naiveSolver.solve(loop, iterations); break;
		}
		// The delay-free solvers stay inside a finite bracket, so this only falls back to the
		// unit-delay loop if the input itself is not finite
		if (!std::isfinite(feedback)) {
			feedback = naiveSolver.previousFeedback;
		}

		auto filtered = filter.process(input + feedback);
		auto distorted = FeedbackLoop::distort(filtered * 0.2f, shapeParam) * 5.f;
		naiveSolver.update(distorted * feedbackParam);
		outputs[SIGNAL_OUTPUT].setVoltage(distorted);
		// The filter state's contribution to this sample's output, computed before the filter
		// advanced
		outputs[STATE_OUTPUT].setVoltage(loop.s);
	}
};


struct FilterDistortWidget : ModuleWidget {
	FilterDistortWidget(FilterDistort* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/FilterDistort.svg")));

		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(15.24, 16.0)), module, FilterDistort::FREQ_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(9.0, 38.0)), module, FilterDistort::Q_PARAM));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(21.48, 43.5)), module, FilterDistort::FILTER_MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(9.0, 62.0)), module, FilterDistort::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(21.48, 62.0)), module, FilterDistort::SHAPE_PARAM));

		addParam(createParamCentered<CKSSThree>(mm2px(Vec(15.24, 88.0)), module, FilterDistort::SOLVER_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.0, 111.948)), module, FilterDistort::SIGNAL_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.24, 27.0)), module, FilterDistort::FREQ_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.0, 49.0)), module, FilterDistort::Q_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(9.0, 73.0)), module, FilterDistort::FEEDBACK_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(21.48, 73.0)), module, FilterDistort::SHAPE_CV_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(21.48, 111.948)), module, FilterDistort::SIGNAL_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(15.24, 100.0)), module, FilterDistort::STATE_OUTPUT));
	}
};


Model* modelFilterDistort = createModel<FilterDistort, FilterDistortWidget>("FilterDistort");
