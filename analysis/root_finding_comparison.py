"""
Compare root-finding methods for the FilterDistort delay-free loop.

Every method solves the same equation as newton_raphson.py, in float32:
    h(y) = y - b0 * (in + g * D(y)) - s = 0,   D(y) = 5 * distort(0.2 * y, shape)

Methods (all share one iteration budget and the module's 1e-5 tolerance):
  bisection           binary search on the a-priori bracket (below); ignores the initial guess
  fixed-point         Avanzini et al. (2005) fig. 3(b), as FilterDistort::solveFixedPoint()
  Newton              plain Newton-Raphson, as FilterDistort::solveDelayFree()
  Newton + bisection  newton_bisection.py: bisect once a step leaves a bracket it has found
  safeguarded Newton  "rtsafe": Newton inside the a-priori bracket, bisecting whenever a step
                      would leave the bracket or isn't shrinking fast enough
  Illinois            modified regula falsi on the a-priori bracket, warm-started at y0

The a-priori bracket: |distort| <= 1, so |D| <= 5 and every root lies in
    c - 5|b0 g| <= y <= c + 5|b0 g|,   c = b0 * in + s,
with h <= 0 at the lower end and h >= 0 at the upper end. Bracketing methods therefore never
need to search for a sign change, and are guaranteed to converge.

Experiments, over a grid of filter mode x cutoff x Q x feedback gain x shape:
  sine        the module running on a 5 V 110 Hz sine; initial guess = previous output
  noise       the module running on uniform white noise in +-5 V; initial guess = previous output
  cold start  single solves from random initial guesses y0 and known terms c, both in +-20 V

Iterations count one h evaluation (derivative-free methods) or one h and h' evaluation
(Newton methods). Cost is estimated in transcendental function calls: D needs sin and tanh,
D' needs cos and tanh, and tanh is shared when both are evaluated at the same point.

Writes figures and CSV tables to analysis/comparison/.

Usage:
    py root_finding_comparison.py [--quick]
"""

import argparse
import time
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt

from newton_raphson import F32, DH_EPSILON, NR_TOLERANCE, Biquad, D, distort_derivative, h, newton_raphson
from newton_bisection import newton_bisection

MAX_ITERATIONS = 48
MODULE_BUDGET = 16  # FilterDistort's NR_MAX_ITERATIONS
TOL = NR_TOLERANCE

OUT_DIR = Path(__file__).with_name("comparison")


def a_priori_bracket(inp, gain, b0, s, like):
    """[lo, hi] containing every root, broadcast to the shape of like. Widened slightly so
    float32 rounding can't put a root just outside it."""
    c = b0 * inp + s
    r = F32(5) * np.abs(b0 * gain) * F32(1.0001) + F32(1e-6)
    lo, hi, _ = np.broadcast_arrays(c - r, c + r, like)
    return lo.astype(F32), hi.astype(F32)


def bisection(y0, inp, gain, shape, b0, s, trace=None, max_iterations=MAX_ITERATIONS):
    lo, hi = a_priori_bracket(inp, gain, b0, s, y0)
    y = lo + (hi - lo) * F32(0.5)
    iterations = np.full(y.shape, max_iterations, dtype=np.int32)
    last_step = hi - lo
    done = np.zeros(y.shape, dtype=bool)
    with np.errstate(all="ignore"):
        for i in range(max_iterations):
            active = ~done
            hy = h(y, inp, gain, shape, b0, s)
            lo = np.where(active & (hy < 0), y, lo)
            hi = np.where(active & (hy > 0), y, hi)
            exact = active & (hy == 0)
            width = hi - lo
            mid = lo + width * F32(0.5)
            y = np.where(active & ~exact, mid, y)
            last_step = np.where(active, width, last_step)
            converged = active & (exact | (width < TOL) | (mid == lo) | (mid == hi))
            iterations[converged] = i + 1
            done |= converged
            if trace is not None:
                trace.append((y.copy(), active.copy()))
            if done.all():
                break
    return y, iterations, last_step


