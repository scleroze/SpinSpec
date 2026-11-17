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
  eigenspaces, so **individual** transitions at exactly degenerate
  frequencies may depend on the arbitrary eigenbasis. Observables
  should be aggregated over degenerate subspaces before physical
  interpretation, with a stated numerical tolerance.
- Experimental CSV files include every pair of eigenstates, including
  zero-strength entries. Signed amplitudes and cross terms are for
  diagnostics.
- No independently selectable display traces for groups yet; the
  published online interface is unchanged.
- A test from the actual full C executable is configured in
  `.github/workflows/experimental-observer.yml`; analytical
  `tests/test_observer_groups.c` checks one-spin weighting, coherent
  Bell-state interference and cancellation.
