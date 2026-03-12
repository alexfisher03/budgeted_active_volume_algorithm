import matplotlib as mpl

FIGSIZE = (6.0, 4.2)

SCHEDULER_STYLE = {
    "exact-optimal": {
        "color": "#000000",
        "marker": "D",
        "markersize": 4,
        "linewidth": 1.6,
        "linestyle": "-",
        "zorder": 10,
        "label": "exact-optimal (certified)",
    },
    "portfolio-best": {
        "color": "#1f5faa",
        "marker": "s",
        "markersize": 3.2,
        "linewidth": 1.5,
        "linestyle": "-",
        "zorder": 8,
        "label": "portfolio-best",
    },
    "oldest-live": {
        "color": "#d47020",
        "marker": "o",
        "markersize": 3.2,
        "linewidth": 1.5,
        "linestyle": "-",
        "zorder": 6,
        "label": "oldest-live",
    },
}

PLOT_ORDER = ["exact-optimal", "portfolio-best", "oldest-live"]

FIRST_FEASIBLE = {
    "exact-optimal": 7,
    "oldest-live": 18,
    "portfolio-best": 20,
}

VLINE_STYLE = {
    "exact-optimal": {"color": "#000000", "alpha": 0.35},
    "oldest-live": {"color": "#d47020", "alpha": 0.35},
    "portfolio-best": {"color": "#1f5faa", "alpha": 0.35},
}


def apply_style():
    mpl.rcParams.update({
        "font.family": "serif",
        "font.size": 10,
        "axes.titlesize": 11,
        "axes.labelsize": 11,
        "xtick.labelsize": 9,
        "ytick.labelsize": 9,
        "legend.fontsize": 7.5,
        "figure.dpi": 150,
        "savefig.dpi": 300,
        "savefig.bbox": "tight",
        "savefig.pad_inches": 0.08,
        "axes.facecolor": "white",
        "figure.facecolor": "white",
        "savefig.facecolor": "white",
    })