def fixed_point(y0, inp, gain, shape, b0, s, trace=None, max_iterations=MAX_ITERATIONS):
    """FilterDistort::solveFixedPoint(): iterate on the nonlinearity's output yN, starting from
    D(previous output), which is the module's prevDistorted."""
    c = b0 * inp + s
    w1 = b0 * gain
    yN = D(np.asarray(y0, dtype=F32), shape)
    yN, c, w1 = (np.array(a, dtype=F32) for a in np.broadcast_arrays(yN, c, w1))
    iterations = np.full(yN.shape, max_iterations, dtype=np.int32)
    last_step = np.zeros(yN.shape, dtype=F32)
    done = np.zeros(yN.shape, dtype=bool)
    with np.errstate(all="ignore"):
        for i in range(max_iterations):
            active = ~done
            nxt = D(w1 * yN + c, shape)
            err = np.abs(nxt - yN)
            yN = np.where(active, nxt, yN)
            last_step = np.where(active, err, last_step)
            converged = active & (err < TOL)
            iterations[converged] = i + 1
            done |= converged
            if trace is not None:
                trace.append((w1 * yN + c, np.zeros(yN.shape, dtype=bool)))
            if done.all():
                break
    return w1 * yN + c, iterations, last_step


def newton(y0, inp, gain, shape, b0, s, trace=None, max_iterations=MAX_ITERATIONS):
    return newton_raphson(y0, inp, gain, shape, b0, s, trace=trace, max_iterations=max_iterations)


def hybrid(y0, inp, gain, shape, b0, s, trace=None, max_iterations=MAX_ITERATIONS):
    return newton_bisection(y0, inp, gain, shape, b0, s, trace=trace, max_iterations=max_iterations)


def safeguarded_newton(y0, inp, gain, shape, b0, s, trace=None, max_iterations=MAX_ITERATIONS):
    """Newton-Raphson kept inside the a-priori bracket (Numerical Recipes' rtsafe). A step is
    replaced by bisection if it would leave the bracket, the derivative vanishes, or the step
    isn't at least halving compared with the step before last."""
    lo, hi = a_priori_bracket(inp, gain, b0, s, y0)
    y = np.clip(np.where(np.isfinite(y0), y0, lo), lo, hi).astype(F32)
    iterations = np.full(y.shape, max_iterations, dtype=np.int32)
    last_step = hi - lo
    dx = hi - lo
    dx_old = hi - lo
    done = np.zeros(y.shape, dtype=bool)
    with np.errstate(all="ignore"):
        for i in range(max_iterations):
            active = ~done
            hy = h(y, inp, gain, shape, b0, s)
            dh = F32(1) - b0 * gain * distort_derivative(y * F32(0.2), shape)
            lo = np.where(active & (hy < 0), y, lo)
            hi = np.where(active & (hy > 0), y, hi)
            exact = active & (hy == 0)
            iterations[exact] = i + 1
            done |= exact
            active &= ~exact

            vanished = np.abs(dh) < DH_EPSILON
            y_newton = y - hy / np.where(vanished, F32(1), dh)
            mid = lo + (hi - lo) * F32(0.5)
            bisect = (vanished | ~np.isfinite(y_newton) | (y_newton <= lo) | (y_newton >= hi)
                      | (np.abs(F32(2) * hy) > np.abs(dx_old * dh)))
            y_next = np.where(bisect, mid, y_newton)
            step = np.abs(y_next - y)
            dx_old = np.where(active, dx, dx_old)
            dx = np.where(active, step, dx)
            y = np.where(active, y_next, y)
            last_step = np.where(active, step, last_step)
            converged = active & ((step < TOL) | ((hi - lo) < TOL))
            iterations[converged] = i + 1
            done |= converged
            if trace is not None:
                trace.append((y.copy(), active & bisect))
            if done.all():
                break
    return y, iterations, last_step


