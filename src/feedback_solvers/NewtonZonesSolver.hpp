#pragma once
#include "FeedbackLoop.hpp"
#include "SafeguardedNewtonSolver.hpp"

// Newton Zones: h is monotonic between its extrema, so they split the bracket into zones that each
// hold at most one root. Searches outward from the zone holding the previous output for the nearest
// one whose ends have opposite signs, then runs safeguarded Newton inside it. This keeps following
// the previous output's root until it disappears, rather than letting Newton jump to another one.
struct NewtonZonesSolver {
	// The scan for h's extrema evaluates h' at this spacing in y (0.1 at distort()'s input). A pair
	// of extrema closer together than this can be missed, which only merges their zones.
	static constexpr float SCAN_STEP = 0.5f;
	// How far the scan reaches past the bracket (pi at distort()'s input), so that the extrema can
	// be reused while the bracket moves around
	static constexpr float SCAN_MARGIN = 15.7f;
	// The bracket and margin span at most about 16 radians at distort()'s input, with far fewer
	// extrema than this. Any past this limit are ignored, which only merges the last zones.
	static constexpr int MAX_EXTREMA = 16;

	// Extrema of h found by the last scan, in ascending order, and what they cover
	float extrema[MAX_EXTREMA];
	int extremaCount = 0;
	float scanKappa = 0.f;
	float scanShape = 0.f;
	float scanLo = INFINITY;
	float scanHi = -INFINITY;

	// Returns the signal to feed back to the filter's input, which isn't finite if the input isn't.
	// Sets iterations to the number of h evaluations at zone ends plus Newton's iterations, or to
	// FeedbackLoop::MAX_ITERATIONS if it ran out of iterations. The scan for extrema isn't included.
	float solve(const FeedbackLoop& loop, int& iterations) {
		return loop.feedback(findRoot(loop, iterations));
	}

private:
	// Finds the extrema of h in the bracket, where h'(y) = 1 - kappa * distort'(0.2 * y) = 0, unless
	// the last scan still covers it. They depend only on kappa and shape, not on the input or the
	// filter state, which only move the bracket. |distort'| <= 1, so with |kappa| <= 1 h has no
	// extrema to look for. Extrema are placed by linear interpolation between scan points; being
	// slightly off is harmless, as the solver still brackets a root in whichever zone it searches.
	void updateExtrema(const FeedbackLoop& loop, const float lo, const float hi) {
		if (loop.kappa == scanKappa && loop.shape == scanShape && lo >= scanLo && hi <= scanHi) {
			return;
		}
		scanKappa = loop.kappa;
		scanShape = loop.shape;
		extremaCount = 0;
		if (std::abs(loop.kappa) <= 1.f) {
			scanLo = -INFINITY;
			scanHi = INFINITY;
			return;
		}
		scanLo = lo - SCAN_MARGIN;
		scanHi = hi + SCAN_MARGIN;
		const auto steps = (int) std::ceil((scanHi - scanLo) / SCAN_STEP);
		auto prevY = scanLo;
		auto prevDh = loop.residualDerivative(prevY);
		for (int i = 1; i <= steps && extremaCount < MAX_EXTREMA; i++) {
			const auto y = scanLo + i * SCAN_STEP;
			const auto dh = loop.residualDerivative(y);
			if ((prevDh < 0.f) != (dh < 0.f)) {
				extrema[extremaCount++] = prevY + (y - prevY) * prevDh / (prevDh - dh);
			}
			prevY = y;
			prevDh = dh;
		}
	}

	float findRoot(const FeedbackLoop& loop, int& iterations) {
		iterations = FeedbackLoop::MAX_ITERATIONS;
		float lo, hi;
		loop.bracket(lo, hi);
		if (!std::isfinite(lo) || !std::isfinite(hi)) {
			return NAN;
		}
		updateExtrema(loop, lo, hi);

		float ends[MAX_EXTREMA + 2];
		int zones = 0;
		ends[0] = lo;
		for (int i = 0; i < extremaCount; i++) {
			if (extrema[i] > lo && extrema[i] < hi) {
				ends[++zones] = extrema[i];
			}
		}
		ends[++zones] = hi;

		// h at each zone end, evaluated when first needed. Neighboring zones share an end.
		float endValues[MAX_EXTREMA + 2];
		std::fill(endValues, endValues + zones + 1, NAN);
		int evaluations = 0;
		const auto valueAt = [&](const int end) {
			if (std::isnan(endValues[end])) {
				endValues[end] = loop.residual(ends[end]);
				evaluations++;
			}
			return endValues[end];
		};

		auto previous = loop.filter.y[0];
		previous = std::isfinite(previous) ? clamp(previous, lo, hi) : lo;
		int previousZone = 0;
		while (previousZone < zones - 1 && previous > ends[previousZone + 1]) {
			previousZone++;
		}

		// Zigzag out from the previous zone, lower neighbor first
		for (int distance = 0; distance < zones; distance++) {
			for (const int zone : {previousZone - distance, previousZone + distance}) {
				if (zone < 0 || zone >= zones) {
					continue;
				}
				const auto start = valueAt(zone);
				if (std::abs(start) < FeedbackLoop::TOLERANCE) {
					iterations = evaluations;
					return ends[zone];
				}
				const auto end = valueAt(zone + 1);
				if (std::abs(end) < FeedbackLoop::TOLERANCE) {
					iterations = evaluations;
					return ends[zone + 1];
				}
				if ((start > 0.f) == (end > 0.f)) {
					continue;
				}
				int newtonIterations;
				const auto y = SafeguardedNewtonSolver::findRootInBracket(loop, ends[zone], ends[zone + 1], clamp(previous, ends[zone], ends[zone + 1]), start < 0.f, FeedbackLoop::MAX_ITERATIONS - evaluations, newtonIterations);
				iterations = evaluations + newtonIterations;
				return y;
			}
		}
		// h(lo) <= 0 <= h(hi), so some zone has to cross zero unless h itself isn't finite
		return previous;
	}
};
