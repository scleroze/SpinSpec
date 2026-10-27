# SpinSpec v1.0.0 — initial public source release

This document describes the first public source-code release of SpinSpec, published as [v1.0.0](https://github.com/scleroze/SpinSpec/releases/tag/v1.0.0) and archived at [Zenodo](https://doi.org/10.5281/zenodo.23696822).

## Included functionality

- Numerical simulation of coupled nuclear spin-1/2 systems using the full product-basis Hamiltonian with Zeeman terms, chemical shifts and isotropic scalar J couplings.
- Symmetric eigensystem calculation with GNU Scientific Library (GSL).
- Computation of transition frequencies from eigenvalue differences and **relative** line strengths from transformed spin-flip matrix elements.
- Lorentzian spectral broadening; plots generated using Gnuplot.
- Two-spin and larger example systems, including the trimethyl phosphate 1H/31P model.
- Linear magnetic-field sweeps, command-line C calculation, and a separate FastAPI-based web interface.
- Scientific overview, input-file reference, build instructions and citation metadata, including author ORCID.

## Scientific limitations

- Only spin-1/2 nuclei are modeled.
- The observation operator currently assigns equal weights to all spins; gyromagnetic-ratio-weighted detection is **not implemented**. The reported spectral strengths are relative model intensities, not calibrated experimental signal amplitudes.
- The program does not model pulse sequences, initial-state populations or receiver response.
- At zero or very low field, the present observation operator is not a generally complete physical model of heteronuclear J-spectrum detection.
- Magnetic-field sweeps in this release are linear, not logarithmic.
- The computational dimension grows as 2^N; practical system size is limited by memory and CPU time.

## Validation

The C core was regression-tested on two-spin and four-spin examples. The C documentation changes were checked against a baseline implementation using byte-for-byte comparisons of computed numerical output files; see the GitHub Actions C-core regression workflow. These tests do not constitute exhaustive scientific validation of every spin system.

## Licensing and citation

- SpinSpec original source: GNU GPL version 3 or later (GPL-3.0-or-later); see [LICENSE](LICENSE).
- Third-party components have their own licenses; see [THIRD_PARTY.md](THIRD_PARTY.md).
- Please cite SpinSpec when it contributes to scientific calculations. Author information is recorded in [CITATION.cff](CITATION.cff).
- The version-specific Zenodo DOI is [10.5281/zenodo.23696822](https://doi.org/10.5281/zenodo.23696822).

Project website: https://nmrspec.org