def illinois(y0, inp, gain, shape, b0, s, trace=None, max_iterations=MAX_ITERATIONS):
    """Illinois (modified regula falsi) on the a-priori bracket. Evaluates both bracket ends and
    then the initial guess, which usually shrinks the bracket a lot (3 evaluations), then
    false-position steps, halving the stale end's value when one end is kept twice in a row."""
    lo, hi = a_priori_bracket(inp, gain, b0, s, y0)
    with np.errstate(all="ignore"):
        f_lo = h(lo, inp, gain, shape, b0, s)
        f_hi = h(hi, inp, gain, shape, b0, s)
        x = np.clip(np.where(np.isfinite(y0), y0, lo), lo, hi).astype(F32)
        fx = h(x, inp, gain, shape, b0, s)
    n_setup = 3
    iterations = np.full(x.shape, max_iterations, dtype=np.int32)
    last_step = hi - lo
    done = np.zeros(x.shape, dtype=bool)
    side = np.zeros(x.shape, dtype=np.int8)

    def update(x, fx, active):
        nonlocal lo, hi, f_lo, f_hi, side
        left = active & (fx < 0)
        right = active & (fx > 0)
        lo, f_lo = np.where(left, x, lo), np.where(left, fx, f_lo)
        hi, f_hi = np.where(right, x, hi), np.where(right, fx, f_hi)
        # Illinois: if the same end moves twice in a row, halve the other end's value
        f_hi = np.where(left & (side == -1), f_hi * F32(0.5), f_hi)
        f_lo = np.where(right & (side == 1), f_lo * F32(0.5), f_lo)
        side = np.where(left, -1, np.where(right, 1, side)).astype(np.int8)

    with np.errstate(all="ignore"):
        for ends, f_end in ((lo, f_lo), (hi, f_hi)):
            exact = ~done & (f_end == 0)
            x = np.where(exact, ends, x)
            iterations[exact] = n_setup
            done |= exact
        exact = ~done & (fx == 0)
        iterations[exact] = n_setup
        done |= exact
        update(x, fx, ~done)
        side[:] = 0

        for i in range(max_iterations - n_setup):
            active = ~done
            denom = f_hi - f_lo
            x_new = (lo * f_hi - hi * f_lo) / np.where(denom == 0, F32(1), denom)
            mid = lo + (hi - lo) * F32(0.5)
            x_new = np.where((denom == 0) | ~np.isfinite(x_new) | (x_new < lo) | (x_new > hi), mid, x_new)
            fx = h(x_new, inp, gain, shape, b0, s)
            step = np.abs(x_new - x)
            x = np.where(active, x_new, x)
            last_step = np.where(active, step, last_step)
            update(x, fx, active)
            converged = active & ((fx == 0) | (step < TOL) | ((hi - lo) < TOL))
            iterations[converged] = n_setup + i + 1
            done |= converged
            if trace is not None:
                trace.append((x.copy(), np.zeros(x.shape, dtype=bool)))
            if done.all():
                break
    return x, iterations, last_step


# name, function, transcendental calls per iteration, colour
METHODS = [
    ("bisection", bisection, 2, "C7"),
    ("fixed-point", fixed_point, 2, "C2"),
    ("Newton", newton, 3, "C3"),
    ("Newton + bisection", hybrid, 3, "C4"),
    ("safeguarded Newton", safeguarded_newton, 3, "C0"),
    ("Illinois", illinois, 2, "C1"),
]


def make_filters(modes, freqs, qs, sample_rate):
    """One Biquad holding a batch of instances with per-instance mode, cutoff and Q."""
    batch = Biquad("lowpass", freqs, qs, sample_rate)
    for mode in ("bandpass", "highpass"):
        sel = modes == mode
        if sel.any():
            part = Biquad(mode, freqs[sel], qs[sel], sample_rate)
            batch.b[:, sel] = part.b
            batch.a[:, sel] = part.a
    return batch


