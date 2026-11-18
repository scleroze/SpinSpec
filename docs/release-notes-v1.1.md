# SpinSpec v1.1.0 — Group-resolved quantum NMR spectra

This release introduces **optional group-resolved observation strengths**
for strongly coupled spin-1/2 systems without modifying the full interacting
Hamiltonian.

## New

- Signed gyromagnetic weighting of the transverse observation operator
  `O = Σ(γ_k/γ_H) I_kx`.
- Explicit, user-defined spin groups (up to 10), which need not coincide with
  isotope groups.
- Group strengths, total strengths and **signed coherent interference**,
  with algebraic closure checked numerically.
- A basis-invariant treatment of exact and numerically degenerate
  energy levels using subspace projectors, including a separate DC
  component and a one-sided spectral-strength sum rule.
- A native **C/Gnuplot** group-spectrum renderer producing
  `group_spectrum_*.jpg` and `group_curve_*.dat`. The display omits the
  0-Hz DC component; the complete numerical data remain available in
  `Sp_invariant_*.csv`.
- Optional group-ID entry in the FastAPI web form, with separate image and
  CSV download links.
- Analytical and independent NumPy validation for several spin
  systems up to eight spins, with targeted zero-field H3–31P lines.

## Compatibility

The old equal-weight observation mode is still the default.
The legacy files `Sp_*.dat`, `Res_*.dat` and `data_*.jpg`
are preserved. Existing `.set` files need no modification. The new
`group_graph` C executable must be compiled alongside the numerical
engine to use optional group plots. The old Gnuplot renderer was
adjusted for zero field and for current Gnuplot multiplot syntax.

## Scientific scope

The new graphs represent **relative operator strengths**, not
calibrated experimental NMR amplitudes. In particular, SpinSpec v1.1
does **not** yet model polarization-transfer/shuttling sequences,
preparation-dependent density matrices, longitudinal detection
`M_z`, receiver response, relaxation or phase-sensitive FID.

Trimethyl-phosphate example files include an H3–31P four-spin
**branch model** rather than the full H9–31P molecule.

See [the group-resolved guide](docs/group-resolved-v1.1.md) for the
mathematical definition, input details, output semantics, limitations
and a worked Linux command-line example.

### Citation

Please cite the GitHub version or the relevant Zenodo record once the
new version is archived. The existing `10.5281/zenodo.23696822`
is the v1.0.0 snapshot and should not be described as an archive of
v1.1.0 unless Zenodo associates an additional record with this release.
