#pragma once
#include "../plugin.hpp"

// The delay-free loop for one sample: the filter output y satisfies y = b0 * (in + g * D(y)) + s,
// where D(y) = 5 * distort(0.2 * y) and s is the contribution of the filter's internal state.
// The delay-free solvers find a root of h(y) = y - b0 * (in + g * D(y)) - s.
struct FeedbackLoop {
	// Delay-free solver stopping criteria. 24 iterations covers safeguarded Newton's 99th
	// percentile with margin (analysis/comparison/REPORT.md).
	static constexpr int MAX_ITERATIONS = 24;
	static constexpr float TOLERANCE = 1e-5f;

	static float distort(const float in, const float shape) {
		auto a = sin(in + (shape - 0.5f) * M_PI);
		auto b = tanh(in);
		auto blend = std::min(shape * 2, 1.f);
		return blend * a + (1 - blend) * b;
	}

	// Derivative of distort() with respect to its input
	static float distortDerivative(const float in, const float shape) {
		auto a = cos(in + (shape - 0.5f) * M_PI);
		auto t = tanh(in);
		auto b = 1 - t * t;
		auto blend = std::min(shape * 2, 1.f);
		return blend * a + (1 - blend) * b;
	}

	const dsp::BiquadFilter& filter;
	const float in;
	const float shape;
	const float gain;
	// Contribution of the filter's internal state to this sample's output
	const float s;
	const float kappa;

	FeedbackLoop(const dsp::BiquadFilter& filter, const float in, const float shape, const float gain) :
		filter(filter),
		in(in),
		shape(shape),
		gain(gain),
		s(filter.b[1] * filter.x[0] + filter.b[2] * filter.x[1] - filter.a[0] * filter.y[0] - filter.a[1] * filter.y[1]),
		kappa(filter.b[0] * gain) {
	}

	float residual(const float y) const {
		return y - filter.b[0] * (in + gain * distort(y * 0.2f, shape) * 5.f) - s;
	}

	// h'(y). D'(y) = distort'(0.2 * y), as the 5 and 0.2 scalings cancel.
	float residualDerivative(const float y) const {
		return 1.f - kappa * distortDerivative(y * 0.2f, shape);
	}

	// Every root lies in [lo, hi]: |distort| <= 1, so |D| <= 5 and y is within 5 * |b0 * g| of
	// c = b0 * in + s, with h(lo) <= 0 and h(hi) >= 0. Widened slightly so float rounding can't
	// put a root just outside it.
	void bracket(float& lo, float& hi) const {
		const auto c = filter.b[0] * in + s;
		const auto r = 5.f * std::abs(filter.b[0] * gain) * 1.0001f + 1e-6f;
		lo = c - r;
		hi = c + r;
	}

	// What the distortion feeds back to the filter's input when the filter outputs y
	float feedback(const float y) const {
		return distort(y * 0.2f, shape) * 5.f * gain;
	}
};
