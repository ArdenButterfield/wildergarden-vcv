"""
Reference implementation of the FilterDistort Newton-Raphson delay-free loop solver.

Mirrors FilterDistort::solveDelayFree() and FilterDistort::process() (Newton-Raphson mode)
from src/FilterDistort.cpp, including Rack's dsp::BiquadFilter (direct form I) and float32
arithmetic. Everything is vectorised over a batch of independent module instances, so the
same code runs a single case or a whole parameter grid.

The loop being solved, per sample:
    y = b0 * (in + g * D(y)) + s,   D(y) = 5 * distort(0.2 * y, shape)
by Newton-Raphson on h(y) = y - b0 * (in + g * D(y)) - s, starting from the previous output.

Non-converged solves are split into two kinds:
  - precision-limited: the iterate is at a root but steps by ~1 float32 ulp forever, because
    the tolerance is smaller than the float32 spacing at that |y|;
  - genuine: the iteration cycles, wanders or blows up away from any root.

Usage:
    py newton_raphson.py [--shape 0.25] [--q 0.707] [--mode lowpass] [--show]
"""

import argparse
from pathlib import Path
from typing import Callable, NamedTuple

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import BoundaryNorm, ListedColormap
from matplotlib.patches import Patch

F32 = np.float32

# Same constants as FilterDistort.cpp
NR_MAX_ITERATIONS = 16
NR_TOLERANCE = F32(1e-5)
DH_EPSILON = F32(1e-9)
MAX_NORMALIZED_FREQ = 0.45

# A non-converged solve whose final step is within this many ulps of y is precision-limited
ULP_SLACK = 4


def distort(x, shape):
    a = np.sin(x + (shape - F32(0.5)) * F32(np.pi))
    b = np.tanh(x)
    blend = np.minimum(shape * F32(2), F32(1))
    return blend * a + (F32(1) - blend) * b


def distort_derivative(x, shape):
    a = np.cos(x + (shape - F32(0.5)) * F32(np.pi))
    t = np.tanh(x)
    b = F32(1) - t * t
    blend = np.minimum(shape * F32(2), F32(1))
    return blend * a + (F32(1) - blend) * b


def D(y, shape):
    """The module's nonlinearity as seen by the loop: 5 * distort(0.2 * y)."""
    return distort(y * F32(0.2), shape) * F32(5)


class Biquad:
    """dsp::BiquadFilter: setParameters() for LOWPASS/BANDPASS/HIGHPASS and IIRFilter::process()."""

    def __init__(self, mode, freq_hz, q, sample_rate):
        freq_hz = np.atleast_1d(np.asarray(freq_hz, dtype=np.float64))
        q = np.broadcast_to(np.asarray(q, dtype=np.float64), freq_hz.shape)
        f = np.minimum(freq_hz / sample_rate, MAX_NORMALIZED_FREQ).astype(F32)
        # K = std::tan(M_PI * f) is evaluated in double, then stored as float
        K = np.tan(np.pi * f.astype(np.float64)).astype(F32)
        q = q.astype(F32)
        norm = F32(1) / (F32(1) + K / q + K * K)
        if mode == "lowpass":
            b0 = K * K * norm
            b1, b2 = F32(2) * b0, b0
        elif mode == "bandpass":
            b0 = K / q * norm
            b1, b2 = np.zeros_like(b0), -b0
        elif mode == "highpass":
            b0 = norm
            b1, b2 = F32(-2) * b0, b0
        else:
            raise ValueError(mode)
        self.b = np.stack([b0, b1, b2]).astype(F32)
        self.a = np.stack([F32(2) * (K * K - F32(1)) * norm, (F32(1) - K / q + K * K) * norm]).astype(F32)
        n = freq_hz.shape[0]
        self.x = np.zeros((2, n), dtype=F32)
        self.y = np.zeros((2, n), dtype=F32)

    def historical(self):
        """s: the output contribution of the filter's internal state."""
        b, a, x, y = self.b, self.a, self.x, self.y
        return b[1] * x[0] + b[2] * x[1] - a[0] * y[0] - a[1] * y[1]

    def process(self, inp):
        out = self.b[0] * inp + self.historical()
        self.x[1] = self.x[0]
        self.x[0] = inp
        self.y[1] = self.y[0]
        self.y[0] = out
        return out