def simulate(solve, modes, freqs, qs, gains, shapes, signal, sample_rate):
    """FilterDistort::process() with a delay-free solver over a batch of instances, each with
    its own mode, cutoff, Q, feedback gain and shape. Returns iterations (samples, batch)."""
    filt = make_filters(modes, freqs, qs, sample_rate)
    gains, shapes = gains.astype(F32), shapes.astype(F32)
    feedback_state = np.zeros(len(freqs), dtype=F32)
    iterations = np.zeros((len(signal), len(freqs)), dtype=np.int32)
    for t, inp in enumerate(signal):
        y, its, _ = solve(filt.y[0], inp, gains, shapes, filt.b[0], filt.historical())
        with np.errstate(all="ignore"):
            filter_in = np.where(np.isfinite(y), inp + D(y, shapes) * gains, inp + feedback_state)
            distorted = D(filt.process(filter_in), shapes)
        feedback_state = distorted * gains
        iterations[t] = its
    return iterations


def root_choice(solve, modes, freqs, qs, gains, shapes, signal, sample_rate, n_points=400):
    """Run the module and, at every sample where the loop equation has more than one root, check
    whether the solver picked the root nearest the previous output (the continuous choice).

    Returns (fraction of converged samples with more than one root, fraction of those where a
    different root was picked)."""
    filt = make_filters(modes, freqs, qs, sample_rate)
    gains, shapes = gains.astype(F32), shapes.astype(F32)
    feedback_state = np.zeros(len(freqs), dtype=F32)
    grid = np.linspace(0, 1, n_points, dtype=F32)[:, None]
    multi = other = total = 0
    cols = np.arange(len(freqs))
    for inp in signal:
        b0, s, y0 = filt.b[0], filt.historical(), filt.y[0].copy()
        y, its, _ = solve(y0, inp, gains, shapes, b0, s)
        lo, hi = a_priori_bracket(inp, gains, b0, s, y0)
        ys = lo[None] + (hi - lo)[None] * grid
        with np.errstate(all="ignore"):
            hs = h(ys, inp, gains, shapes, b0, s)
            crossing = np.sign(hs[1:]) != np.sign(hs[:-1])
            roots = np.where(crossing, (ys[1:] + ys[:-1]) * F32(0.5), np.inf)
            nearest = roots[np.argmin(np.abs(roots - y0[None]), axis=0), cols]
            ok = np.isfinite(y) & (its < MAX_ITERATIONS)
            several = ok & (crossing.sum(axis=0) > 1)
            # A root counts as the nearest one if within two grid steps of it
            other += (several & (np.abs(y - nearest) > 2 * (hi - lo) / (n_points - 1))).sum()
            multi += several.sum()
            total += ok.sum()
            filter_in = np.where(np.isfinite(y), inp + D(y, shapes) * gains, inp + feedback_state)
            distorted = D(filt.process(filter_in), shapes)
        feedback_state = distorted * gains
    return multi / max(total, 1), other / max(multi, 1)


def count_roots(c, gain, shape, b0, n_points=512):
    """Number of sign changes of h on a fine grid over the a-priori bracket (s = 0, in = c / b0)."""
    lo, hi = a_priori_bracket(c / b0, gain, b0, F32(0), c)
    count = np.zeros(c.shape, dtype=np.int16)
    prev = None
    for k in range(n_points):
        y = lo + (hi - lo) * F32(k / (n_points - 1))
        hy = h(y, c / b0, gain, shape, b0, F32(0))
        if prev is not None:
            count += (np.sign(hy) != np.sign(prev)) & (hy != 0)
        prev = hy
    return np.maximum(count, 1)


def summarise(name, cost, its):
    flat = its.ravel()
    conv16 = (flat <= MODULE_BUDGET).mean()
    conv48 = (flat < MAX_ITERATIONS).mean()
    converged = flat[flat < MAX_ITERATIONS]
    return {
        "method": name,
        "converged within 16 (%)": 100 * conv16,
        "converged within 48 (%)": 100 * conv48,
        "mean iterations": flat.mean(),
        "median iterations (converged)": np.median(converged) if converged.size else np.nan,
        "p99 iterations": np.percentile(flat, 99),
        "max iterations (converged)": converged.max() if converged.size else np.nan,
        "mean transcendental calls": cost * flat.mean(),
    }


