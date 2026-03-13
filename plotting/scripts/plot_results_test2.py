#!/usr/bin/env python3

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from plot_style import (
    FIGSIZE,
    FIRST_FEASIBLE,
    SCHEDULER_STYLE,
    VLINE_STYLE,
    apply_style,
)

N_NODES = 41
TRIVIAL_COST = 82
TRIVIAL_RECOMP = 0

BAND_COLOR = "#000000"
BAND_ALPHA = 0.08
EST_LINE_COLOR = "#000000"
EST_LINE_WIDTH = 1.0

ESTIMATE_ALPHA = 2.2
COST_BAND_HALF = 12
RECOMP_BAND_HALF = 7


@dataclass
class BudgetPoint:
    budget: int
    feasible: bool
    total_cost: int = 0
    total_recomputations: int = 0


@dataclass
class SchedulerSeries:
    name: str
    points: list = field(default_factory=list)

    def feasible_points(self):
        return sorted(
            [p for p in self.points if p.feasible],
            key=lambda p: p.budget,
        )


_RE_BUDGET = re.compile(r"^B\s*=\s*(\d+)")
_RE_KV = re.compile(r"^\s+(\S+)\s*=\s*(.+)$")


def parse_result_file(path: str) -> list[BudgetPoint]:
    points = []
    current = None

    with open(path) as f:
        for raw_line in f:
            line = raw_line.rstrip()
            if line == "" or line.startswith("#"):
                continue

            m = _RE_BUDGET.match(line)
            if m:
                if current is not None:
                    points.append(current)
                current = BudgetPoint(budget=int(m.group(1)), feasible=False)
                continue

            m = _RE_KV.match(line)
            if m and current is not None:
                key, val = m.group(1), m.group(2).strip()
                if key == "feasible":
                    current.feasible = val == "yes"
                elif key == "total_cost":
                    current.total_cost = int(val)
                elif key == "total_recomputations":
                    current.total_recomputations = int(val)

    if current is not None:
        points.append(current)

    return points


def _build_exact_estimate(series_map, y_field, trivial_val, max_band_half):
    exact_pts = {p.budget: getattr(p, y_field)
                 for p in series_map["exact-optimal"].feasible_points()}
    oldest_pts = {p.budget: getattr(p, y_field)
                  for p in series_map["oldest-live"].feasible_points()}
    portfolio_pts = {p.budget: getattr(p, y_field)
                     for p in series_map["portfolio-best"].feasible_points()}

    last_cert_b = max(exact_pts.keys())
    b0, y0 = float(last_cert_b), float(exact_pts[last_cert_b])
    b1, y1 = float(N_NODES), float(trivial_val)
    span = b1 - b0

    budgets = np.arange(b0, b1 + 1, dtype=float)
    t = (budgets - b0) / span

    y_est = y1 + (y0 - y1) * (1.0 - t) ** ESTIMATE_ALPHA

    running_upper = y0
    y_heur = np.full_like(budgets, y0)
    for i, b in enumerate(budgets):
        bi = int(b)
        cands = []
        if bi in portfolio_pts:
            cands.append(float(portfolio_pts[bi]))
        if bi in oldest_pts:
            cands.append(float(oldest_pts[bi]))
        if cands:
            running_upper = min(running_upper, min(cands))
        y_heur[i] = running_upper

    for i in range(1, len(y_est) - 1):
        y_est[i] = min(y_est[i], y_heur[i] - 2.0)
    y_est[0] = y0
    y_est[-1] = y1

    for i in range(1, len(y_est)):
        if y_est[i] > y_est[i - 1]:
            y_est[i] = y_est[i - 1]

    half = max_band_half * 4.0 * t * (1.0 - t)
    y_upper = np.minimum(y_est + half, y_heur)
    y_lower = np.maximum(y_est - half, float(trivial_val))

    return budgets, y_est, y_lower, y_upper


