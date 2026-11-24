#pragma once
#include "DualFeedbackLoop.hpp"
#include "SafeguardedNewtonSolver.hpp"

// Newton Zones for the dual loop: h is monotonic between its extrema, so they split the bracket into
// zones that each hold at most one root. Searches outward from the zone holding the previous
// solution for the nearest one whose ends have opposite signs, then runs safeguarded Newton inside
// it, so it keeps following the previous solution's root until it disappears.
//
// Unlike NewtonZonesSolver, the extrema can't be cached between samples: each path's filter output
// is a_i * u + c_i, and c_i moves with the input and the filter state, shifting the two paths'
// terms of h' relative to each other. Instead the extrema are found lazily, scanning outward from
// the previous solution only as far as the zigzag search reaches, which is usually just the zone
// it's in.
struct DualNewtonZonesSolver {
	// Smallest scan step, as a distance at distort()'s input (0.5 in y, as in NewtonZonesSolver).
	// A pair of extrema closer together than this can be missed, which only merges their zones.
	static constexpr float MIN_SCAN_STEP = 0.1f;
	// Total h' evaluations the scan may spend in one solve. Past this, a scan stops at the bracket
	// end, which only merges the remaining zones on that side.
	static constexpr int MAX_SCAN_STEPS = 256;

	// Returns the signal to feed back to the filters' inputs, which isn't finite if the input isn't.
	// Sets iterations to the number of h evaluations at zone ends plus Newton's iterations, or to
	// FeedbackLoop::MAX_ITERATIONS if it ran out of iterations. The scan for extrema isn't included.
	float solve(const DualFeedbackLoop& loop, int& iterations) const {
		return loop.feedback(findRoot(loop, iterations));
	}

private:
	struct Scan {
		const DualFeedbackLoop& loop;
		float minStep;
		float bound;
		int stepsLeft = MAX_SCAN_STEPS;

		explicit Scan(const DualFeedbackLoop& loop) : loop(loop) {
			const auto maxA = std::max(std::abs(loop.a[0]), std::abs(loop.a[1]));
			// MIN_SCAN_STEP at distort()'s input is 5 * MIN_SCAN_STEP in y, so 5 * MIN_SCAN_STEP / |a|
			// in u for the path whose distortion moves fastest
			minStep = 5.f * MIN_SCAN_STEP / std::max(maxA, 1e-6f);
			bound = loop.secondDerivativeBound();
		}

		// Walks from `from` towards `limit` (direction given by their order) and returns the first
		// extremum of h, placed by linear interpolation between scan points, or `limit` if there's
		// none before it. If `from` is itself an extremum, the scan starts one min step past it,
		// so that h' there (close to 0, of either sign) can't count as a sign change.
		float nextExtremum(const float from, const float limit, const bool fromIsExtremum) {
			const auto direction = limit > from ? 1.f : -1.f;
			const auto distanceTo = [&](const float y) {
				return (limit - y) * direction;
			};
			auto y = from;
			if (fromIsExtremum) {
				y += direction * minStep;
			}
			if (distanceTo(y) <= 0.f) {
				return limit;
			}
			auto dh = loop.residualDerivative(y);
			while (stepsLeft > 0) {
				stepsLeft--;
				// h' can't reach zero within |h'| / bound, so steep stretches go by in one step
				const auto step = std::max(minStep, bound > 0.f ? std::abs(dh) / bound : INFINITY);
				if (step >= distanceTo(y)) {
					const auto dhLimit = loop.residualDerivative(limit);
					if ((dh < 0.f) != (dhLimit < 0.f)) {
						return y + (limit - y) * dh / (dh - dhLimit);
					}
					return limit;
				}
				const auto next = y + direction * step;
				const auto dhNext = loop.residualDerivative(next);
				if ((dh < 0.f) != (dhNext < 0.f)) {
					return y + (next - y) * dh / (dh - dhNext);
				}
				y = next;
				dh = dhNext;
			}
			return limit;
		}
	};

	static float findRoot(const DualFeedbackLoop& loop, int& iterations) {
		iterations = FeedbackLoop::MAX_ITERATIONS;
		float lo, hi;
		loop.bracket(lo, hi);
		if (!std::isfinite(lo) || !std::isfinite(hi)) {
			return NAN;
		}
		auto previous = loop.previous();
		previous = std::isfinite(previous) ? clamp(previous, lo, hi) : lo;

		// With no extrema the whole bracket is one rising zone
		if (loop.monotonic()) {
			return SafeguardedNewtonSolver::findRootInBracket(loop, lo, hi, previous, true, FeedbackLoop::MAX_ITERATIONS, iterations);
		}

		Scan scan(loop);
		int evaluations = 0;
		const auto valueAt = [&](const float y) {
			evaluations++;
			return loop.residual(y);
		};

		// Checks the zone [start, end]: returns true and sets root if h has a root at an end or
		// crosses zero inside it
		float root = previous;
		const auto check = [&](const float start, const float hStart, const float end, const float hEnd) {
			if (std::abs(hStart) < FeedbackLoop::TOLERANCE) {
				root = start;
				iterations = evaluations;
				return true;
			}
			if (std::abs(hEnd) < FeedbackLoop::TOLERANCE) {
				root = end;
				iterations = evaluations;
				return true;
			}
			if ((hStart > 0.f) == (hEnd > 0.f)) {
				return false;
			}
			int newtonIterations;
			// Always allow Newton at least one iteration, even if the zone ends used up the budget
			const int maxIterations = std::max(FeedbackLoop::MAX_ITERATIONS - evaluations, 1);
			root = SafeguardedNewtonSolver::findRootInBracket(loop, start, end, clamp(previous, start, end), hStart < 0.f, maxIterations, newtonIterations);
			const int maxReported = FeedbackLoop::MAX_ITERATIONS;
			iterations = std::min(evaluations + newtonIterations, maxReported);
			return true;
		};

		// The zone holding the previous solution
		auto lower = scan.nextExtremum(previous, lo, false);
		auto upper = scan.nextExtremum(previous, hi, false);
		auto hLower = valueAt(lower);
		auto hUpper = valueAt(upper);
		if (check(lower, hLower, upper, hUpper)) {
			return root;
		}

		// Zigzag out from it, lower neighbor first. h(lo) <= 0 <= h(hi), so some zone has to cross
		// zero unless h itself isn't finite.
		while (lower > lo || upper < hi) {
			if (lower > lo) {
				const auto next = scan.nextExtremum(lower, lo, true);
				const auto hNext = valueAt(next);
				if (check(next, hNext, lower, hLower)) {
					return root;
				}
				lower = next;
				hLower = hNext;
			}
			if (upper < hi) {
				const auto next = scan.nextExtremum(upper, hi, true);
				const auto hNext = valueAt(next);
				if (check(upper, hUpper, next, hNext)) {
					return root;
				}
				upper = next;
				hUpper = hNext;
			}
		}
		return previous;
	}
};
