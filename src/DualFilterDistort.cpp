#include "plugin.hpp"
#include "feedback_solvers/DualFeedbackLoop.hpp"
#include "feedback_solvers/NaiveSolver.hpp"
#include "feedback_solvers/DualNewtonZonesSolver.hpp"
#include "feedback_solvers/SafeguardedNewtonSolver.hpp"

// Two FilterDistort paths in parallel: the input plus the summed, gain-scaled distorted outputs of
// both paths goes into each path's filter, so the loop to solve each sample is
// f1(a1 u + c1) + f2(a2 u + c2) - u = 0 for the summed feedback u (see DualFeedbackLoop).
struct DualFilterDistort : Module {
	static constexpr int PATHS = DualFeedbackLoop::PATHS;

	// Per-path params, inputs and outputs are laid out path by path, PATH_*_LEN apart
	enum ParamId {
		FREQ_PARAM,
		Q_PARAM,
		FILTER_MODE_PARAM,
		FEEDBACK_PARAM,
		SHAPE_PARAM,
		PATH_PARAMS_LEN,
		SOLVER_PARAM = PATH_PARAMS_LEN * PATHS,
		PARAMS_LEN
	};
	enum InputId {
		FREQ_CV_INPUT,
		Q_CV_INPUT,
		FEEDBACK_CV_INPUT,
		SHAPE_CV_INPUT,
		PATH_INPUTS_LEN,
		SIGNAL_INPUT = PATH_INPUTS_LEN * PATHS,
		INPUTS_LEN
	};
	enum OutputId {
		PATH_OUTPUT,
		PATH_OUTPUTS_LEN,
		MIX_OUTPUT = PATH_OUTPUTS_LEN * PATHS,
		ITERATIONS_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	static int param(const int path, const ParamId id) {
		return path * PATH_PARAMS_LEN + id;
	}
	static int input(const int path, const InputId id) {
		return path * PATH_INPUTS_LEN + id;
	}
	static int output(const int path, const OutputId id) {
		return path * PATH_OUTPUTS_LEN + id;
	}

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

	struct Path {
		dsp::BiquadFilter filter;
		// Last values used to compute coefficients, so we only recompute on change
		float lastFreqParam = -1.f;
		float lastQParam = -1.f;
		int lastMode = -1;
		float lastSampleRate = -1.f;
	};
	Path paths[PATHS];

	// The naive solver's previous feedback is also the previous summed feedback u, where the
	// delay-free solvers start
	NaiveSolver naiveSolver;
	SafeguardedNewtonSolver safeguardedNewtonSolver;
	DualNewtonZonesSolver newtonZonesSolver;

	DualFilterDistort() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		for (int i = 0; i < PATHS; i++) {
			const auto name = "Path " + std::to_string(i + 1) + " ";
			configParam(param(i, FREQ_PARAM), 0.f, 1.f, 0.5f, name + "Filter Frequency", " Hz", FREQ_RANGE, MIN_FREQ);
			configParam(param(i, Q_PARAM), 0.f, 1.f, 0.f, name + "Filter Q", "", Q_RANGE, MIN_Q);
			configSwitch(param(i, FILTER_MODE_PARAM), 0.f, 2.f, 0.f, name + "Filter Mode", {"Lowpass", "Bandpass", "Highpass"});
			configParam(param(i, FEEDBACK_PARAM), 0.f, 5.f, 0.f, name + "Distortion Feedback Gain");
			configParam(param(i, SHAPE_PARAM), 0.f, 1.f, 0.f, name + "Distortion Shape");

			configInput(input(i, FREQ_CV_INPUT), name + "Filter Frequency CV");
			configInput(input(i, Q_CV_INPUT), name + "Filter Q CV");
			configInput(input(i, FEEDBACK_CV_INPUT), name + "Distortion Feedback Gain CV");
			configInput(input(i, SHAPE_CV_INPUT), name + "Distortion Shape CV");

			configOutput(output(i, PATH_OUTPUT), name + "Signal");
		}
		configSwitch(SOLVER_PARAM, 0.f, 2.f, 0.f, "Feedback Loop Solver", {"Naive (unit delay)", "Safeguarded Newton (delay-free)", "Newton Zones (delay-free)"});

		configInput(SIGNAL_INPUT, "Signal");

		configOutput(MIX_OUTPUT, "Mix (average of both paths)");
		configOutput(ITERATIONS_OUTPUT, "Solver iterations (0V = none, 10V = max iterations or not converged)");
	}

	void onReset() override {
		for (auto& path : paths) {
			path.filter.reset();
		}
		naiveSolver.reset();
	}