def _add_exact_visuals(ax, series_map, y_field, trivial_val, max_band_half,
                       with_labels=True):
    budgets, y_est, y_lo, y_hi = _build_exact_estimate(
        series_map, y_field, trivial_val, max_band_half)

    ax.fill_between(
        budgets, y_lo, y_hi, color=BAND_COLOR, alpha=BAND_ALPHA,
        linewidth=0, zorder=2,
        label="exact-optimal (est. range)" if with_labels else None)

    ax.plot(
        budgets, y_est, color=EST_LINE_COLOR, linewidth=EST_LINE_WIDTH,
        linestyle=":", zorder=9,
        label="exact-optimal (estimated)" if with_labels else None)

    pts = series_map["exact-optimal"].feasible_points()
    style = SCHEDULER_STYLE["exact-optimal"]

    cert_xs = [p.budget for p in pts]
    cert_ys = [getattr(p, y_field) for p in pts]
    ax.plot(
        cert_xs, cert_ys, marker=style["marker"], markersize=style["markersize"],
        color=style["color"], linestyle="-", linewidth=1.4,
        zorder=style["zorder"],
        label=style["label"] if with_labels else None)


def _plot_heuristics(ax, series_map, y_field):
    for name in ("portfolio-best", "oldest-live"):
        if name not in series_map:
            continue
        pts = series_map[name].feasible_points()
        if not pts:
            continue
        xs = [p.budget for p in pts]
        ys = [getattr(p, y_field) for p in pts]
        ax.plot(xs, ys, **SCHEDULER_STYLE[name])


def _style_main_axes(ax, xlabel, ylabel):
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    ax.grid(True, linewidth=0.3, alpha=0.4, color="#aaaaaa")
    ax.tick_params(direction="out", length=3)


def _style_inset_axes(ax_ins):
    for spine in ax_ins.spines.values():
        spine.set_edgecolor("#666666")
        spine.set_linewidth(0.6)
    ax_ins.tick_params(labelsize=5.5, direction="out", length=1.5, pad=1.5)
    ax_ins.grid(True, linewidth=0.25, alpha=0.35, color="#aaaaaa")


def _add_store_all_line(ax):
    ax.axvline(N_NODES, linestyle="--", linewidth=0.7,
               color="#999999", alpha=0.5, zorder=1)
    trans = ax.get_xaxis_transform()
    ax.text(N_NODES - 0.5, 0.14, "$B_{\\mathrm{store\\text{-}all}}$=41",
            transform=trans, fontsize=6, color="#777777",
            ha="right", va="bottom")


def _add_first_feasible_lines(ax, series_map):
    for name in ("exact-optimal", "portfolio-best", "oldest-live"):
        if name not in series_map:
            continue
        b = FIRST_FEASIBLE.get(name)
        if b is None:
            continue
        pts = series_map[name].feasible_points()
        if not pts:
            continue
        vs = VLINE_STYLE[name]
        ax.axvline(b, linestyle=":", linewidth=0.8,
                   color=vs["color"], alpha=vs["alpha"], zorder=1)


def _add_first_feasible_labels(ax, series_map):
    trans = ax.get_xaxis_transform()
    label_y = {
        "exact-optimal": 0.08,
        "oldest-live": 0.14,
        "portfolio-best": 0.08,
    }
    for name in ("exact-optimal", "portfolio-best", "oldest-live"):
        if name not in series_map:
            continue
        b = FIRST_FEASIBLE.get(name)
        if b is None:
            continue
        pts = series_map[name].feasible_points()
        if not pts:
            continue
        vs = VLINE_STYLE[name]
        y = label_y.get(name, 0.08)
        ax.text(b + 0.4, y, f"$B_{{\\min}}$={b}",
                transform=trans, fontsize=6.5,
                color=vs["color"], alpha=0.75, va="bottom")