def h(y, inp, gain, shape, b0, s):
    return y - b0 * (inp + gain * D(y, shape)) - s


def newton_raphson(y0, inp, gain, shape, b0, s, trace=None, max_iterations=NR_MAX_ITERATIONS):
    """The loop of FilterDistort::solveDelayFree(), vectorised, from initial guess y0.

    Returns (y, iterations, last_step). iterations is the count needed for
    |step| < NR_TOLERANCE, or max_iterations if the solver ran out of iterations, the
    derivative vanished, or the result is not finite. last_step is the size of the final step
    taken (not part of the C++ code; used to classify non-convergence).
    If trace is a list, (iterate, is_bisection_step) is appended to it after each step; the
    flag is always False here, and exists so plots can share a trace format with other solvers.
    """
    y = np.array(y0, dtype=F32, copy=True)
    iterations = np.full(y.shape, max_iterations, dtype=np.int32)
    last_step = np.zeros(y.shape, dtype=F32)
    # Instances that have hit a `break` in the C++ loop
    done = np.zeros(y.shape, dtype=bool)
    no_bisection = np.zeros(y.shape, dtype=bool)
    if trace is not None:
        trace.append((y.copy(), no_bisection))

    with np.errstate(all="ignore"):
        for i in range(max_iterations):
            hy = h(y, inp, gain, shape, b0, s)
            # D'(y) = distort'(0.2 * y), as the 5 and 0.2 scalings cancel
            dh = F32(1) - b0 * gain * distort_derivative(y * F32(0.2), shape)
            vanished = ~done & (np.abs(dh) < DH_EPSILON)
            last_step[vanished] = np.inf
            done |= vanished
            step = np.where(done, F32(0), hy / np.where(done, F32(1), dh))
            last_step = np.where(done, last_step, np.abs(step))
            y = np.where(done, y, y - step)
            just_converged = ~done & (np.abs(step) < NR_TOLERANCE)
            iterations[just_converged] = i + 1
            done |= just_converged
            if trace is not None:
                trace.append((y.copy(), no_bisection))
            if done.all():
                break

    iterations[~np.isfinite(y)] = max_iterations
    return y, iterations, last_step


class Solver(NamedTuple):
    """A delay-free loop solver, so the same simulation and figure code can run any of them.

    solve(y0, inp, gain, shape, b0, s, trace=None) -> (y, iterations, last_step), where
    iterations == max_iterations means not converged.
    """
    name: str
    solve: Callable
    max_iterations: int
    tolerance: float


NEWTON_RAPHSON = Solver("Newton-Raphson", newton_raphson, NR_MAX_ITERATIONS, NR_TOLERANCE)


def genuinely_failed(solver, y, iterations, last_step):
    """Not converged, and not merely stepping by ~1 ulp at a root."""
    with np.errstate(all="ignore"):
        ulp = np.spacing(np.abs(y).astype(F32))
        precision_limited = np.isfinite(y) & (last_step <= ULP_SLACK * ulp)
    return (iterations >= solver.max_iterations) & ~precision_limited


