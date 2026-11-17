#!/usr/bin/env python3
"""Independent multi-spin reference for experimental SpinSpec observation.

Reference Hamiltonian and operators are constructed using independent NumPy
matrix operations. This does not call the C implementation's builders and
does not reuse its diagonalization results. It compares all transition
frequencies and coherent group strengths, aggregating degeneracies.
Original example files are not modified.
"""
from __future__ import annotations

import csv
import math
import os
from pathlib import Path
import subprocess
import sys
import time

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
EXAMPLES = ROOT / "examples"
GAMMA_REF_HZ_PER_MT = 42577.47892

# We use a wider frequency tolerance at high field because the experimental
# CSV writes frequencies to 12 significant decimal digits.
def freq_tol(a: float, b: float) -> float:
    return max(2e-5, 3e-10 * max(abs(a), abs(b)))

def strength_tol(a: float, b: float) -> float:
    return 4e-7 * max(1.0, abs(a), abs(b))


def read_model(filename: str, corrected: str | None):
    path = EXAMPLES / filename
    lines = path.read_text(encoding="utf-8").splitlines()
    gammas = list(map(float, lines[1].split()))
    shifts = list(map(float, lines[2].split()))
    n = len(gammas)

    if corrected == "three_gamma":
        assert filename == "three_spins.set" and gammas[1] == 42578.0
        gammas[1] = 42.578
        lines[1] = " ".join(str(g) for g in gammas)
    elif corrected == "benzene_shifts":
        assert filename == "1_3_5_Trifluoro_benzene.set"
        assert len(shifts) == 4 and n == 6
        # Explicitly stated TEST ASSUMPTION, not a correction of the published
        # input: the two missing chemical shifts are assumed to be zero.
        shifts.extend([0.0, 0.0])
        lines[2] = " ".join(str(x) for x in shifts)
    elif corrected is not None:
        raise ValueError("Unknown correction type")

    if len(shifts) != n:
        raise ValueError(
            f"{filename}: {n} spins but only {len(shifts)} chemical shifts"
        )
    couplings = []
    for line in lines[4:]:
        s = line.strip()
        if s.startswith("Graph"):
            break
        if not s:
            continue
        a, b, j = s.split()
        a, b = int(a), int(b)
        if not (0 <= a < b < n):
            raise ValueError(f"Invalid J pair: {s}")
        couplings.append((a, b, float(j)))
    return lines, np.array(gammas) * 1000.0, np.array(shifts), couplings


def reference_spectrum(gammas, shifts, couplings, field_mt, group_ids):
    """Build and diagonalize H/h independently; compute all group operators."""
    nspins = len(gammas)
    dimension = 1 << nspins
    states = np.arange(dimension, dtype=np.int64)
    bits = np.array(
        [(states >> (nspins - 1 - k)) & 1 for k in range(nspins)]
    ).T
    z = 0.5 - bits.astype(np.float64)
    diag = field_mt * (z @ (gammas * (1.0 - shifts * 1e-6)))
    for i, j, coupling in couplings:
        diag += coupling * z[:, i] * z[:, j]
    hamiltonian = np.diag(diag)
    for i, j, coupling in couplings:
        other = states ^ (1 << (nspins-1-i)) ^ (1 << (nspins-1-j))
        allowed = bits[:, i] != bits[:, j]
        hamiltonian[states[allowed], other[allowed]] += coupling / 2.0

    if not np.array_equal(hamiltonian, hamiltonian.T):
        raise AssertionError("Reference Hamiltonian is not symmetric")
    eigenvalues, eigenvectors = np.linalg.eigh(hamiltonian)

    group_count = max(group_ids) + 1
    group_operators = [
        np.zeros((dimension, dimension)) for _ in range(group_count)
    ]
    for spin, group in enumerate(group_ids):
        other = states ^ (1 << (nspins - 1 - spin))
        group_operators[group][states, other] += (
            gammas[spin] / (2.0 * GAMMA_REF_HZ_PER_MT)
        )

    # Eigenstate pair strengths: phase/sign conventions cancel after squaring.
    upper = np.triu_indices(dimension, k=1)
    amps = np.stack([
        (eigenvectors.T @ operator @ eigenvectors)[upper]
        for operator in group_operators
    ], axis=1)
    total = amps.sum(axis=1) ** 2
    individual = amps ** 2
    interference = total - individual.sum(axis=1)
    values = np.column_stack((total, interference, individual))
    frequencies = np.abs(
        eigenvalues[upper[0]] - eigenvalues[upper[1]]
    )
    order = np.argsort(frequencies, kind="stable")
    return frequencies[order], values[order]