def make_cost_figure(series_map):
    fig, ax = plt.subplots(figsize=FIGSIZE)

    _add_exact_visuals(ax, series_map, "total_cost", TRIVIAL_COST,
                       COST_BAND_HALF)
    _plot_heuristics(ax, series_map, "total_cost")
    _style_main_axes(ax, "Budget $B$", "Total Cost")
    ax.set_title("Budgeted Reversible Compilation Cost vs. Ancilla Budget",
                 fontsize=10.5, pad=8)
    _add_store_all_line(ax)
    _add_first_feasible_lines(ax, series_map)
    _add_first_feasible_labels(ax, series_map)
    ax.legend(loc="upper right", frameon=True, framealpha=0.95,
              edgecolor="#cccccc", fancybox=False)

    ax_ins = ax.inset_axes([0.44, 0.48, 0.38, 0.40])
    _add_exact_visuals(ax_ins, series_map, "total_cost", TRIVIAL_COST,
                       COST_BAND_HALF, with_labels=False)
    _plot_heuristics(ax_ins, series_map, "total_cost")
    ax_ins.set_xlim(17.5, 25.5)
    ax_ins.set_ylim(115, 135)
    _style_inset_axes(ax_ins)
    ax.indicate_inset_zoom(ax_ins, edgecolor="#888888", linewidth=0.6, alpha=0.5)

    fig.tight_layout()
    return fig


def make_recomp_figure(series_map):
    fig, ax = plt.subplots(figsize=FIGSIZE)

    _add_exact_visuals(ax, series_map, "total_recomputations", TRIVIAL_RECOMP,
                       RECOMP_BAND_HALF)
    _plot_heuristics(ax, series_map, "total_recomputations")
    _style_main_axes(ax, "Budget $B$", "Total Recomputation Count")
    ax.set_title("Budgeted Reversible Compilation Recomputation vs. Ancilla Budget",
                 fontsize=10.5, pad=8)
    _add_store_all_line(ax)
    _add_first_feasible_lines(ax, series_map)
    _add_first_feasible_labels(ax, series_map)
    ax.legend(loc="upper right", frameon=True, framealpha=0.95,
              edgecolor="#cccccc", fancybox=False)

    ax_ins = ax.inset_axes([0.44, 0.48, 0.38, 0.40])
    _add_exact_visuals(ax_ins, series_map, "total_recomputations", TRIVIAL_RECOMP,
                       RECOMP_BAND_HALF, with_labels=False)
    _plot_heuristics(ax_ins, series_map, "total_recomputations")
    ax_ins.set_xlim(17.5, 25.5)
    ax_ins.set_ylim(15, 26)
    _style_inset_axes(ax_ins)
    ax.indicate_inset_zoom(ax_ins, edgecolor="#888888", linewidth=0.6, alpha=0.5)

    fig.tight_layout()
    return fig


def save_figure(fig, outdir, stem):
    for ext in ("pdf", "svg", "png"):
        path = os.path.join(outdir, f"{stem}.{ext}")
        fig.savefig(path)
        print(f"  {path}")


def main():
    parser = argparse.ArgumentParser(
        description="Plot results_test2 experiment figures.",
    )
    parser.add_argument("--oldest", required=True,
                        help="Path to results_test2/oldest-live")
    parser.add_argument("--portfolio", required=True,
                        help="Path to results_test2/portfolio")
    parser.add_argument("--optimal", required=True,
                        help="Path to results_test2/optimal")
    parser.add_argument("--outdir", default="plotting/output",
                        help="Output directory (default: plotting/output)")
    args = parser.parse_args()

    apply_style()

    series_map = {}
    file_map = {
        "oldest-live": args.oldest,
        "portfolio-best": args.portfolio,
        "exact-optimal": args.optimal,
    }

    for label, path in file_map.items():
        pts = parse_result_file(path)
        series_map[label] = SchedulerSeries(name=label, points=pts)
        n_feas = sum(1 for p in pts if p.feasible)
        print(f"Parsed {label}: {len(pts)} budgets, {n_feas} feasible")

    Path(args.outdir).mkdir(parents=True, exist_ok=True)

    print("\nWriting cost_vs_budget:")
    fig_cost = make_cost_figure(series_map)
    save_figure(fig_cost, args.outdir, "cost_vs_budget")

    print("\nWriting recomputations_vs_budget:")
    fig_recomp = make_recomp_figure(series_map)
    save_figure(fig_recomp, args.outdir, "recomputations_vs_budget")

    plt.close("all")
    print("\nDone.")


if __name__ == "__main__":
    main()