def simulate(solver, mode, freq_hz, q, gain, shape, signal, sample_rate):
    """FilterDistort::process() with a delay-free solver, over a batch of instances.

    The solver starts from the filter's previous output, as FilterDistort::solveDelayFree() does.
    freq_hz, q and gain broadcast to the batch shape. Returns output, iterations and genuine
    failure flags, each (samples, batch).
    """
    freq_hz = np.atleast_1d(np.asarray(freq_hz, dtype=np.float64))
    filt = Biquad(mode, freq_hz, q, sample_rate)
    gain = np.broadcast_to(np.asarray(gain, dtype=F32), freq_hz.shape).astype(F32)
    shape = F32(shape)
    n = freq_hz.shape[0]
    feedback_state = np.zeros(n, dtype=F32)
    outputs = np.zeros((len(signal), n), dtype=F32)
    iterations = np.zeros((len(signal), n), dtype=np.int32)
    genuine = np.zeros((len(signal), n), dtype=bool)

    for t, inp in enumerate(signal.astype(F32)):
        y, its, last_step = solver.solve(filt.y[0], inp, gain, shape, filt.b[0], filt.historical())
        # Fall back to the unit-delay loop if the solver diverged
        filter_in = np.where(np.isfinite(y), inp + D(y, shape) * gain, inp + feedback_state)
        filtered = filt.process(filter_in)
        distorted = D(filtered, shape)
        feedback_state = distorted * gain
        outputs[t] = distorted
        iterations[t] = its
        genuine[t] = genuinely_failed(solver, y, its, last_step)

    return outputs, iterations, genuine


def plot_solve(ax, solver, y0, c, gain, shape, b0, title):
    """Plot h(y) for one solve, with the iterates: Newton-Raphson tangent steps in red,
    bisection midpoints in purple.

    c = b0 * in + s collects everything known at this sample, so in = c / b0, s = 0.
    """
    inp, s = F32(c / b0), F32(0)
    trace = []
    _, its, _ = solver.solve(np.array([y0], F32), inp, gain, shape, b0, s, trace=trace)
    iterates = np.array([v[0][0] for v in trace], dtype=np.float64)
    bisection = np.array([v[1][0] for v in trace], dtype=bool)
    keep = np.isfinite(iterates)
    finite, bisection = iterates[keep], bisection[keep]

    span = max(np.ptp(finite), 4.0)
    lo, hi = finite.min() - 0.25 * span, finite.max() + 0.25 * span
    ys = np.linspace(lo, hi, 2000).astype(F32)
    hs = h(ys, inp, gain, shape, b0, s)
    ax.axhline(0, color="0.6", lw=0.8)
    ax.plot(ys, hs, color="C0", lw=1.5, label="h(y)")

    hv = h(finite.astype(F32), inp, gain, shape, b0, s)
    for k in range(len(finite) - 1):
        if bisection[k + 1]:
            # Bisection step: no tangent, just the evaluated midpoint
            ax.plot([finite[k + 1], finite[k + 1]], [0, hv[k + 1]], color="C4", lw=0.9, alpha=0.7)
        else:
            ax.plot([finite[k], finite[k], finite[k + 1]], [0, hv[k], 0], color="C3", lw=0.9, alpha=0.7)
    ax.scatter(finite[~bisection], hv[~bisection], color="C3", s=14, zorder=3, label="Newton-Raphson iterates")
    if bisection.any():
        ax.scatter(finite[bisection], hv[bisection], color="C4", marker="s", s=14, zorder=3,
                   label="bisection midpoints")
    ax.scatter([finite[0]], [hv[0]], color="k", s=30, zorder=4, label="initial guess")

    # Inset zoomed on the bisection midpoints when the full trajectory dwarfs them
    if bisection.any():
        b_lo, b_hi = finite[bisection].min(), finite[bisection].max()
        if b_hi - b_lo < 0.1 * (hi - lo):
            pad = max(b_hi - b_lo, 1e-3) * 0.5
            zoom = ax.inset_axes([0.55, 0.08, 0.42, 0.42])
            zys = np.linspace(b_lo - pad, b_hi + pad, 500).astype(F32)
            zoom.axhline(0, color="0.6", lw=0.8)
            zoom.plot(zys, h(zys, inp, gain, shape, b0, s), color="C0", lw=1.2)
            zoom.scatter(finite[bisection], hv[bisection], color="C4", marker="s", s=10, zorder=3)
            zoom.set_xlim(b_lo - pad, b_hi + pad)
            zoom.set_title(f"bisection, zoomed ({bisection.sum()} steps)", fontsize=7)
            zoom.tick_params(labelsize=6)
            ax.indicate_inset_zoom(zoom, edgecolor="0.4")

    status = f"converged after {its[0]} iterations" if its[0] < solver.max_iterations else \
        f"not converged after {solver.max_iterations} iterations"
    ax.set_title(f"{title}\ny0 = {y0:.3g} V, c = {c:.3g} V: {status}", fontsize=10)
    ax.set_xlabel("y (filter output, V)")
    ax.set_ylabel("h(y)")
    ax.set_xlim(lo, hi)
    pad = 0.1 * np.ptp(hs)
    ax.set_ylim(hs.min() - pad, hs.max() + pad)
    ax.legend(fontsize=7, loc="upper left")


