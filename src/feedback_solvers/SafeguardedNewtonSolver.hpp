#pragma once
#include "FeedbackLoop.hpp"

struct SafeguardedNewtonSolver {
	static constexpr float DH_EPSILON = 1e-9f;

	// Safeguarded Newton (Numerical Recipes' rtsafe) on a bracket that h crosses zero in, rising if
	// rising is set and falling otherwise: Newton-Raphson from y, kept inside the bracket, which is
	// narrowed by the sign of h at every iterate. A step is replaced by bisection if it would leave
	// the bracket, the derivative vanishes, or it isn't at least halving compared with the step
	// before last, so it always converges.
	// Sets iterations to the number of h and h' evaluations needed for the step or the bracket to
	// fall below FeedbackLoop::TOLERANCE, or to maxIterations if it ran out of iterations.
	// Loop is FeedbackLoop or DualFeedbackLoop: anything with residual() and residualDerivative().
	template <typename Loop>
	static float findRootInBracket(const Loop& loop, float lo, float hi, float y, const bool rising, const int maxIterations, int& iterations) {
		iterations = maxIterations;
		auto dx = hi - lo;
		auto dxOld = dx;
		for (int i = 0; i < maxIterations; i++) {
			const auto h = loop.residual(y);
			if (h == 0.f) {
				iterations = i + 1;
				break;
			}
			if ((h < 0.f) == rising) {
				lo = y;
			}
			else {
				hi = y;
			}
			const auto dh = loop.residualDerivative(y);

			auto next = y;
			bool bisect = std::abs(dh) < DH_EPSILON;
			if (!bisect) {
				next = y - h / dh;
				// y has just become an end of the bracket, so a converged step that rounds back onto
				// it mustn't count as leaving the bracket
				bisect = !std::isfinite(next) || (std::abs(next - y) >= FeedbackLoop::TOLERANCE && (next <= lo || next >= hi || std::abs(2.f * h) > std::abs(dxOld * dh)));
			}
			if (bisect) {
				next = lo + (hi - lo) * 0.5f;
			}
			const auto step = std::abs(next - y);
			dxOld = dx;
			dx = step;
			y = next;
			if (step < FeedbackLoop::TOLERANCE || hi - lo < FeedbackLoop::TOLERANCE) {
				iterations = i + 1;
				break;
			}
		}
		return y;
	}

	// Safeguarded Newton on the whole bracket, starting from the previous solution (loop.previous()),
	// which is close to the solution for audio-rate signals.
	// Returns the signal to feed back to the filter's input, which isn't finite if the input isn't.
	// Sets iterations as findRootInBracket() does.
	template <typename Loop>
	float solve(const Loop& loop, int& iterations) const {
		float lo, hi;
		loop.bracket(lo, hi);
		const auto previous = loop.previous();
		const auto start = std::isfinite(previous) ? clamp(previous, lo, hi) : lo;
		return loop.feedback(findRootInBracket(loop, lo, hi, start, true, FeedbackLoop::MAX_ITERATIONS, iterations));
	}
};