def check_spectrum(case_label, filename, field_mt, group_ids, corrected=None,
                   timeout_seconds=160):
    lines, gammas, shifts, couplings = read_model(filename, corrected)
    nspins = len(gammas)
    if len(group_ids) != nspins:
        raise ValueError(f"{case_label}: group count != spin count")
    if min(group_ids) < 0 or max(group_ids) > 9:
        raise ValueError("Invalid group identifiers")
    working_model = Path("multispin_reference.set")
    lines[3] = f"{field_mt:.12g}"
    working_model.write_text("\n".join(lines) + "\n", encoding="utf-8")
    result_path = Path("Sp_groups_00000.csv")
    result_path.unlink(missing_ok=True)

    cmd_env = dict(
        os.environ,
        SPINSPEC_OBSERVER_GROUPS=",".join(map(str, group_ids)),
        OMP_NUM_THREADS="2",
    )
    started = time.monotonic()
    proc = subprocess.run(
        ["./engine_matrix", str(working_model)],
        env=cmd_env, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=timeout_seconds,
    )
    if proc.returncode != 0:
        raise AssertionError(
            f"{case_label}, B={field_mt} mT: C engine failed: "
            + proc.stdout[-1500:]
        )
    if not result_path.exists():
        raise AssertionError(f"{case_label}: no output CSV")

    theory_f, theory_v = reference_spectrum(
        gammas, shifts, couplings, field_mt, group_ids
    )
    group_count = max(group_ids) + 1
    with result_path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    pairs = (1 << nspins) * ((1 << nspins) - 1) // 2
    if len(rows) != pairs:
        raise AssertionError(f"{case_label}: {len(rows)} rows != {pairs}")

    actual_f = np.array([float(r["frequency_hz"]) for r in rows])
    actual_v = np.array([
        [float(r["total_strength"]), float(r["interference"])] +
        [float(r[f"strength_g{g}"]) for g in range(group_count)]
        for r in rows
    ])
    if not np.all(np.isfinite(actual_v)) or not np.all(np.isfinite(actual_f)):
        raise AssertionError(f"{case_label}: nonfinite result")
    order = np.argsort(actual_f, kind="stable")
    actual_f, actual_v = actual_f[order], actual_v[order]

    if len(theory_f) != len(actual_f):
        raise AssertionError("Different number of transitions")
    differences = np.abs(actual_f - theory_f)
    allowed = np.maximum(
        2e-5, 3e-10 * np.maximum(np.abs(actual_f), np.abs(theory_f))
    )
    if np.any(differences > allowed):
        idx = int(np.argmax(differences / allowed))
        raise AssertionError(
            f"{case_label} B={field_mt}: frequency mismatch at {idx}: "
            f"C={actual_f[idx]:.12g}, reference={theory_f[idx]:.12g} Hz"
        )

    # GSL and LAPACK may choose different orthogonal bases within
    # degenerate eigenspaces. Only frequency-cluster sums are invariant.
    groups = []
    start = 0
    while start < len(theory_f):
        end = start + 1
        while end < len(theory_f) and abs(theory_f[end] - theory_f[end - 1]) <= freq_tol(theory_f[end], theory_f[end - 1]):
            end += 1
        groups.append((start, end))
        start = end

    max_error = 0.0
    for start, end in groups:
        expected = np.sum(theory_v[start:end, :], axis=0)
        observed = np.sum(actual_v[start:end, :], axis=0)
        errors = np.abs(expected - observed)
        max_error = max(max_error, float(np.max(errors)))
        for i, (a, b) in enumerate(zip(observed, expected)):
            if abs(a - b) > strength_tol(a, b):
                raise AssertionError(
                    f"{case_label} B={field_mt}, frequency cluster "
                    f"{theory_f[start]:.10g} Hz, component {i}: "
                    f"C={a:.12g}, reference={b:.12g}, "
                    f"delta={a-b:.6g}"
                )
    # Strong independent trace sum rules for all spin-flip operators.
    dim = 1 << nspins
    expected_group = [
        dim / 8.0 * sum((gammas[k] / GAMMA_REF_HZ_PER_MT)**2
                        for k in range(nspins) if group_ids[k] == g)
        for g in range(group_count)
    ]
    observed_integral = actual_v.sum(axis=0)
    for g, value in enumerate(expected_group):
        if abs(observed_integral[g + 2] - value) > strength_tol(observed_integral[g + 2], value):
            raise AssertionError(
                f"{case_label}: group {g} oscillator-strength sum-rule failed"
            )
    if abs(observed_integral[0] - sum(expected_group)) > strength_tol(observed_integral[0], sum(expected_group)):
        raise AssertionError(f"{case_label}: total sum-rule failed")
    if abs(observed_integral[1]) > strength_tol(observed_integral[1], 0.0):
        raise AssertionError(f"{case_label}: total cross term must vanish")

    duration = time.monotonic() - started
    print(
        f"PASS {case_label:23s} N={nspins} B={field_mt:g} mT "
        f"pairs={pairs:5d}, clusters={len(groups):5d}, "
        f"max dA={max_error:.3g}, "
        f"max dnu={np.max(differences):.3g} Hz, "
        f"time={duration:.2f}s",
        flush=True,
    )