def run(solver, description, default_output, show_own_failures=False):
    """Parse the command line, run solver through every analysis and save the figure.

    With show_own_failures, adds a row with solves where this solver itself does not converge:
    one that never takes a bisection step, and one that does.
    """
    parser = argparse.ArgumentParser(description=description, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--shape", type=float, default=0.25, help="distortion shape, 0-1 (default 0.25)")
    parser.add_argument("--q", type=float, default=0.70710678, help="filter Q (default 0.707)")
    parser.add_argument("--mode", choices=["lowpass", "bandpass", "highpass"], default="lowpass")
    parser.add_argument("--sample-rate", type=float, default=48000.0)
    parser.add_argument("--input-freq", type=float, default=110.0, help="test sine frequency in Hz")
    parser.add_argument("--input-amp", type=float, default=5.0, help="test sine amplitude in V")
    parser.add_argument("--gentle", type=float, nargs=2, default=[2000.0, 2.0], metavar=("FREQ", "GAIN"),
                        help="cutoff (Hz) and feedback gain of the gentle setting (default 2000 2)")
    parser.add_argument("--hard", type=float, nargs=2, default=[18000.0, 5.0], metavar=("FREQ", "GAIN"),
                        help="cutoff (Hz) and feedback gain of the hard setting (default 18000 5)")
    parser.add_argument("--output", type=Path, default=default_output)
    parser.add_argument("--show", action="store_true", help="open an interactive window")
    args = parser.parse_args()

    sr = args.sample_rate
    shape = F32(args.shape)
    n = int(round(sr * 4 / args.input_freq))  # four periods
    t = np.arange(n) / sr
    signal = (args.input_amp * np.sin(2 * np.pi * args.input_freq * t)).astype(F32)

    # Time-domain runs of the module at two settings, as a batch of two instances
    freqs = np.array([args.gentle[0], args.hard[0]])
    gains = np.array([args.gentle[1], args.hard[1]])
    labels = [f"cutoff {f:g} Hz, feedback {g:g}" for f, g in zip(freqs, gains)]
    max_its = solver.max_iterations
    _, its, genuine = simulate(solver, args.mode, freqs, args.q, gains, shape, signal, sr)
    print(f"{solver.name}, module running on a {args.input_amp:g} V {args.input_freq:g} Hz sine, "
          f"shape {args.shape:g}:")
    for col, label in enumerate(labels):
        nc = its[:, col] >= max_its
        print(f"  {label}: mean {its[:, col].mean():.2f} iterations, "
              f"{100 * nc.mean():.1f}% not converged ({100 * genuine[:, col].mean():.1f}% genuine, "
              f"{100 * (nc & ~genuine[:, col]).mean():.1f}% precision-limited)")

    # Basin map for the hard setting: one solve for every initial guess y0 and known term
    # c = b0 * in + s. In the module y0 is the previous output; here it is swept freely.
    hard = Biquad(args.mode, [args.hard[0]], args.q, sr)
    b0 = hard.b[0, 0]
    hard_gain = F32(args.hard[1])
    y0s = np.linspace(-30, 30, 401).astype(F32)
    cs = np.linspace(-30, 30, 401).astype(F32)
    Y0, C = np.meshgrid(y0s, cs)
    y, basin_its, last_step = solver.solve(Y0, C / b0, hard_gain, shape, b0, F32(0))
    basin_genuine = genuinely_failed(solver, y, basin_its, last_step)
    # Categories: 1..max-1 iterations to converge, max = precision-limited, max+1 = genuine
    basin = np.where(basin_genuine, max_its + 1, basin_its)
    print(f"Basin map, {labels[1]} (b0 * g = {b0 * hard_gain:.2f}): "
          f"{100 * (basin == max_its).mean():.1f}% precision-limited, "
          f"{100 * basin_genuine.mean():.1f}% genuine failures")

    # Example solves on the same row of the basin map (same c), differing only in y0: the plain
    # Newton-Raphson genuine failure closest to the centre, and the nearest guess beside it where
    # Newton-Raphson converges. Chosen from Newton-Raphson for every solver, so their figures
    # show the same two cases.
    nr_y, nr_its, nr_last_step = newton_raphson(Y0, C / b0, hard_gain, shape, b0, F32(0))
    nr_genuine = genuinely_failed(NEWTON_RAPHSON, nr_y, nr_its, nr_last_step)
    rows, cols = np.nonzero(nr_genuine)
    if rows.size == 0:
        raise SystemExit("No Newton-Raphson failures at the hard setting; try a higher --hard gain or cutoff.")
    k = np.argmin(np.hypot(Y0[rows, cols], C[rows, cols]))
    r, fail_col = rows[k], cols[k]
    converged_cols = np.flatnonzero(nr_its[r] < NR_MAX_ITERATIONS)
    ok_col = converged_cols[np.argmin(np.abs(converged_cols - fail_col))]

    # This solver's own genuine failures closest to the centre of the basin map, split by
    # whether it ever took a bisection step
    own_cases = []
    if show_own_failures:
        trace = []
        solver.solve(Y0, C / b0, hard_gain, shape, b0, F32(0), trace=trace)
        ever_bisected = np.stack([step[1] for step in trace]).any(axis=0)
        for kind, mask in [("never brackets a root", basin_genuine & ~ever_bisected),
                           ("out of iterations while bisecting", basin_genuine & ever_bisected)]:
            rows, cols = np.nonzero(mask)
            if rows.size:
                k = np.argmin(np.hypot(Y0[rows, cols], C[rows, cols]))
                own_cases.append((kind, rows[k], cols[k]))

    # Parameter map: cutoff x feedback gain, every cell a separate module instance
    grid_freqs = np.geomspace(20.0, 20000.0, 60)
    grid_gains = np.linspace(0.0, 5.0, 41)
    F, G = np.meshgrid(grid_freqs, grid_gains)
    _, grid_its, grid_genuine = simulate(solver, args.mode, F.ravel(), args.q, G.ravel(), shape, signal, sr)
    grid_mean = grid_its.mean(axis=0).reshape(F.shape)
    grid_nc = (grid_its >= max_its).mean(axis=0).reshape(F.shape) * 100
    grid_gen = grid_genuine.mean(axis=0).reshape(F.shape) * 100

    n_rows = 4 if own_cases else 3
    fig = plt.figure(figsize=(13, 4.7 * n_rows), layout="constrained")
    fig.suptitle(f"FilterDistort {solver.name} solver: {args.mode}, Q = {args.q:.3g}, shape = {args.shape:g}, "
                 f"fs = {sr:g} Hz, max {max_its} iterations, tolerance {solver.tolerance:g}")
    gs = fig.add_gridspec(n_rows, 2)
    own_letters = "gh"
    for col, (kind, row, own_col) in enumerate(own_cases):
        plot_solve(fig.add_subplot(gs[3, col]), solver, float(Y0[row, own_col]), float(C[row, own_col]),
                   hard_gain, shape, b0, f"({own_letters[col]}) Does not converge: {kind}")

    plot_solve(fig.add_subplot(gs[0, 0]), solver, float(Y0[r, ok_col]), float(C[r, ok_col]), hard_gain, shape, b0,
               f"(a) Newton-Raphson converges: {labels[1]}")
    plot_solve(fig.add_subplot(gs[0, 1]), solver, float(Y0[r, fail_col]), float(C[r, fail_col]), hard_gain, shape,
               b0, f"(b) Newton-Raphson does not converge: same filter and c, different y0")

    ax = fig.add_subplot(gs[1, 0])
    base = plt.get_cmap("viridis", max_its - 1)
    cmap = ListedColormap([base(i) for i in range(max_its - 1)] + ["0.75", "crimson"])
    norm = BoundaryNorm(np.arange(0.5, max_its + 2), cmap.N)
    mesh = ax.pcolormesh(Y0, C, basin, cmap=cmap, norm=norm, shading="nearest", rasterized=True)
    cbar = fig.colorbar(mesh, ax=ax, ticks=[1] + list(range(5, max_its, 5 if max_its <= 20 else 10)))
    cbar.set_label("iterations to converge")
    ax.legend(handles=[Patch(color="0.75", label="not converged: precision-limited"),
                       Patch(color="crimson", label="not converged: genuine")],
              fontsize=7, loc="lower left", framealpha=0.9)
    ax.scatter([Y0[r, ok_col], Y0[r, fail_col]], [C[r, fail_col]] * 2, c=["w", "k"], edgecolors="k", s=40)
    ax.annotate("(a)", (Y0[r, ok_col], C[r, ok_col]), xytext=(-22, 6), textcoords="offset points",
                color="w", fontweight="bold")
    ax.annotate("(b)", (Y0[r, fail_col], C[r, fail_col]), xytext=(6, 6), textcoords="offset points",
                color="w", fontweight="bold")
    for col, (_, row, own_col) in enumerate(own_cases):
        ax.scatter([Y0[row, own_col]], [C[row, own_col]], c="k", edgecolors="w", s=40)
        ax.annotate(f"({own_letters[col]})", (Y0[row, own_col], C[row, own_col]), xytext=(6, -14),
                    textcoords="offset points", color="w", fontweight="bold")
    ax.set_xlabel("initial guess y0 (V)")
    ax.set_ylabel("c = b0 * in + s (V)")
    ax.set_title(f"(c) Iterations by initial guess, {labels[1]} (b0 * g = {b0 * hard_gain:.2f})", fontsize=10)

    ax = fig.add_subplot(gs[1, 1])
    for col in range(2):
        ax.step(t * 1000, its[:, col], where="mid", lw=1, label=labels[col], color=f"C{col}")
        bad = genuine[:, col]
        ax.scatter(t[bad] * 1000, its[bad, col], color="crimson", s=12, zorder=3,
                   label="genuine failure" if col == 1 else None)
    ax.axhline(max_its, color="k", ls="--", lw=0.8, label="max iterations (not converged)")
    ax.set_xlabel("time (ms)")
    ax.set_ylabel("iterations")
    ax.set_title(f"(d) Module on a {args.input_amp:g} V {args.input_freq:g} Hz sine: iterations per sample",
                 fontsize=10)
    ax.legend(fontsize=7, loc="center right")

    for col, (data, title, cmap_name) in enumerate([
        (grid_mean, "(e) Module: mean iterations per sample", "viridis"),
        (grid_nc, "(f) Module: samples not converged (%), hatched where any are genuine", "magma"),
    ]):
        ax = fig.add_subplot(gs[2, col])
        mesh = ax.pcolormesh(F, G, data, cmap=cmap_name, shading="nearest", rasterized=True,
                             vmin=1 if col == 0 else 0, vmax=max(data.max(), 1e-9))
        fig.colorbar(mesh, ax=ax)
        if col == 1 and (grid_gen > 0).any():
            ax.contourf(F, G, grid_gen > 0, levels=[0.5, 1.5], colors="none", hatches=["////"])
        ax.scatter(freqs, gains, c=["C0", "C1"], edgecolors="w", s=60, zorder=3)
        ax.set_xscale("log")
        ax.set_xlabel("cutoff (Hz)")
        ax.set_ylabel("feedback gain")
        ax.set_title(title, fontsize=10)

    fig.savefig(args.output, dpi=120)
    print(f"Saved {args.output}")
    if args.show:
        plt.show()


if __name__ == "__main__":
    run(NEWTON_RAPHSON, __doc__, Path(__file__).with_name("newton_raphson.png"))
