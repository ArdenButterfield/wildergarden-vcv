#pragma once
#include "FeedbackLoop.hpp"

// Breaks the delay-free loop with a unit delay: feeds back what the previous sample fed back,
// so there is nothing to solve.
struct NaiveSolver {
	float previousFeedback = 0.f;

	void reset() {
		previousFeedback = 0.f;
	}

	// Call once per sample with the distorted output scaled by the feedback gain
	void update(const float feedback) {
		previousFeedback = feedback;
	}

	// Returns the signal to feed back to the filter's input. Sets iterations to 0.
	// Works with any loop (FeedbackLoop or DualFeedbackLoop), as it never looks at it.
	template <typename Loop>
	float solve(const Loop& loop, int& iterations) const {
		iterations = 0;
		return previousFeedback;
	}
};
