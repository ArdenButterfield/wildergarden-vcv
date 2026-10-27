"""
Newton-Raphson with a bisection fallback for the FilterDistort delay-free loop.

Same loop and float32 arithmetic as newton_raphson.py:
    h(y) = y - b0 * (in + g * D(y)) - s = 0
starting from the previous output y0.

Runs plain Newton-Raphson, but keeps track of a bracket: the most recent evaluated points
where h(y) < 0 and h(y) > 0. Once both signs have been seen, a zero crossing is known to lie
between them, and each new Newton-Raphson iterate that lands inside the bracket tightens it.

The failing case in newton_raphson.png has the iterates oscillating around a zero crossing
(so a bracket exists), then jumping out of it, far below the initial guess. Whenever a
Newton-Raphson step would leave the bracket, have a vanishing derivative or give a non-finite
value while a bracket is known, the solver switches to binary search (bisection) on the
bracket for the rest of that sample. Bisection always converges, halving the bracket each
iteration until it is narrower than the tolerance.

If no bracket has been found yet, a failing step behaves exactly like plain Newton-Raphson.

Usage:
    py newton_bisection.py [--shape 0.25] [--q 0.707] [--mode lowpass] [--show]
"""

from pathlib import Path

import numpy as np

from newton_raphson import F32, DH_EPSILON, NR_TOLERANCE, Solver, distort_derivative, h, run

# Bisection needs about log2(bracket width / tolerance) iterations: ~24 for a 150 V bracket at
# 1e-5, on top of the Newton-Raphson steps taken before switching
HYBRID_MAX_ITERATIONS = 48
HYBRID_TOLERANCE = NR_TOLERANCE


def newton_bisection(y0, inp, gain, shape, b0, s, trace=None, max_iterations=HYBRID_MAX_ITERATIONS):
    """Newton-Raphson that falls back to bisection when a step leaves the known bracket.

    Vectorised like newton_raphson(). Each iteration is one evaluation of h. Returns
    (y, iterations, last_step), where iterations is the count needed to converge, or
    max_iterations if not converged, and last_step is the last Newton-Raphson step size
    or bracket width. If trace is a list, (iterate, is_bisection_step) is appended to it after
    each step.

    Converges when a Newton-Raphson step is smaller than the tolerance, h is exactly 0, or (in
    bisection) the bracket is narrower than the tolerance or can't be split further in float32.
    """
    y = np.array(y0, dtype=F32, copy=True)
    iterations = np.full(y.shape, max_iterations, dtype=np.int32)
    last_step = np.zeros(y.shape, dtype=F32)
    done = np.zeros(y.shape, dtype=bool)
    bisecting = np.zeros(y.shape, dtype=bool)
    # Bracket endpoints: the latest points with h < 0 and h > 0 (NaN until seen)
    neg_y = np.full(y.shape, np.nan, dtype=F32)
    pos_y = np.full(y.shape, np.nan, dtype=F32)
    if trace is not None:
        trace.append((y.copy(), bisecting.copy()))

    def bracket():
        have = np.isfinite(neg_y) & np.isfinite(pos_y)
        lo = np.fmin(neg_y, pos_y)
        hi = np.fmax(neg_y, pos_y)
        return have, lo, hi

    with np.errstate(all="ignore"):
        for i in range(max_iterations):
            active = ~done
            hy = h(y, inp, gain, shape, b0, s)

            # Record y as a bracket endpoint. Before a bracket exists any point counts; after,
            # only points inside it, so the bracket only ever tightens.
            have, lo, hi = bracket()
            inside = ~have | ((y >= lo) & (y <= hi))
            record = active & np.isfinite(hy) & inside
            neg_y = np.where(record & (hy < 0), y, neg_y)
            pos_y = np.where(record & (hy > 0), y, pos_y)

            exact = active & (hy == 0)
            iterations[exact] = i + 1
            last_step[exact] = 0
            done |= exact
            active &= ~exact

            have, lo, hi = bracket()
            width = hi - lo
            mid = lo + (hi - lo) * F32(0.5)

            # Newton-Raphson step, for instances not yet bisecting
            newton = active & ~bisecting
            dh = F32(1) - b0 * gain * distort_derivative(y * F32(0.2), shape)
            vanished = np.abs(dh) < DH_EPSILON
            step = hy / np.where(vanished, F32(1), dh)
            y_next = y - step
            leaves = have & ((y_next < lo) | (y_next > hi))
            failing = vanished | ~np.isfinite(y_next) | leaves

            # Failing step with a bracket: switch to bisection, starting at the bracket midpoint
            switch = newton & failing & have
            bisecting |= switch

            # Failing step without a bracket: same as plain Newton-Raphson, which stops on a
            # vanishing derivative and otherwise carries on from wherever the step lands
            stop = newton & ~have & vanished
            last_step[stop] = np.inf
            done |= stop

            take = newton & ~switch & ~stop
            y = np.where(take, y_next, y)
            last_step = np.where(take, np.abs(step), last_step)
            newton_converged = take & (np.abs(step) < HYBRID_TOLERANCE)
            iterations[newton_converged] = i + 1
            done |= newton_converged

            # Bisection: evaluate the midpoint next iteration, or stop once the bracket is
            # narrower than the tolerance or can't be split any further in float32
            bisect = active & bisecting
            y = np.where(bisect, mid, y)
            last_step = np.where(bisect, width, last_step)
            bisect_converged = bisect & ((width < HYBRID_TOLERANCE) | (mid == lo) | (mid == hi))
            iterations[bisect_converged] = i + 1
            done |= bisect_converged

            if trace is not None:
                trace.append((y.copy(), bisect.copy()))
            if done.all():
                break

    iterations[~np.isfinite(y)] = max_iterations
    return y, iterations, last_step


NEWTON_BISECTION = Solver("Newton-Raphson + bisection", newton_bisection, HYBRID_MAX_ITERATIONS, HYBRID_TOLERANCE)


if __name__ == "__main__":
    run(NEWTON_BISECTION, __doc__, Path(__file__).with_name("newton_bisection.png"), show_own_failures=True)
