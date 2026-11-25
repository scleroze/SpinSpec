# SpinSpec

SpinSpec is a spin-1/2 nuclear magnetic resonance (NMR) spectrum simulator. It constructs and diagonalizes a coupled-spin Hamiltonian and computes transition frequencies and **relative** spectral line strengths. A C numerical engine produces discrete transitions, Lorentz-broadened spectra and JPEG plots. This repository also contains a FastAPI-based web front end.

Online project: https://nmrspec.org

[![Zenodo DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.23696822.svg)](https://doi.org/10.5281/zenodo.23696822)

**Current release:** [SpinSpec v1.2.0](https://github.com/scleroze/SpinSpec/releases/tag/v1.2.0) (25 November 2026). The [v1.0.0 archived scientific release](https://doi.org/10.5281/zenodo.23696822) has DOI [10.5281/zenodo.23696822](https://doi.org/10.5281/zenodo.23696822); that DOI refers to v1.0.0, not v1.2.0.

## What's new in SpinSpec 1.2.0

- Exact symmetric Hamiltonian diagonalization is partitioned by conserved total spin projection, **M_z**, with the full product-basis Hamiltonian still constructed before individual sectors are diagonalized using GSL.
- Faster signed gamma-weighted observer transformations. The measured nine-spin benchmark speedups relative to the reference engine were approximately **22× at 3 T** and **49× at zero field**; results depend on the model and hardware. The **public web calculator is limited to eight spins**.
- The web interface automatically **replots** when only linewidth, plot windows, or independent X/Y limits change; the previously computed transitions are reused without diagonalization.
- One positive **FWHM** field, independent optional X and Y bounds (including negative interference), automatic panel scaling, decimal point/comma browser input, and backward-compatible `.set` files.
- Ordinary and group-resolved images use a unified C/Gnuplot renderer; both modes use the same signed gamma-weighted transverse observation operator. Group strengths, signed interference and invariant CSV exports remain available.

See the [v1.2.0 release notes](docs/release-notes-v1.2.md), the [web tutorial](https://nmrspec.org/tutorial), and the [version history](https://nmrspec.org/changes).

## Group-resolved spin spectra (introduced in v1.1, retained in v1.2)

SpinSpec v1.1 introduces an optional gamma-weighted observation operator,
a decomposition into **up to ten user-defined spin groups** and a **signed
interference** component. The Hamiltonian and J-couplings remain unchanged.
For degenerate eigenspaces, the invariant output uses energy-subspace
projectors, including a mathematically defined DC component.

Use `SPINSPEC_OBSERVER_GROUPS=0,0,0,1` (one group ID per spin) for
the H3–P example. The optional web-interface group field accepts
`0 0 0 1`. Native **C/Gnuplot** generates
`group_spectrum_00000.jpg` and `group_curve_00000.dat` while the
full results, including DC, remain in `Sp_invariant_00000.csv`.

For the optional mode compile an additional executable:

```sh
gcc -std=c11 -O2 bin/group_graph.c -o bin/group_graph -lm
```

The filenames `Sp_*.dat`, `Res_*.dat` and `data_*.jpg` are preserved.
Starting with the corrected v1.1.0 engine, ordinary and grouped observations
use the same signed gamma-weighted operator. As a result, ordinary numeric
strengths change relative to older equal-weight results, even though frequencies
and input `.set` files remain unchanged. **Group curves are operator strengths, not
experimental FID amplitudes**: the initial density matrix, excitation,
pulse sequence, longitudinal detector model and receiver response remain
outside the scope of this version.

See [group-resolved user guide](docs/group-resolved-v1.1.md).

## Getting started (Linux command line)

**Online, without installation:** open [nmrspec.org](https://nmrspec.org), choose an example or enter spin parameters, and select **Calculate spectrum**. The browser interface runs calculations on the hosted server.

**Run the C simulator locally** on Ubuntu or Debian:

```bash
sudo apt-get update
sudo apt-get install -y git build-essential libgsl-dev gnuplot-nox
git clone https://github.com/scleroze/SpinSpec.git
cd SpinSpec/bin
gcc -O2 -fopenmp engine_matrix.c -o engine_matrix -lgsl -lgslcblas -lm
gcc -O2 matrix_graph.c GnuGraph.c -o matrix_graph -lm
cp ../examples/two_spins.set .
OMP_NUM_THREADS=1 ./engine_matrix two_spins.set
```

The final command runs the example directly from the terminal. Look in the `bin/` directory for `Sp_00000.dat` (transition list), `Res_*_00000.dat` (broadened spectra), and `data_00000.jpg` (plot). Both compiled executables, `engine_matrix` and `matrix_graph`, must remain in the same working directory. To reproduce the archived first release, check out tag `v1.0.0` before building.

**Python is not needed for this command-line workflow**; it is used for the separate FastAPI web server. Native Windows build instructions are not provided; on Windows, a Linux environment such as WSL is an option.

## Physical model

For `N` spin-1/2 nuclei, the complete product-basis Hamiltonian has dimension `2^N × 2^N` and includes Zeeman terms (with chemical shifts) and isotropic scalar `J` couplings. Its total longitudinal spin projection `M_z = sum_k I_kz` is conserved. The current C engine first constructs the full real symmetric Hamiltonian, groups basis states into `M_z` sectors, checks that couplings between sectors vanish within numerical tolerance, and diagonalizes each sector independently using GSL. Eigenvectors are then assembled in the original product basis. This is an exact block decomposition for the implemented Hamiltonian, **not** an approximation that discards inter-spin couplings.

The transition frequencies are the absolute differences of eigenvalues expressed in Hz:

```text
frequency(i,j) = abs(E[j] - E[i])
```

Transition strengths are calculated from matrix elements of a single-spin-flip operator, transformed into the Hamiltonian eigenbasis. **Both ordinary and group-resolved modes weight the transverse observation operator by the signed gyromagnetic ratios** (`O = sum_k (gamma_k/gamma_H) I_kx`). The grouped mode additionally reports the group-specific and coherent cross contributions. Older equal-weight strengths are not the default after the v1.1.0 correction. Initial-state populations, preparation, relaxation, receiver response and pulse-sequence dynamics are not modeled. Consequently, reported strengths are relative squared operator matrix elements, **not** calibrated experimental signal amplitudes. In particular, the observation model alone is not a complete experimental zero-field signal model for heteronuclear systems.

The engine sums Lorentzian profiles for visualization. Each chosen graph window uses a reference Larmor frequency for its selected spin; **these windows are not isotope-selective component spectra**.

## Requirements (Linux)

- A C compiler, such as GCC, with OpenMP support
- GNU Scientific Library (GSL) and its CBLAS support
- Gnuplot with JPEG support
- Standard math library (`libm`)

On Debian or Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential libgsl-dev gnuplot-nox
```

## Build

From the repository root:

```bash
cd bin
gcc -O2 -fopenmp engine_matrix.c -o engine_matrix -lgsl -lgslcblas -lm
gcc -O2 matrix_graph.c GnuGraph.c -o matrix_graph -lm
```

The C calculation invokes `./matrix_graph` for the ordinary result. Keep **both executables** in the working directory. For invariant group plotting and cached replotting, also build `group_graph` and `replot_spectrum` as described in the [release notes](docs/release-notes-v1.2.md). The original source also contains an SVG-generation declaration, but SVG output is not needed for the commands above.

## Quick start

With the executables built in `bin/`:

```bash
cp ../examples/two_spins.set .
./engine_matrix two_spins.set
```

For a four-spin ¹H/³¹P example, use `../examples/Trimethyl_phosphate (one brench).set`.

The engine writes files in the **current working directory**:

- `Sp_00000.dat`: discrete transition frequencies and relative strengths (plus state indices and an internal lookup field)
- `Res_<spin>_00000.dat`: Lorentz-broadened intensity samples for each requested display window
- `data_00000.jpg`: rendered multiplot image, if Gnuplot is available

For a field sweep, subsequent frames use `00001`, `00002`, etc. Parallel execution can affect the ordering of rows in a `Sp_*.dat` file; sort by a stable transition identifier when comparing independently generated datasets.

## Input file format (.set)

Example: `examples/two_spins.set`

```text
#two
42.578 17.24
0 0
0.05
0 1 11

Graph 0 1
1.2 1.2
0.1 16
```

The records are:

| Record | Meaning | Units |
| --- | --- | --- |
| `#...` | Optional title/header | — |
| `42.578 17.24` | `γ/(2π)` for each nucleus, in spin order | MHz/T |
| `0 0` | Chemical shifts, one per nucleus | ppm |
| `0.05` | Static magnetic field | mT |
| `0 1 11` | Isotropic coupling `spin_i spin_j J_ij`; **zero-based** spin indices | Hz |
| `Graph 0 1` | Zero-based indices selecting frequency display windows | — |
| `1.2 1.2` | Vertical amplitude limits for those windows | relative units |
| `0.1 16` | Legacy Lorentzian FWHM and frequency-window half-width; a zero half-width selects automatic scaling | Hz |

Additional couplings may be listed on separate lines before `Graph`. Use spin pairs in ascending index order (`i < j`), matching the current Hamiltonian implementation. The modern web form has **no separate Graph heights or spectrum half-width input**: one FWHM field and optional independent X/Y boundaries replace those controls. It writes backward-compatible legacy records (`0` for automatic panel heights, and `FWHM 0` for automatic half-width) plus optional `XRange left right` and `YRange lower upper` records; `auto` means an unspecified bound. In the browser, either decimal dots or decimal commas are accepted if list items are separated by spaces; saved `.set` files use decimal dots. Observer-group assignments are configured in the web interface and are **not saved** in `.set` files.

For a **linear magnetic-field sweep**, the field record may contain three numbers: `B_start B_stop B_step`, all in **mT**; the current GitHub version does not implement logarithmic sweeps.

The number of spins is inferred from the gyromagnetic-ratio line. Every nucleus represented in the calculation is spin-1/2. For practical calculations, keep the exponential scaling of the `2^N` Hamiltonian in mind.

## Web application

The `app/`, `static/` and `templates/` directories contain a FastAPI application used by the online interface. The current `app/app.py` specifies deployment-specific paths under `/opt/nmr-web` and expects compiled C binaries. Its server configuration and deployment requirements are separate from the self-contained C example above; cloning this repository alone does not configure a production web server.

## Tests and development status

The repository includes GitHub Actions checks for compilation, analytical and invariant-observer tests, numerical regression comparisons (including multispin models), unified plotting, cached replot parity and example spectra. The reference and invariance tests compare numerical quantities rather than merely checking that output files exist. See [release notes for v1.2.0](docs/release-notes-v1.2.md) for benchmark scope and validated precision. The opt-in group mode offers coherent group strengths and signed interference with gamma weighting; these are not calibrated experimental amplitudes.

## License and attribution

SpinSpec's original source code is distributed under the **GNU General Public License, version 3 or later (GPL-3.0-or-later)**. See [LICENSE](LICENSE). This is free software: GPL permits commercial as well as noncommercial use, provided its terms are followed. External dependencies and any separately licensed assets retain their own licenses.

The numerical C engine uses GNU Scientific Library (GSL), also under GPL. A standalone installation must provide the dependencies described above. The online service at [nmrspec.org](https://nmrspec.org) is a separate hosted deployment.

**Academic citation:** if SpinSpec contributes to published NMR calculations, please cite the software and its version. See [CITATION.cff](CITATION.cff) for author metadata and the suggested citation. For scientific work performed with version **v1.0.0**, cite: Kupriyanov, P. (2026). *SpinSpec: Quantum simulation of NMR spectra* (Version 1.0.0) [Computer software]. Zenodo. https://doi.org/10.5281/zenodo.23696822. Citation is requested as an academic practice; it is not an additional restriction under the GPL.

Copyright (c) 2026 Pavel Kupriyanov for the original SpinSpec contributions, subject to verification of any employer or contributor rights.
