# SpinSpec v1.2.0 — Faster exact NMR simulation and automatic spectrum replot

**Release date:** 25 November 2026

**Online calculator:** https://nmrspec.org  
**Source code:** https://github.com/scleroze/SpinSpec  
**Versioned source:** https://github.com/scleroze/SpinSpec/releases/tag/v1.2.0

## What changed

### Exact Hamiltonian optimization

- The real symmetric Hamiltonian is diagonalized using sectors of conserved total longitudinal spin projection `M_z = sum_k I_kz`.
- The current C implementation constructs the full `2^N × 2^N` product-basis Hamiltonian, checks that inter-sector elements vanish within tolerance, diagonalizes each sector with GSL and reassembles eigenvectors in the original basis. No spin-spin couplings are discarded.
- Gamma-weighted observer transformations and transition-strength calculations are faster.
- On the validated **nine-spin computational benchmarks**, execution was approximately **22× faster at 3 T** and **49× faster at zero field** than the reference engine. These measurements are case- and hardware-dependent, **not** universal guarantees.
- Reference comparisons yielded maximum reported line-frequency deviations of approximately `1.66e-5 Hz` at 3 T and `1.68e-12 Hz` at zero field; invariant DC strength also agreed with the reference.

**Public online limit:** Eight spin-1/2 nuclei. Nine-spin benchmarks tested the computational engine separately.

### Web calculation and visualization

- One **Calculate spectrum** control automatically reuses cached quantum transitions when only FWHM, display windows or X/Y limits change; compatible redraws do **not** repeat the Hamiltonian diagonalization.
- The interface now has a single positive **Lorentzian FWHM** input and independent optional left/right frequency-offset limits and lower/upper relative-intensity limits. Blank limits request automatic scaling; negative Y values are allowed for signed interference.
- The ordinary and group-resolved views use a unified native C/Gnuplot renderer.
- Decimal point and decimal comma are accepted in browser numeric inputs; multiple numeric values must be separated by spaces. Saved `.set` files retain decimal points.
- Calculation errors and HTTP 429 rate-limit notices are displayed over the preview.
- The web interface imports older `.set` graph width and height records, with automatic scaling and optional `XRange`/`YRange` metadata.
- All 17 model examples are available in the categorized example catalog. Some are explicitly illustrative or reduced: **do not treat their coupling constants as experimental reference data without checking primary literature**.

### Physical meaning and numerical exports

Both modes use the **same signed gamma-weighted transverse observation operator**

```text
O = sum_k (gamma_k / gamma_H) I_kx.
```

The group-resolved mode divides this operator into observer groups, calculates coherent contributions and **signed interference**, and exports basis-invariant energy-block strengths. The ordinary export is organized by eigenstate pairs. Both CSVs contain relative squared matrix-element strengths **before broadening**, not calibrated experimental FID amplitudes.

**Invariant zero-frequency (DC) strength:** the group CSV retains the DC records. The group image includes a schematic absolute-frequency overview that can display 0 Hz; local Lorentz-broadened windows can also include its contribution if they cover 0 Hz. The ordinary renderer uses its separately reconstructed invariant DC strength. A DC contribution is not an independently detected AC resonance. The current model omits populations, state preparation, pulse sequences, relaxation and receiver response.

## Verification

- Analytical two-spin and heteronuclear models, invariant observation-operator tests, numerical reference comparisons, group-sum identity and Gnuplot regression tests.
- Parity of ordinary and grouped plots with cached replotting, at both finite and zero magnetic fields.
- Public-server smoke tests on **25 November 2026**: ordinary calculation, group-resolved calculation, CSV downloads and replot without Hamiltonian diagonalization passed.
- GitHub Actions checks are required to succeed before the release is published.

## Reproducibility and caveats

For scientific work record the precise source version, **full input `.set` file**, observer group assignments, magnetic field, FWHM, plot limits and CSV exports. Observer groups are set in the browser and **are not saved in `.set` files**. The web deployment retained pre-existing local copies of model examples during upgrade; **their exact contents have not yet been proven identical to every file under `examples/` in this repository**. Saving the calculation input is essential.

**Citation:** cite this specific GitHub release using the repository `CITATION.cff`. The Zenodo DOI [10.5281/zenodo.23696822](https://doi.org/10.5281/zenodo.23696822) archives **SpinSpec v1.0.0**, not this v1.2.0 release.

**License:** GPL-3.0-or-later. Older versions remain available under their existing tags and Releases.