def main():
    cases = [
        ("three_raw", "three_spins.set", (0.05,), [0, 0, 1], None),
        ("three_corrected", "three_spins.set", (0, 0.05, 10), [0, 0, 1], "three_gamma"),
        ("trimethyl_P", "Trimethyl_phosphate (one brench).set", (0, 0.05, 0.5, 5000), [0, 0, 0, 1], None),
        ("triethyl_P", "Triethyle_phosphate (one brench).set", (0, 0.05, 5000), [0, 0, 0, 0, 0, 1], None),
        ("trifluoro_ethanol", "2_2_2_TriFluoro_Ethanol.set", (0, 0.05, 1, 9394.7), [0, 0, 1, 1, 1, 0], None),
        ("trifluorobenzene_test", "1_3_5_Trifluoro_benzene.set", (0, 0.05, 9394.7), [0, 0, 0, 1, 1, 1], "benzene_shifts"),
        ("tetrafluoropropanol", "2_2_3_3_TetraFluoro_1-Propanol.set", (0.05,), [0, 0, 0, 1, 1, 2, 1, 0], None),
    ]
    # Validate the *actual* original files; never quietly fix their defects.
    issues = []
    for filename in ("three_spins.set", "1_3_5_Trifluoro_benzene.set"):
        ls = (EXAMPLES / filename).read_text(encoding="utf-8").splitlines()
        n = len(ls[1].split())
        m = len(ls[2].split())
        if m != n:
            issues.append(f"{filename}: {n} spin gammas, {m} chemical shifts")
        if filename == "three_spins.set" and float(ls[1].split()[1]) > 1000:
            issues.append(f"{filename}: second gamma is 42578 MHz/T, anomalous")
    assert len(issues) == 2, issues
    print("Original example input issues:", "; ".join(issues), flush=True)

    passed = 0
    for label, name, fields, gids, correction in cases:
        for field in fields:
            check_spectrum(label, name, field, gids, corrected=correction,
                           timeout_seconds=230 if len(gids) >= 8 else 90)
            passed += 1
    print(f"PASS multi-spin reference: {passed} C-vs-NumPy cases; {len(issues)} input issues noted")


if __name__ == "__main__":
    main()
