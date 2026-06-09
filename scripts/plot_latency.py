#!/usr/bin/env python3
"""Plot latency-by-percentile curves from the benchmark's HDR CSV exports.

Run the benchmark first (it writes bench/results/hdr_*.csv), then:

    python scripts/plot_latency.py

Produces bench/results/latency.png -- a classic HdrHistogram "latency by
percentile" chart with a logarithmic percentile axis so the tail is visible.
"""
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "..", "bench", "results")
OPS = ["add", "cancel", "match"]


def load(op):
    path = os.path.join(RESULTS_DIR, f"hdr_{op}.csv")
    percentiles, latencies = [], []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            p = float(row["percentile"])
            if p >= 100.0:  # avoid div-by-zero on the log axis
                continue
            percentiles.append(p)
            latencies.append(float(row["latency_ns"]))
    return percentiles, latencies


def main():
    fig, ax = plt.subplots(figsize=(9, 5.5))
    found = False
    for op in OPS:
        try:
            pcts, lat = load(op)
        except FileNotFoundError:
            continue
        found = True
        # x = 1/(1-p): standard HDR percentile axis (90% -> 10, 99% -> 100, ...)
        x = [1.0 / (1.0 - p / 100.0) for p in pcts]
        ax.plot(x, lat, marker="o", markersize=3, label=op)

    if not found:
        print("No bench/results/hdr_*.csv found. Run build/bench/bench_main first.",
              file=sys.stderr)
        return 1

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Percentile  (1 / (1 - p));  90%=10, 99%=100, 99.9%=1000)")
    ax.set_ylabel("Latency (ns)")
    ax.set_title("FlatBook operation latency by percentile")
    ax.grid(True, which="both", ls=":", alpha=0.5)
    ax.legend()
    fig.tight_layout()

    out = os.path.join(RESULTS_DIR, "latency.png")
    fig.savefig(out, dpi=120)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
