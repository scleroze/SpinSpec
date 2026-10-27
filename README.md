# SpinSpec

SpinSpec is a spin-1/2 nuclear magnetic resonance (NMR) spectrum simulator. It constructs and diagonalizes a coupled-spin Hamiltonian and computes transition frequencies and **relative** spectral line strengths. A C numerical engine produces discrete transitions, Lorentz-broadened spectra and JPEG plots. This repository also contains a FastAPI-based web front end.

Online project: https://nmrspec.org

[![Zenodo DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.23696822.svg)](https://doi.org/10.5281/zenodo.23696822)

**Archived scientific release:** [SpinSpec v1.0.0](https://doi.org/10.5281/zenodo.23696822), DOI: [10.5281/zenodo.23696822](https://doi.org/10.5281/zenodo.23696822).

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

For `N` spin-1/2 nuclei, the complete product-basis Hamiltonian has dimension `2^N × 2^N` and includes Zeeman terms (with chemical shifts) and isotropic scalar `J` couplings. The engine diagonalizes the real symmetric matrix using GSL.

The transition frequencies are the absolute differences of eigenvalues expressed in Hz:

```text
frequency(i,j) = abs(E[j] - E[i])
```

Transition strengths are calculated from matrix elements of a single-spin-flip operator, transformed into the Hamiltonian eigenbasis. **In the current released engine all spins enter that operator with equal weights.** Nuclear gyromagnetic ratios affect the Hamiltonian but are **not yet used to weight the detection operator**; initial-state populations, receiver response and pulse-sequence dynamics are not modeled. Consequently, reported amplitudes should not be interpreted as calibrated experimental signal intensities. In particular, the current equal-weight detection model does not provide a general physically complete zero-field J-spectrum for heteronuclear systems.

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

The C calculation invokes `./matrix_graph` for each field/frame, so **both executables must be available in the working directory**. The original source also contains an SVG-generation declaration, but SVG output is not needed for the commands above.

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
| `0.1 16` | Lorentzian FWHM and frequency-window half-width | Hz |

Additional couplings may be listed on separate lines before `Graph`. Use spin pairs in ascending index order (`i < j`), matching the current Hamiltonian implementation.

For a **linear magnetic-field sweep**, the field record may contain three numbers: `B_start B_stop B_step`, all in **mT**; the current GitHub version does not implement logarithmic sweeps.

The number of spins is inferred from the gyromagnetic-ratio line. Every nucleus represented in the calculation is spin-1/2. For practical calculations, keep the exponential scaling of the `2^N` Hamiltonian in mind.

## Web application

The `app/`, `static/` and `templates/` directories contain a FastAPI application used by the online interface. The current `app/app.py` specifies deployment-specific paths under `/opt/nmr-web` and expects compiled C binaries. Its server configuration and deployment requirements are separate from the self-contained C example above; cloning this repository alone does not configure a production web server.

## Tests and development status

The repository includes a GitHub Actions smoke-test workflow that builds the C executables and runs two-spin and four-spin example calculations on pushes and pull requests. The original numerical regression verification was performed before the first public snapshot; the current public workflow checks that example outputs are generated, not that every numerical value matches a historical baseline.

Planned work includes optional decomposition of transition contributions by spin group and a gyromagnetic-ratio-weighted magnetic-moment observation operator. **Neither capability should be assumed present in the current GitHub `main` numerical engine.**

## License and attribution

SpinSpec's original source code is distributed under the **GNU General Public License, version 3 or later (GPL-3.0-or-later)**. See [LICENSE](LICENSE). This is free software: GPL permits commercial as well as noncommercial use, provided its terms are followed. External dependencies and any separately licensed assets retain their own licenses.

The numerical C engine uses GNU Scientific Library (GSL), also under GPL. A standalone installation must provide the dependencies described above. The online service at [nmrspec.org](https://nmrspec.org) is a separate hosted deployment.

**Academic citation:** if SpinSpec contributes to published NMR calculations, please cite the software and its version. See [CITATION.cff](CITATION.cff) for author metadata and the suggested citation. For scientific work performed with version **v1.0.0**, cite: Kupriyanov, P. (2026). *SpinSpec: Quantum simulation of NMR spectra* (Version 1.0.0) [Computer software]. Zenodo. https://doi.org/10.5281/zenodo.23696822. Citation is requested as an academic practice; it is not an additional restriction under the GPL.

Copyright (c) 2026 Pavel Kupriyanov for the original SpinSpec contributions, subject to verification of any employer or contributor rights.
