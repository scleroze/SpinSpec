#!/usr/bin/env python3
"""Independent analytic regression: heteronuclear spin-1/2 pair, J=11 Hz.

This test does not diagonalize a Hamiltonian. All four nonzero transverse
transition strengths are obtained from the exact two-level mixing angle.
The two spin-forbidden transitions are included with zero strength.
At B=0, strengths are summed over degenerate energy subspaces.
"""
from __future__ import annotations

import csv
import math
import os
from pathlib import Path
import subprocess

G_H = 42_578.0     # Hz/mT = 42.578 MHz/T
G_P = 17_240.0     # Hz/mT = 17.240 MHz/T
G_REF = 42_577.47892
J = 11.0
FIELDS_MT = (0.0, 1.0e-6, 1.0e-3, 0.05, 0.5, 10.0)
FREQ_TOL_HZ = 2e-6
STRENGTH_TOL = 2e-8
COLUMNS = ("total_strength", "strength_g0", "strength_g1", "interference")


def analytic_pairs(field_mt: float) -> list[tuple[float, dict[str, float]]]:
    """Frequency, strength components from explicit closed-form eigenstates."""
    vh, vp = G_H * field_mt, G_P * field_mt
    delta = vh - vp
    gap = math.hypot(delta, J)
    c = math.sqrt((1.0 + delta / gap) / 2.0)
    s = math.sqrt((1.0 - delta / gap) / 2.0)  # positive J
    e_uu = (vh + vp) / 2.0 + J / 4.0
    e_dd = -(vh + vp) / 2.0 + J / 4.0
    e_plus = -J / 4.0 + gap / 2.0
    e_minus = -J / 4.0 - gap / 2.0
    a, b = G_H / G_REF, G_P / G_REF

    # (nu, <i|O_H|j>, <i|O_P|j>) for eigenstates
    transitions = (
        (abs(e_uu - e_plus), a * s / 2, b * c / 2),
        (abs(e_dd - e_plus), a * c / 2, b * s / 2),
        (abs(e_uu - e_minus), a * c / 2, -b * s / 2),
        (abs(e_dd - e_minus), -a * s / 2, b * c / 2),
        (abs(e_uu - e_dd), 0.0, 0.0),
        (abs(e_plus - e_minus), 0.0, 0.0),
    )

    result = []
    for nu, dh, dp in transitions:
        result.append((nu, {
            "total_strength": (dh + dp) ** 2,
            "strength_g0": dh ** 2,
            "strength_g1": dp ** 2,
            "interference": 2 * dh * dp,
        }))
    return result


def accum_at_frequencies(
    pairs: list[tuple[float, dict[str, float]]]
) -> list[tuple[float, dict[str, float]]]:
    """Merge degenerate lines (individual eigenvectors can be nonunique)."""
    groups: list[tuple[float, dict[str, float]]] = []
    for nu, values in pairs:
        for previous_nu, prior in groups:
            if abs(nu - previous_nu) <= FREQ_TOL_HZ:
                for key in COLUMNS:
                    prior[key] += values[key]
                break
        else:
            groups.append((nu, values.copy()))
    return groups


def run_case(field_mt: float) -> tuple[float, float]:
    model = Path("test_hp_analytic.set")
    model.write_text(
        f"#HP_analytic\\n42.578 17.240\\n0 0\\n{field_mt:.12g}\\n"
        "0 1 11\\n\\nGraph 0 1\\n1.2 1.2\\n0.1 100\\n",
        encoding="ascii",
    )
    env = dict(os.environ, SPINSPEC_OBSERVER_GROUPS="0,1", OMP_NUM_THREADS="1")
    proc = subprocess.run(
        ["./engine_matrix", str(model)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        env=env, timeout=45,
    )
    if proc.returncode != 0:
        raise AssertionError(
            f"C engine failed at B={field_mt} mT:\n{proc.stdout[-4000:]}"
        )
    csvpath = Path("Sp_groups_00000.csv")
    if not csvpath.is_file():
        raise AssertionError(f"Output not found at B={field_mt} mT")
    with csvpath.open(newline="") as f:
        lines = list(csv.DictReader(f))
    if len(lines) != 6:
        raise AssertionError(f"Expected 6 transitions, got {len(lines)}")

    expected = accum_at_frequencies(analytic_pairs(field_mt))
    observed: list[tuple[float, dict[str, float]]] = [
        (nu, {key: 0.0 for key in COLUMNS}) for nu, _ in expected
    ]
    counts = [0] * len(expected)
    for line in lines:
        freq = float(line["frequency_hz"])
        candidates = [
            i for i, (nu, _) in enumerate(expected)
            if abs(freq - nu) <= FREQ_TOL_HZ
        ]
        if len(candidates) != 1:
            raise AssertionError(
                f"B={field_mt}, nu={freq}: expected exactly one match, "
                f"got {candidates} from {[x[0] for x in expected]}"
            )
        target = candidates[0]
        counts[target] += 1
        for key in COLUMNS:
            value = float(line[key])
            if not math.isfinite(value):
                raise AssertionError(f"Non-finite {key} at B={field_mt}")
            observed[target][1][key] += value

        closure = (float(line["strength_g0"]) +
                   float(line["strength_g1"]) +
                   float(line["interference"]))
        if abs(float(line["total_strength"]) - closure) > STRENGTH_TOL:
            raise AssertionError("Transition strength closure failed")

    sum_total = 0.0
    max_deviation = 0.0
    for (nu, theory), (_, actual) in zip(expected, observed):
        for key in COLUMNS:
            error = abs(theory[key] - actual[key])
            max_deviation = max(error, max_deviation)
            if error > STRENGTH_TOL:
                raise AssertionError(
                    f"B={field_mt}, nu={nu:.12g}, {key}: "
                    f"theory={theory[key]:.12g} actual={actual[key]:.12g}"
                )
        sum_total += actual["total_strength"]

    # The field-independent transverse strength sum rule.
    a, b = G_H / G_REF, G_P / G_REF
    expected_sum = (a*a + b*b) / 2.0
    if abs(sum_total - expected_sum) > STRENGTH_TOL:
        raise AssertionError(
            f"Sum rule failed at B={field_mt}: {sum_total} != {expected_sum}"
        )
    if field_mt == 0.0:
        at_J = next(
            row for nu, row in observed if abs(nu - J) < FREQ_TOL_HZ
        )
        true_J = (a - b)**2 / 4
        if abs(at_J["total_strength"] - true_J) > STRENGTH_TOL:
            raise AssertionError("Singlet-triplet line strength mismatch")
        print(f"  B=0: 11-Hz singlet-triplet strength {at_J['total_strength']:.9f}")
    print(
        f"B={field_mt:10g} mT: {len(lines)} pairs, "
        f"{len(expected)} distinct frequencies, "
        f"total strength={sum_total:.9f}, max error={max_deviation:.2g}"
    )
    return sum_total, max_deviation


def main() -> None:
    for field_mt in FIELDS_MT:
        run_case(field_mt)
    print("PASS: all analytic 1H-31P comparisons, including B=0, passed")


if __name__ == "__main__":
    main()
