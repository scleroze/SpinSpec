#!/usr/bin/env python3
"""Render reproducible plots from the experimental SpinSpec C engine.

The source of every plotted intensity is Sp_invariant_00000.csv as
produced by the executable in the experimental branch.
Run at repository root:
    /usr/bin/python3 scripts/plot_experimental_spectrum.py
"""
from __future__ import annotations

import csv
import os
from pathlib import Path
import shutil
import subprocess

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
ENGINE_DIR = ROOT / "bin"
MODEL = ROOT / "examples" / "Trimethyl_phosphate (one brench).set"
OUT = ROOT / "spectrum-preview"
GROUPS = "0,0,0,1"

def grouped_positive_lines(records, tolerance=1e-6):
    """Collapse equal-frequency block lines; all zero-frequency blocks sum."""
    lines = sorted(
        (float(r["frequency_hz"]), float(r["total_strength"]))
        for r in records
    )
    result = []
    for f, intensity in lines:
        if result and abs(f - result[-1][0]) <= tolerance:
            result[-1] = (result[-1][0], result[-1][1] + intensity)
        else:
            result.append((f, intensity))
    return [(f, y) for f, y in result if y > 1e-8]


def compute(field_mt, tag):
    model_lines = MODEL.read_text(encoding="utf-8").splitlines()
    model_lines[3] = format(field_mt, ".12g")
    input_path = ENGINE_DIR / ("preview_" + tag + ".set")
    input_path.write_text("\n".join(model_lines) + "\n", encoding="utf-8")
    csv_path = ENGINE_DIR / "Sp_invariant_00000.csv"
    csv_path.unlink(missing_ok=True)
    env = dict(os.environ, OMP_NUM_THREADS="1",
               SPINSPEC_OBSERVER_GROUPS=GROUPS)
    proc = subprocess.run(
        ["./engine_matrix", input_path.name],
        cwd=ENGINE_DIR, env=env, capture_output=True, text=True, timeout=120)
    if proc.returncode != 0:
        raise RuntimeError("C engine failed: " + proc.stdout[-800:] + proc.stderr[-800:])
    if not csv_path.is_file():
        raise RuntimeError("Experimental invariant CSV not generated")
    shutil.copyfile(csv_path, OUT / ("Sp_invariant_" + tag + ".csv"))
    with csv_path.open(newline="", encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    return grouped_positive_lines(rows)


def draw_sticks(ax, items, title, min_x=None, max_x=None):
    if not items:
        raise AssertionError("No spectral lines to plot")
    for x, y in items:
        ax.vlines(x, 0, y, color="#087ba3", linewidth=2.6)
        ax.plot([x], [y], "o", ms=4, color="#087ba3")
    ax.set_ylim(0, 1.23 * max(y for _, y in items))
    if min_x is not None and max_x is not None:
        ax.set_xlim(min_x, max_x)
    ax.set_ylabel("Relative line strength")
    ax.set_title(title, fontsize=14)
    ax.grid(axis="y", alpha=.25)


def render_zero(lines):
    fig, axes = plt.subplots(2, 1, figsize=(11, 7.5), layout="constrained")
    fig.suptitle(r"Zero-field SpinSpec (C): $^1$H$_3$–$^{31}$P, $J=11$ Hz",
                 fontsize=17, weight="semibold")
    full = [(f, s) for f, s in lines if f < 33.1]
    visible = [(f, s) for f, s in full if f > 1e-6]
    draw_sticks(axes[0], full, "Complete one-sided spectrum, including DC", -1, 34)
    draw_sticks(axes[1], visible, "Nonzero-frequency lines (magnified)", -1, 34)
    for ax, pairlist in zip(axes, (full, visible)):
        for f, intensity in pairlist:
            ax.annotate(f"{f:.0f} Hz   {intensity:.4f}", (f, intensity),
                        xytext=(7, 7), textcoords="offset points", fontsize=10)
    axes[1].set_xlabel("Frequency (Hz)")
    fig.savefig(OUT / "SpinSpec_B0_zero.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def render_weak(lines):
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.2), layout="constrained")
    fig.suptitle(r"SpinSpec (C): $B_0=0.05$ mT, $^1$H$_3$–$^{31}$P, $J=11$ Hz",
                 fontsize=16, weight="semibold")
    windows = ((825, 895), (2105, 2150))
    for ax, (a, b) in zip(axes, windows):
        displayed = [(f, s) for f, s in lines if a <= f <= b]
        draw_sticks(ax, displayed, f"Frequency window {a}–{b} Hz", a, b)
        ax.set_xlabel("Frequency (Hz)")
    fig.savefig(OUT / "SpinSpec_B0_0p05mT.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def main():
    OUT.mkdir(exist_ok=True)
    zero = compute(0.0, "zero")
    weak = compute(0.05, "0p05mT")
    render_zero(zero)
    render_weak(weak)
    (OUT / "README.txt").write_text(
        "SpinSpec experimental gamma-weighted, energy-subspace-invariant\n"
        "transverse observation model for 3 x 1H + 31P, J=11 Hz.\n"
        "These are one-sided, relative, stick spectra with a half-weight\n"
        "zero-frequency block. They do not model initial populations,\n"
        "pulse preparation, relaxation or receiver response.\n"
        "Source: bin/engine_matrix.c, bin/observer_invariant.h.\n"
        "GROUPS=0,0,0,1. Fields: B0=0 and B0=0.05 mT.\n",
        encoding="utf-8"
    )
    for label, lines in (("zero", zero), ("0.05 mT", weak)):
        meaningful = [(round(f, 6), round(s, 7)) for f, s in lines if s > 0.01]
        print(f"Field {label}: {meaningful}", flush=True)
    print("Plots created in spectrum-preview/", flush=True)

if __name__ == "__main__":
    main()
