#pragma once
#include "FeedbackLoop.hpp"

// The delay-free loop for one sample of two parallel filter -> distortion paths whose distorted
// outputs are summed and fed back into the input of both filters. Path i's filter outputs
// y_i(u) = a_i * u + c_i when the summed feedback is u, where a_i = b0_i and c_i = b0_i * in + s_i
// (s_i is the contribution of the filter's internal state), and feeds back
// f_i(u) = g_i * D_i(y_i(u)) with D_i(y) = 5 * distort(0.2 * y, shape_i).
// The delay-free solvers find a root of h(u) = u - f_1(u) - f_2(u), i.e.
// f_1(a_1 u + c_1) + f_2(a_2 u + c_2) - u = 0 with the sign flipped so that h rises through roots
// at the ends of the bracket.
struct DualFeedbackLoop {
	static constexpr int PATHS = 2;

	const FeedbackLoop paths[PATHS];
	// Filter output = a * u + c for each path
	float a[PATHS];
	float c[PATHS];
	// The summed feedback from the previous sample, where the solvers start
	const float previousSum;

	DualFeedbackLoop(const dsp::BiquadFilter& filter1, const float shape1, const float gain1,
	                 const dsp::BiquadFilter& filter2, const float shape2, const float gain2,
	                 const float in, const float previousSum) :
		paths{FeedbackLoop(filter1, in, shape1, gain1), FeedbackLoop(filter2, in, shape2, gain2)},
		previousSum(previousSum) {
		for (int i = 0; i < PATHS; i++) {
			a[i] = paths[i].filter.b[0];
			c[i] = paths[i].filter.b[0] * in + paths[i].s;
		}
	}

	// What path i's filter outputs when the summed feedback is u
	float pathOutput(const int i, const float u) const {
		return a[i] * u + c[i];
	}

	float residual(const float u) const {
		auto h = u;
		for (int i = 0; i < PATHS; i++) {
			h -= paths[i].feedback(pathOutput(i, u));
		}
		return h;
	}

	// h'(u) = 1 - sum of g_i * a_i * distort'(0.2 * y_i(u)), as the 5 and 0.2 scalings cancel
	float residualDerivative(const float u) const {
		auto dh = 1.f;
		for (int i = 0; i < PATHS; i++) {
			dh -= paths[i].kappa * FeedbackLoop::distortDerivative(pathOutput(i, u) * 0.2f, paths[i].shape);
		}
		return dh;
	}

	// |distort'| <= 1, so h' >= 1 - sum of |g_i * a_i|, and h has no extrema (one root) when that
	// sum is at most 1
	bool monotonic() const {
		return std::abs(paths[0].kappa) + std::abs(paths[1].kappa) <= 1.f;
	}

	// Bound on |h''|: |distort''| <= 1 for every shape (sin'' and the tanh blend both stay within
	// it), so |h''(u)| <= sum of 0.2 * |g_i| * a_i^2. h' can't reach zero within |h'(u)| / bound
	// of u, which lets the scan for extrema take large steps where h is steep.
	float secondDerivativeBound() const {
		auto bound = 0.f;
		for (int i = 0; i < PATHS; i++) {
			bound += 0.2f * std::abs(paths[i].gain) * a[i] * a[i];
		}
		return bound;
	}

	// Every root lies in [lo, hi]: |distort| <= 1, so |f_i| <= 5 * |g_i| and u is within
	// 5 * (|g_1| + |g_2|) of 0, with h(lo) <= 0 and h(hi) >= 0. Widened slightly so float rounding
	// can't put a root just outside it.
	void bracket(float& lo, float& hi) const {
		const auto r = 5.f * (std::abs(paths[0].gain) + std::abs(paths[1].gain)) * 1.0001f + 1e-6f;
		lo = -r;
		hi = r;
		// A non-finite input leaves the bracket finite, so report it through the bracket like
		// FeedbackLoop does
		if (!std::isfinite(c[0]) || !std::isfinite(c[1])) {
			lo = NAN;
			hi = NAN;
		}
	}

	// Where the solvers start
	float previous() const {
		return previousSum;
	}

	// The root is the summed signal fed back to both filters' inputs
	float feedback(const float u) const {
		return u;
	}
};
