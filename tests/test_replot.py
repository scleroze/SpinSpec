#!/usr/bin/env python3
"""Numerical regression: saved-transition replot vs complete engine broadening.

Run from the repository root after compiling:
    bin/engine_matrix, bin/matrix_graph and bin/replot_spectrum.
Requires Gnuplot and GSL as in the existing C CI.
"""
from pathlib import Path
from tempfile import TemporaryDirectory
import math
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(cmd, folder):
    process = subprocess.run(cmd, cwd=folder, capture_output=True,
                             text=True, timeout=60, check=False)
    assert process.returncode == 0, (cmd, process.stdout, process.stderr[-4000:])


def load_curve(path):
    rows = path.read_text().splitlines()[1:]
    return [tuple(map(float, row.split())) for row in rows]


def check(model, bfield):
    original = (ROOT / "examples" / model).read_text().splitlines()
    original[3] = str(bfield)
    graph_row = next(i for i, line in enumerate(original)
                     if line.startswith("Graph "))
    selected = [int(v) for v in original[graph_row].split()[1:]]
    original[graph_row + 1] = " ".join("0" for _ in selected)
    original[graph_row + 2] = "0.5 0"
    spin_gamma = [float(v) for v in original[1].split()]
    spin_shift = [float(v) for v in original[2].split()]
    couplings = {}
    for line in original[4:graph_row]:
        if line.strip():
            a, b, j = line.split()
            couplings[(int(a), int(b))] = float(j)
    total_abs_j = sum(abs(v) for (a,b), v in couplings.items() if a<b)

    with TemporaryDirectory(prefix="spinspec-replot-test-") as tmp:
        folder = Path(tmp)
        (folder / "input.set").write_text("\n".join(original) + "\n")
        for binary in ("matrix_graph", "group_graph"):
            (folder / binary).symlink_to(ROOT / "bin" / binary)
        run([str(ROOT / "bin" / "engine_matrix"), "input.set"], folder)
        assert (folder / "Sp_tolerance_00000.dat").is_file()
        assert (folder / "Sp_dc_00000.dat").is_file()
        reference = {spin: load_curve(folder / f"Res_{spin}_00000.dat")
                     for spin in selected}
        spectrum_before = (folder / "Sp_00000.dat").read_bytes()
        dc_before = (folder / "Sp_dc_00000.dat").read_bytes()
        params = [
            f"{len(spin_gamma)} {len(selected)} {bfield} 0.5 0 nan nan {total_abs_j}"
        ]
        params += [f"{g} {sh}" for g, sh in zip(spin_gamma, spin_shift)]
        params += [str(spin) for spin in selected]
        (folder / "plot_params.dat").write_text("\n".join(params) + "\n")
        run([str(ROOT / "bin" / "replot_spectrum"), "plot_params.dat"], folder)
        for spin in selected:
            fresh = load_curve(folder / f"Res_{spin}_00000.dat")
            assert len(fresh) == len(reference[spin]) == 2000
            diffs = [abs(a[1] - b[1]) for a, b in zip(fresh, reference[spin])]
            max_diff = max(diffs)
            assert max_diff < 2e-5, (model, bfield, spin, max_diff)
            assert all(abs(a[0]-b[0]) <= 1e-5
                       for a,b in zip(fresh,reference[spin]))
        assert (folder / "Sp_00000.dat").read_bytes() == spectrum_before
        assert (folder / "Sp_dc_00000.dat").read_bytes() == dc_before
        print(f"PASS replot parity: {model}, B0={bfield} mT, {len(selected)} panels")


if __name__ == "__main__":
    check("two_spins.set", 3000)
    check("three_spins.set", 0)
    check("fluoroethane.set", 0)
    print("REPLOT PARITY: PASS")
