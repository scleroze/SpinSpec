#!/usr/bin/env python3
"""Regression: the ordinary spectrum and the total group observer are identical.

Only nonzero-frequency block transitions are compared to the ordinary
per-eigenvector pairs. The invariant DC convention includes diagonal
matrix elements; these are not exported by the ordinary transition CSV.
"""
import csv
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
EXAMPLES = ROOT / "examples"
MODELS = [
    ("two_spins.set", "0,1"),
    ("Trimethyl_phosphate (one brench).set", "0,0,0,1"),
]
FIELDS = (0.0, 0.000434, 0.05)
TOL_F = 2e-6
TOL_S = 1e-8

def run_engine(env):
    p = subprocess.run(["./engine_matrix", "unified_observer.set"],
                       env=env, capture_output=True, text=True, timeout=90)
    if p.returncode:
        raise AssertionError(p.stdout[-1500:] + p.stderr[-1500:])

def read_ordinary():
    rows = []
    with open("Sp_00000.dat", encoding="utf-8") as f:
        next(f)
        for line in f:
            v = line.split()
            rows.append((float(v[1]), float(v[2])))
    return rows

def read_invariant():
    with open("Sp_invariant_00000.csv", newline="") as f:
        return list(csv.DictReader(f))

def compare():
    for filename, gids in MODELS:
        for field in FIELDS:
            lines = (EXAMPLES / filename).read_text(encoding="utf-8").splitlines()
            lines[3] = format(field, ".12g")
            Path("unified_observer.set").write_text("\n".join(lines)+"\n", encoding="utf-8")
            env = dict(os.environ, OMP_NUM_THREADS="1")
            env.pop("SPINSPEC_OBSERVER_GROUPS", None)
            run_engine(env)
            ordinary_without_groups = read_ordinary()
            env["SPINSPEC_OBSERVER_GROUPS"] = gids
            run_engine(env)
            ordinary_with_groups = read_ordinary()
            if len(ordinary_without_groups) != len(ordinary_with_groups):
                raise AssertionError("Ordinary pair count changed")
            for (f1,s1),(f2,s2) in zip(ordinary_without_groups,ordinary_with_groups):
                assert abs(f1-f2) < TOL_F
                assert abs(s1-s2) < TOL_S
            invariant = read_invariant()
            significant = [row for row in invariant if float(row["frequency_hz"]) > 1e-8]
            # Different energy-block pairs can share an identical transition
            # frequency. Compare their sum once per frequency cluster.
            compared = []
            for row in significant:
                freq = float(row["frequency_hz"])
                if any(abs(freq - known) < TOL_F for known in compared):
                    continue
                compared.append(freq)
                expected = sum(float(x["total_strength"]) for x in significant
                               if abs(float(x["frequency_hz"])-freq) < TOL_F)
                observed = sum(strength for f,strength in ordinary_with_groups
                               if abs(f-freq) < TOL_F and f > 1e-8)
                if abs(observed-expected) > TOL_S:
                    raise AssertionError(
                        f"{filename}, B={field}, freq={freq}: "
                        f"ordinary={observed}, invariant={expected}")
            max_finite = max((s for f,s in ordinary_with_groups if f > 1e-8), default=0)
            if field == 0 and filename.startswith("Trimethyl") and max_finite < 1e-3:
                raise AssertionError("H3-P zero-field J lines disappeared")
            print(f"PASS unified: {filename}, B={field:g} mT, "
                  f"finite-blocks={len(significant)}", flush=True)

if __name__ == "__main__":
    compare()