	// Knob position plus CV, clamped to the knob's range
	float modulatedParam(const int path, const ParamId id, const InputId cvId) {
		auto* quantity = paramQuantities[param(path, id)];
		return clamp(params[param(path, id)].getValue() + inputs[input(path, cvId)].getVoltage(), quantity->getMinValue(), quantity->getMaxValue());
	}

	void updateFilter(const int i, const float sampleRate) {
		auto& path = paths[i];
		const auto freqParam = modulatedParam(i, FREQ_PARAM, FREQ_CV_INPUT);
		const auto qParam = modulatedParam(i, Q_PARAM, Q_CV_INPUT);
		const auto mode = (int) std::round(params[param(i, FILTER_MODE_PARAM)].getValue());
		if (freqParam == path.lastFreqParam && qParam == path.lastQParam && mode == path.lastMode && sampleRate == path.lastSampleRate) {
			return;
		}
		path.lastFreqParam = freqParam;
		path.lastQParam = qParam;
		path.lastMode = mode;
		path.lastSampleRate = sampleRate;

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
		path.filter.setParameters(type, normalizedFreq, q, 1.f);
	}

	void process(const ProcessArgs& args) override {
		float shape[PATHS];
		float gain[PATHS];
		for (int i = 0; i < PATHS; i++) {
			updateFilter(i, args.sampleRate);
			shape[i] = modulatedParam(i, SHAPE_PARAM, SHAPE_CV_INPUT);
			gain[i] = modulatedParam(i, FEEDBACK_PARAM, FEEDBACK_CV_INPUT);
		}
		const auto in = inputs[SIGNAL_INPUT].getVoltage();

		const DualFeedbackLoop loop(paths[0].filter, shape[0], gain[0], paths[1].filter, shape[1], gain[1], in, naiveSolver.previousFeedback);
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

		auto nextFeedback = 0.f;
		auto mix = 0.f;
		for (int i = 0; i < PATHS; i++) {
			const auto filtered = paths[i].filter.process(in + feedback);
			const auto distorted = FeedbackLoop::distort(filtered * 0.2f, shape[i]) * 5.f;
			nextFeedback += distorted * gain[i];
			mix += distorted;
			outputs[output(i, PATH_OUTPUT)].setVoltage(distorted);
		}
		naiveSolver.update(nextFeedback);
		outputs[MIX_OUTPUT].setVoltage(mix / PATHS);
		// Iteration count rescaled so the solver's max iterations is 10V. Stays at 0V in naive
		// mode, since there is nothing to solve.
		outputs[ITERATIONS_OUTPUT].setVoltage(10.f * iterations / FeedbackLoop::MAX_ITERATIONS);
	}
};


struct DualFilterDistortWidget : ModuleWidget {
	DualFilterDistortWidget(DualFilterDistort* module) {
		using M = DualFilterDistort;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/DualFilterDistort.svg")));

		// Each column has FilterDistort's layout, the second one 6 HP to the right
		for (int i = 0; i < M::PATHS; i++) {
			const auto x = 30.48f * i;
			addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(x + 15.24, 16.0)), module, M::param(i, M::FREQ_PARAM)));
			addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x + 9.0, 38.0)), module, M::param(i, M::Q_PARAM)));
			addParam(createParamCentered<CKSSThree>(mm2px(Vec(x + 21.48, 43.5)), module, M::param(i, M::FILTER_MODE_PARAM)));
			addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x + 9.0, 62.0)), module, M::param(i, M::FEEDBACK_PARAM)));
			addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x + 21.48, 62.0)), module, M::param(i, M::SHAPE_PARAM)));

			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 15.24, 27.0)), module, M::input(i, M::FREQ_CV_INPUT)));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 9.0, 49.0)), module, M::input(i, M::Q_CV_INPUT)));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 9.0, 73.0)), module, M::input(i, M::FEEDBACK_CV_INPUT)));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x + 21.48, 73.0)), module, M::input(i, M::SHAPE_CV_INPUT)));

			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x + 15.24, 100.0)), module, M::output(i, M::PATH_OUTPUT)));
		}

		// Shared controls down the middle, between the columns
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(30.48, 88.0)), module, M::SOLVER_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(30.48, 100.0)), module, M::ITERATIONS_OUTPUT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.24, 111.948)), module, M::SIGNAL_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(45.72, 111.948)), module, M::MIX_OUTPUT));
	}
};


Model* modelDualFilterDistort = createModel<DualFilterDistort, DualFilterDistortWidget>("DualFilterDistort");
