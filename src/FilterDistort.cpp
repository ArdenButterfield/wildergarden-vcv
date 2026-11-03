#include "plugin.hpp"

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
		ITERATIONS_OUTPUT,
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

	// Delay-free solver stopping criteria, shared by both solvers. 24 iterations covers safeguarded
	// Newton's 99th percentile with margin (analysis/comparison/REPORT.md).
	static constexpr int SOLVER_MAX_ITERATIONS = 24;
	static constexpr float SOLVER_TOLERANCE = 1e-5f;
	static constexpr float DH_EPSILON = 1e-9f;

	enum Solver {
		SOLVER_NAIVE,
		SOLVER_SAFEGUARDED_NEWTON,
		SOLVER_ILLINOIS
	};

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
		configSwitch(SOLVER_PARAM, 0.f, 2.f, 0.f, "Feedback Loop Solver", {"Naive (unit delay)", "Safeguarded Newton (delay-free)", "Illinois (delay-free)"});

		configInput(SIGNAL_INPUT, "Signal");
		configInput(FREQ_CV_INPUT, "Filter Frequency CV");
		configInput(Q_CV_INPUT, "Filter Q CV");
		configInput(FEEDBACK_CV_INPUT, "Distortion Feedback Gain CV");
		configInput(SHAPE_CV_INPUT, "Distortion Shape CV");

		configOutput(SIGNAL_OUTPUT, "Signal");
		configOutput(ITERATIONS_OUTPUT, "Solver iterations (0V = none, 10V = max iterations or not converged)");
	}

	void onReset() override {
		filter.reset();
		feedbackState = 0;
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

	float distort(const float in, const float shape) {
		auto a = sin(in + (shape - 0.5f) * M_PI);
		auto b = tanh(in);
		auto blend = std::min(shape * 2, 1.f);
		return blend * a + (1 - blend) * b;
	}

	// Derivative of distort() with respect to its input
	float distortDerivative(const float in, const float shape) {
		auto a = cos(in + (shape - 0.5f) * M_PI);
		auto t = tanh(in);
		auto b = 1 - t * t;
		auto blend = std::min(shape * 2, 1.f);
		return blend * a + (1 - blend) * b;
	}

	// The delay-free loop: the filter output y satisfies y = b0 * (in + g * D(y)) + s, where
	// D(y) = 5 * distort(0.2 * y) and s is the contribution of the filter's internal state.
	// Both solvers find a root of h(y) = y - b0 * (in + g * D(y)) - s.

	// Contribution of the filter's internal state to this sample's output
	float filterStateContribution() {
		const auto& b = filter.b;
		const auto& a = filter.a;
		return b[1] * filter.x[0] + b[2] * filter.x[1] - a[0] * filter.y[0] - a[1] * filter.y[1];
	}

	float loopResidual(const float y, const float in, const float s, const float shape, const float gain) {
		return y - filter.b[0] * (in + gain * distort(y * 0.2f, shape) * 5.f) - s;
	}

	// Every root lies in [lo, hi]: |distort| <= 1, so |D| <= 5 and y is within 5 * |b0 * g| of
	// c = b0 * in + s, with h(lo) <= 0 and h(hi) >= 0. Widened slightly so float rounding can't
	// put a root just outside it.
	void loopBracket(const float in, const float s, const float gain, float& lo, float& hi) {
		const auto c = filter.b[0] * in + s;
		const auto r = 5.f * std::abs(filter.b[0] * gain) * 1.0001f + 1e-6f;
		lo = c - r;
		hi = c + r;
	}

	// Safeguarded Newton (Numerical Recipes' rtsafe): Newton-Raphson from the previous output,
	// kept inside the bracket, which is narrowed by the sign of h at every iterate. A step is
	// replaced by bisection if it would leave the bracket, the derivative vanishes, or it isn't
	// at least halving compared with the step before last, so it always converges.
	// Sets iterations to the number of h and h' evaluations needed for the step or the bracket
	// to fall below SOLVER_TOLERANCE, or to SOLVER_MAX_ITERATIONS if it ran out of iterations.
	float solveSafeguardedNewton(const float in, const float shape, const float gain, int& iterations) {
		iterations = SOLVER_MAX_ITERATIONS;
		const auto s = filterStateContribution();
		const auto kappa = filter.b[0] * gain;
		float lo, hi;
		loopBracket(in, s, gain, lo, hi);

		// Start from the previous output, which is close to the solution for audio-rate signals
		auto y = filter.y[0];
		y = std::isfinite(y) ? clamp(y, lo, hi) : lo;
		auto dx = hi - lo;
		auto dxOld = dx;
		for (int i = 0; i < SOLVER_MAX_ITERATIONS; i++) {
			const auto h = loopResidual(y, in, s, shape, gain);
			if (h == 0.f) {
				iterations = i + 1;
				break;
			}
			if (h < 0.f) {
				lo = y;
			}
			else {
				hi = y;
			}
			// D'(y) = distort'(0.2 * y), as the 5 and 0.2 scalings cancel
			const auto dh = 1.f - kappa * distortDerivative(y * 0.2f, shape);

			auto next = y;
			bool bisect = std::abs(dh) < DH_EPSILON;
			if (!bisect) {
				next = y - h / dh;
				bisect = !std::isfinite(next) || next <= lo || next >= hi || std::abs(2.f * h) > std::abs(dxOld * dh);
			}
			if (bisect) {
				next = lo + (hi - lo) * 0.5f;
			}
			const auto step = std::abs(next - y);
			dxOld = dx;
			dx = step;
			y = next;
			if (step < SOLVER_TOLERANCE || hi - lo < SOLVER_TOLERANCE) {
				iterations = i + 1;
				break;
			}
		}
		return y;
	}

	// Illinois (modified regula falsi) on the bracket. Evaluates both bracket ends, then the
	// previous output, which usually narrows the bracket a lot, then takes false-position steps,
	// halving the stale end's value whenever the same end moves twice in a row. Needs no
	// derivative and always converges, but doesn't follow the previous output as closely as
	// Newton, so it more often switches between roots when there are several.
	// Sets iterations to the number of h evaluations needed for the step or the bracket to fall
	// below SOLVER_TOLERANCE, or to SOLVER_MAX_ITERATIONS if it ran out of iterations.
	float solveIllinois(const float in, const float shape, const float gain, int& iterations) {
		iterations = SOLVER_MAX_ITERATIONS;
		const auto s = filterStateContribution();
		float lo, hi;
		loopBracket(in, s, gain, lo, hi);

		auto fLo = loopResidual(lo, in, s, shape, gain);
		if (fLo == 0.f) {
			iterations = 1;
			return lo;
		}
		auto fHi = loopResidual(hi, in, s, shape, gain);
		if (fHi == 0.f) {
			iterations = 2;
			return hi;
		}
		auto x = filter.y[0];
		x = std::isfinite(x) ? clamp(x, lo, hi) : lo;
		auto fx = loopResidual(x, in, s, shape, gain);
		if (fx == 0.f) {
			iterations = 3;
			return x;
		}
		const int setupEvaluations = 3;

		// Which end moved last: -1 for lo, 1 for hi, 0 for neither yet
		int side = 0;
		auto moveEnd = [&](const float x, const float fx) {
			if (fx < 0.f) {
				lo = x;
				fLo = fx;
				if (side == -1) {
					fHi *= 0.5f;
				}
				side = -1;
			}
			else if (fx > 0.f) {
				hi = x;
				fHi = fx;
				if (side == 1) {
					fLo *= 0.5f;
				}
				side = 1;
			}
		};
		moveEnd(x, fx);
		// The initial guess narrows the bracket but doesn't count as a false-position step
		side = 0;

		for (int i = 0; i < SOLVER_MAX_ITERATIONS - setupEvaluations; i++) {
			const auto denom = fHi - fLo;
			auto next = denom != 0.f ? (lo * fHi - hi * fLo) / denom : lo + (hi - lo) * 0.5f;
			if (!std::isfinite(next) || next < lo || next > hi) {
				next = lo + (hi - lo) * 0.5f;
			}
			fx = loopResidual(next, in, s, shape, gain);
			const auto step = std::abs(next - x);
			x = next;
			moveEnd(x, fx);
			if (fx == 0.f || step < SOLVER_TOLERANCE || hi - lo < SOLVER_TOLERANCE) {
				iterations = setupEvaluations + i + 1;
				break;
			}
		}
		return x;
	}

	void process(const ProcessArgs& args) override {
		updateFilter(args.sampleRate);
		const auto input = inputs[SIGNAL_INPUT].getVoltage();
		const auto shapeParam = modulatedParam(SHAPE_PARAM, SHAPE_CV_INPUT);
		const auto feedbackParam = modulatedParam(FEEDBACK_PARAM, FEEDBACK_CV_INPUT);

		float filterIn = input + feedbackState;
		// Iteration count rescaled so the solver's max iterations is 10V. Stays at 0V in naive
		// mode, since there is nothing to solve.
		float iterationsVoltage = 0.f;
		const auto solver = (int) std::round(params[SOLVER_PARAM].getValue());
		if (solver == SOLVER_SAFEGUARDED_NEWTON || solver == SOLVER_ILLINOIS) {
			int iterations;
			const auto y = solver == SOLVER_SAFEGUARDED_NEWTON
				? solveSafeguardedNewton(input, shapeParam, feedbackParam, iterations)
				: solveIllinois(input, shapeParam, feedbackParam, iterations);
			iterationsVoltage = 10.f * iterations / SOLVER_MAX_ITERATIONS;
			// Both solvers stay inside a finite bracket, so this only falls back to the
			// unit-delay loop if the input itself is not finite
			if (std::isfinite(y)) {
				filterIn = input + distort(y * 0.2f, shapeParam) * 5.f * feedbackParam;
			}
		}

		auto filtered = filter.process(filterIn);
		auto distorted = distort(filtered * 0.2f, shapeParam) * 5.f;
		feedbackState = distorted * feedbackParam;
		outputs[SIGNAL_OUTPUT].setVoltage(distorted);
		outputs[ITERATIONS_OUTPUT].setVoltage(iterationsVoltage);
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
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(15.24, 100.0)), module, FilterDistort::ITERATIONS_OUTPUT));
	}
};


Model* modelFilterDistort = createModel<FilterDistort, FilterDistortWidget>("FilterDistort");