def write_csv(path, rows):
    keys = list(rows[0].keys())
    with open(path, "w") as f:
        f.write(",".join(keys) + "\n")
        for row in rows:
            f.write(",".join(f"{row[k]:.4g}" if isinstance(row[k], (float, np.floating)) else str(row[k])
                             for k in keys) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--quick", action="store_true", help="smaller grid and fewer samples, for testing")
    args = parser.parse_args()
    OUT_DIR.mkdir(exist_ok=True)
    rng = np.random.default_rng(0)
    sr = 48000.0

    # Parameter grid; every combination is one module instance
    modes_list = ["lowpass", "bandpass", "highpass"]
    freqs_list = np.geomspace(50.0, 20000.0, 6 if args.quick else 10)
    qs_list = [0.70710678, 2.0, 8.0]
    gains_list = [0.5, 1.0, 2.0, 3.0, 4.0, 5.0]
    shapes_list = [0.0, 0.25, 0.5, 0.75, 1.0]
    grid = np.array(np.meshgrid(np.arange(3), freqs_list, qs_list, gains_list, shapes_list, indexing="ij"))
    grid = grid.reshape(5, -1)
    modes = np.array(modes_list)[grid[0].astype(int)]
    freqs, qs, gains, shapes = grid[1], grid[2], grid[3], grid[4]
    n_inst = len(freqs)
    filt = make_filters(modes, freqs, qs, sr)
    b0 = filt.b[0]
    kappa = b0 * gains.astype(F32)  # instantaneous loop gain b0 * g
    print(f"{n_inst} parameter combinations, b0 * g from {kappa.min():.3g} to {kappa.max():.3g}")

    n = int(sr * (1 if args.quick else 2) / 110.0)
    t = np.arange(n) / sr
    signals = {
        "sine": (5 * np.sin(2 * np.pi * 110 * t)).astype(F32),
        "noise": rng.uniform(-5, 5, n).astype(F32),
    }

    # Cold start: random (y0, c) per instance
    n_draws = 100 if args.quick else 400
    inst = np.repeat(np.arange(n_inst), n_draws)
    y0_cold = rng.uniform(-20, 20, inst.size).astype(F32)
    c_cold = rng.uniform(-20, 20, inst.size).astype(F32)
    b0_cold, g_cold, shape_cold = b0[inst], gains[inst].astype(F32), shapes[inst].astype(F32)

    results = {}  # (experiment, method) -> iterations, (samples or draws, instances)
    for name, solve, cost, _ in METHODS:
        for exp, signal in signals.items():
            start = time.perf_counter()
            results[exp, name] = simulate(solve, modes, freqs, qs, gains, shapes, signal, sr)
            print(f"  {exp:10s} {name:20s} {time.perf_counter() - start:6.1f} s")
        start = time.perf_counter()
        _, its, _ = solve(y0_cold, c_cold / b0_cold, g_cold, shape_cold, b0_cold, F32(0))
        results["cold start", name] = its.reshape(n_inst, n_draws).T
        print(f"  {'cold start':10s} {name:20s} {time.perf_counter() - start:6.1f} s")

    # Multiple roots: how often the equation itself has more than one solution
    roots = count_roots(c_cold, g_cold, shape_cold, b0_cold).reshape(n_inst, n_draws).T

    experiments = ["sine", "noise", "cold start"]

    # Summary tables
    for exp in experiments:
        rows = [summarise(name, cost, results[exp, name]) for name, _, cost, _ in METHODS]
        write_csv(OUT_DIR / f"summary_{exp.replace(' ', '_')}.csv", rows)
        print(f"\n{exp}:")
        print(f"  {'method':20s} {'conv@16':>8s} {'conv@48':>8s} {'mean it':>8s} {'p99 it':>7s} {'calls':>7s}")
        for r in rows:
            print(f"  {r['method']:20s} {r['converged within 16 (%)']:7.2f}% {r['converged within 48 (%)']:7.2f}% "
                  f"{r['mean iterations']:8.2f} {r['p99 iterations']:7.0f} {r['mean transcendental calls']:7.2f}")

    # Breakdowns by kappa bin, shape and mode
    kappa_edges = np.array([0, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0001])
    kappa_bin = np.clip(np.digitize(kappa, kappa_edges) - 1, 0, len(kappa_edges) - 2)
    kappa_labels = [f"{lo:g}-{hi:g}" for lo, hi in zip(kappa_edges[:-1], kappa_edges[1:])]
    kappa_mid = 0.5 * (kappa_edges[:-1] + kappa_edges[1:])

    def by_group(its, groups, n_groups, fn):
        return np.array([fn(its[:, groups == k]) if (groups == k).any() else np.nan for k in range(n_groups)])

    fail48 = lambda its: 100 * (its >= MAX_ITERATIONS).mean()
    fail16 = lambda its: 100 * (its > MODULE_BUDGET).mean()
    mean_its = lambda its: its.mean()

    breakdown_rows = []
    for exp in experiments:
        for name, _, cost, _ in METHODS:
            its = results[exp, name]
            for label, (key, groups, n_groups, names) in {
                "b0*g": ("kappa", kappa_bin, len(kappa_labels), kappa_labels),
                "shape": ("shape", np.searchsorted(shapes_list, shapes), len(shapes_list), shapes_list),
                "mode": ("mode", grid[0].astype(int), 3, modes_list),
                "Q": ("Q", np.searchsorted(qs_list, qs), len(qs_list), qs_list),
            }.items():
                for k in range(n_groups):
                    sel = groups == k
                    if not sel.any():
                        continue
                    breakdown_rows.append({
                        "experiment": exp, "method": name, "by": label, "group": str(names[k]),
                        "instances": int(sel.sum()),
                        "not converged within 16 (%)": fail16(its[:, sel]),
                        "not converged within 48 (%)": fail48(its[:, sel]),
                        "mean iterations": mean_its(its[:, sel]),
                        "mean transcendental calls": cost * mean_its(its[:, sel]),
                    })
    write_csv(OUT_DIR / "breakdown.csv", breakdown_rows)

    # Figure 1: non-convergence vs b0 * g, per experiment, at the module's budget and at 48
    fig, axes = plt.subplots(2, 3, figsize=(15, 8.5), layout="constrained", sharex=True)
    for col, exp in enumerate(experiments):
        for row, (fn, budget) in enumerate([(fail16, MODULE_BUDGET), (fail48, MAX_ITERATIONS)]):
            ax = axes[row, col]
            for name, _, _, colour in METHODS:
                vals = by_group(results[exp, name], kappa_bin, len(kappa_labels), fn)
                ax.plot(kappa_mid, np.maximum(vals, 1e-3), "o-", color=colour, label=name, ms=4)
            ax.set_yscale("log")
            ax.set_ylim(5e-4, 150)
            ax.axvline(1, color="k", ls=":", lw=0.8)
            ax.set_title(f"{exp}: not converged within {budget} iterations", fontsize=10)
            ax.set_ylabel("% of solves (0 plotted at 1e-3)")
            if row == 1:
                ax.set_xlabel("instantaneous loop gain b0 * g")
    axes[0, 0].legend(fontsize=8)
    fig.suptitle("Convergence failures vs instantaneous loop gain (dotted: b0 * g = 1, above which roots "
                 "may be non-unique and fixed-point iteration may not contract)")
    fig.savefig(OUT_DIR / "convergence_vs_loop_gain.png", dpi=110)
    plt.close(fig)

    # Figure 2: cost vs b0 * g
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.5), layout="constrained", sharey=True)
    for col, exp in enumerate(experiments):
        ax = axes[col]
        for name, _, cost, colour in METHODS:
            vals = by_group(results[exp, name], kappa_bin, len(kappa_labels), mean_its) * cost
            ax.plot(kappa_mid, vals, "o-", color=colour, label=name, ms=4)
        ax.set_yscale("log")
        ax.axvline(1, color="k", ls=":", lw=0.8)
        ax.set_title(f"{exp}: mean cost per solve", fontsize=10)
        ax.set_xlabel("instantaneous loop gain b0 * g")
    axes[0].set_ylabel("transcendental calls (sin/cos/tanh)")
    axes[0].legend(fontsize=8)
    fig.savefig(OUT_DIR / "cost_vs_loop_gain.png", dpi=110)
    plt.close(fig)

    # Figure 3: by shape, for the cold start and noise experiments, restricted to b0 * g > 1
    hot = kappa > 1
    shape_idx = np.searchsorted(shapes_list, shapes)
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.5), layout="constrained")
    for col, exp in enumerate(["noise", "cold start"]):
        ax = axes[col]
        for name, _, _, colour in METHODS:
            its = results[exp, name][:, hot]
            vals = by_group(its, shape_idx[hot], len(shapes_list), fail48)
            ax.plot(shapes_list, np.maximum(vals, 1e-3), "o-", color=colour, label=name, ms=4)
        ax.set_yscale("log")
        ax.set_ylim(5e-4, 150)
        ax.set_xlabel("distortion shape")
        ax.set_ylabel("% not converged within 48")
        ax.set_title(f"{exp}, b0 * g > 1 only", fontsize=10)
    axes[0].legend(fontsize=8)
    ax = axes[2]
    for k, shape in enumerate(shapes_list):
        sel = shape_idx == k
        vals = by_group(roots[:, sel], kappa_bin[sel], len(kappa_labels), lambda r: 100 * (r > 1).mean())
        ax.plot(kappa_mid, vals, "o-", label=f"shape {shape:g}", ms=4)
    ax.axvline(1, color="k", ls=":", lw=0.8)
    ax.set_xlabel("instantaneous loop gain b0 * g")
    ax.set_ylabel("% of cold-start cases")
    ax.set_title("Equation has more than one root", fontsize=10)
    ax.legend(fontsize=8)
    fig.savefig(OUT_DIR / "shape_and_root_multiplicity.png", dpi=110)
    plt.close(fig)

    # Root multiplicity table
    mult_rows = []
    for k, label in enumerate(kappa_labels):
        sel = kappa_bin == k
        if sel.any():
            mult_rows.append({"b0*g": label, "instances": int(sel.sum()),
                              "multiple roots (%)": 100 * (roots[:, sel] > 1).mean(),
                              "max roots": int(roots[:, sel].max())})
    write_csv(OUT_DIR / "root_multiplicity.csv", mult_rows)
    print("\nRoot multiplicity (cold start):")
    for r in mult_rows:
        print(f"  b0*g {r['b0*g']:8s}: {r['multiple roots (%)']:6.2f}% multiple, max {r['max roots']}")

    # Root choice on the sine input, for the instances where roots can be non-unique
    print("\nRoot choice, sine input, b0 * g > 1:")
    choice_rows = []
    for name, solve, _, _ in METHODS:
        multi, other = root_choice(solve, modes[hot], freqs[hot], qs[hot], gains[hot], shapes[hot],
                                   signals["sine"], sr)
        choice_rows.append({"method": name, "converged samples with several roots (%)": 100 * multi,
                            "of those, root other than nearest to previous output (%)": 100 * other})
        print(f"  {name:20s} {100 * multi:5.1f}% of samples have several roots; "
              f"picked a non-nearest root in {100 * other:5.1f}% of those")
    write_csv(OUT_DIR / "root_choice.csv", choice_rows)
    print(f"\nWrote results to {OUT_DIR}")


if __name__ == "__main__":
    main()
