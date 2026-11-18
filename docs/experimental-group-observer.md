# Experimental gamma-weighted, group-resolved observation

**Branch:** `experimental/group-resolved-observer`. This is not part of the
archived SpinSpec v1.0.0 release and is not deployed at nmrspec.org.

## Motivation

Current `Sp_*.dat` transition strengths come from a legacy equal-weight
flip operator. In particular, `Res_<spin>_*.dat` windows are *not*
nucleus-selective spectra; each window uses the same transition list.

The experimental code adds a separate operator model with user-defined
spin groups. It does not alter the Zeeman + isotropic-J Hamiltonian,
eigensolver, or legacy output.

## Build and usage

From `bin/`:

```sh
gcc -O2 -fopenmp engine_matrix.c -o engine_matrix -lgsl -lgslcblas -lm
gcc -O2 matrix_graph.c GnuGraph.c -o matrix_graph -lm
cp ../examples/two_spins.set .
OMP_NUM_THREADS=1 SPINSPEC_OBSERVER_GROUPS=0,1 ./engine_matrix two_spins.set
```

One group ID is required per spin, in the input-file order. Valid group
IDs range from 0 to 9. Up to ten groups are supported.

For the four-spin `Trimethyl_phosphate (one brench).set` model, a possible
choice is `SPINSPEC_OBSERVER_GROUPS=0,0,0,1`, with three protons in group
0 and phosphorus in group 1.

The new output `Sp_groups_00000.csv` contains
`state_i,state_j,frequency_hz,total_strength,interference` followed
by `amplitude_gN,strength_gN` for each group. It is separate from
legacy `Sp_00000.dat` and `Res_*.dat`.

## Basis-invariant spectrum including zero frequency

The opt-in experimental engine also exports `Sp_invariant_00000.csv`.
Unlike the transition-by-transition `Sp_groups_00000.csv`, its rows
refer to **pairs of energy eigenspaces**. The CSV has columns

```text
block_a,block_b,dim_a,dim_b,energy_a_hz,energy_b_hz,frequency_hz,
total_strength,interference,strength_g0,strength_g1,...
```

The block dimensions allow the reader to distinguish exact or numerical
degeneracies. No amplitude is given for a degenerate block pair because
individual amplitudes depend on the basis; only basis-invariant strengths
are meaningful.

Define the orthogonal projectors `P_a` onto the energy blocks. For a real
symmetric observation operator `O = sum_g O_g`:

```text
frequency(a,b) = abs(E_b - E_a), a < b
strength(a,b)  = Tr(P_a O P_b O),  a < b

frequency(a,a) = 0
strength(a,a)  = (1/2) Tr(P_a O P_a O)
```

Group strengths follow the same rules with `O_g` in place of `O`.
The interference column is the total strength minus the sum of group
strengths. **Within the zero-frequency block, diagonal matrix elements
are included**. This avoids the arbitrary rotation dependence of an
off-diagonal-only transition list.

This is a *one-sided positive-frequency power convention*, with a half
weight at zero frequency. It obeys the exact sum rule

```text
sum_(0 <= frequency) strength = (1/2) Tr(O^2).
```

The zero-frequency row is an algebraic component of the chosen operator
model, not a prediction of a detectable DC signal: experimental FID
processing, field switching, initial populations and receiver coupling
are outside the current model.

The energy-block numerical tolerance is
`max(1e-10 Hz, 64 * DBL_EPSILON * max(1 Hz, max(abs(E_i))))`.
Levels that differ below this tolerance are treated as numerically
degenerate. The reference test rotates eigenvectors inside degenerate
spaces to verify invariance, and a separate independent NumPy test
checks the full including-zero-frequency matrix-element sum rule.

## Mathematical definition

For each group `g`, define the dimensionless transverse operator

```text
O_g = sum_(k in g) [gamma_k/gamma_H] I_kx

D_g(m,n) = <m|O_g|n>
A_g(m,n) = D_g(m,n)^2

A_total = (sum_g D_g)^2
A_interference = A_total - sum_g A_g
               = 2 sum_(g<h) D_g D_h
```

The numerator and denominator gammas are expressed in identical units.
The reference gamma_H/(2*pi) is 42.57747892 MHz/T. Signed negative
gammas are retained. The `I_kx` spin-1/2 flip matrix element is 1/2.
Hamiltonian eigenstates in this implementation are real, so the
amplitudes are real. For a complex Hamiltonian replace the square
with the absolute square and cross terms with real parts of products.

**Important:** Group *strengths* generally do not add to the total
strength; interference may be negative. Summing their *amplitudes* first
and then squaring produces the full result.

## Limitations and next scientific checks

- These are relative transition strengths, not calibrated detected
  intensities. Preparation, populations, receiver bandwidth and noise
  are not modeled. Both the polarization mechanism and the detection
  observable may change the physical spectrum.
- This transverse operator is a choice of measurement axis, not a
  universal zero-field NMR experiment model. A realistic zero-field
  experiment requires the appropriate initial density matrix,
  field-switching sequence, vector magnetometry orientation and
  dynamics.
- Individual eigenvectors are not uniquely defined inside degenerate
  eigenspaces: `Sp_groups_*.csv` still contains arbitrary eigenvector-
  dependent transition-by-transition amplitudes. Use the new
  `Sp_invariant_*.csv` for basis-invariant energy-block intensities,
  especially at B=0.
- Experimental CSV files include every pair of eigenstates, including
  zero-strength entries. Signed amplitudes and cross terms are for
  diagnostics.
- No independently selectable display traces for groups yet; the
  published online interface is unchanged.
- A test from the actual full C executable is configured in
  `.github/workflows/experimental-observer.yml`; analytical
  `tests/test_observer_groups.c` checks one-spin weighting, coherent
  Bell-state interference and cancellation.
