# SpinSpec: numerical spectrum CSV formats

This page describes the two numerical exports in SpinSpec v1.1.0 after the unified-observer correction. **The files contain discrete transition strengths before Lorentzian line broadening**, not samples from the plotted curves.

Magnetic fields are entered in mT; all CSV transition frequencies are in **Hz**; transition strengths have **relative model units**. Neither mode models thermal populations, sample preparation, pulse sequences, relaxation, receiver response or calibrated experimental amplitudes.

## Ordinary mode — `Sp_transitions_00000.csv`

This CSV is converted from the main engine output `Sp_00000.dat`. The ordinary observer is now gamma-weighted, matching the total group observer. Each row is a pair of eigenstates:

| Column | Meaning |
| --- | --- |
| `state_i`, `state_j` | Indices of two eigenstates, **1-based**, in the diagonalizer's order (not necessarily ordered by energy). |
| `frequency_hz` | Transition frequency `abs(E_j - E_i)` when `E` is given in Hz, i.e. `|E_j-E_i|/h` for energies in joules. |
| `relative_strength` | Squared eigenbasis matrix element of the **gamma-weighted transverse observation operator** `O = sum_k (gamma_k/gamma_H) I_kx`. |

Illustrative (not generated or experimental) values:

```csv
state_i,state_j,frequency_hz,relative_strength
1,2,2128.400000,0.250000
1,3,2131.200000,0.500000
```

The C engine now writes transition frequency and relative strength with 17 significant decimal digits. The CSV conversion retains those digits. Row order may vary under parallel computation.

**Important:** `Sp_00000.dat` has a sixth internal lookup column `I` that is indexed by **product-basis** states, not Hamiltonian eigenstates. It is **not a valid physical assignment of an eigenstate transition to a spin**, and is intentionally omitted from the user-facing CSV.

## Group mode — `Sp_invariant_00000.csv`

This output is created directly by the C engine. The group IDs are assigned per spin (example: `0 0 0 1`), but the Hamiltonian remains unchanged. Each row describes a pair of **energy blocks** (numerically degenerate eigenspaces).

| Column | Meaning |
| --- | --- |
| `block_a`, `block_b` | Energy-block IDs, starting from **0**, in ascending energy order. |
| `dim_a`, `dim_b` | Number of eigenstates in each block. |
| `energy_a_hz`, `energy_b_hz` | Mean block energies in frequency units `E/h` (Hz). |
| `frequency_hz` | Nonnegative block-to-block transition frequency (Hz). |
| `total_strength` | Squared contribution of the combined, gamma-weighted observation operator, with degenerate blocks treated invariantly. |
| `interference` | Signed coherent cross contribution; can be **negative**. |
| `strength_g0`, `strength_g1`, ... | Individual group strengths by group ID. |

For every row:

```text
total_strength = strength_g0 + strength_g1 + ... + interference
```

Illustrative consistent single-transition example (not a particular molecule):

```csv
block_a,block_b,dim_a,dim_b,energy_a_hz,energy_b_hz,frequency_hz,total_strength,interference,strength_g0,strength_g1
0,1,1,1,-5,6,11,0.25,-1,1,0.25
```

It can arise from group transition amplitudes `D_0=1` and `D_1=-0.5`. Then `|D_0+D_1|²=0.25`, while the separate squared strengths total `1.25` and the interference is `-1`.

The observation operator is

```text
O = sum_g O_g, where O_g = sum_{k in g} (gamma_k/gamma_H) I_{kx}.
```

For distinct energy blocks `a < b`, the invariant strength is `||P_a O P_b||_F²`. For `a=b`, the half-sided zero-frequency convention is `(1/2)||P_a O P_a||_F²`. Therefore block energies and dimensions provide important context for degeneracies.

**Zero-frequency rows remain in the CSV**, although the plotted dynamic group spectrum omits them. Negative interference is not a negative probability or a stand-alone signal.

## Comparing ordinary and group modes

Both modes now use the **same** `O = sum_k (gamma_k/gamma_H) I_kx` operator and normalization. Ordinary CSV rows refer to individual eigenstate pairs; group CSV rows refer to invariant energy-subspace pairs. Sum ordinary strengths across all pairs of states belonging to the corresponding distinct energy blocks to compare with the invariant group `total_strength` at nonzero frequency. At zero frequency, `Sp_invariant_*.csv` additionally includes diagonal eigenbasis matrix elements and the one-sided factor 1/2; the ordinary per-eigenstate-pair CSV does **not** and must not be directly compared for DC. For all full-spectrum invariant sums, `sum I = Tr(O^2)/2`.

## Frequencies versus plot offsets

The CSV `frequency_hz` is an **absolute transition frequency** in Hz. The local x-coordinate of a plotted window is

```text
frequency_offset_hz = frequency_hz - reference_frequency_hz.
```

The `Graph` list in the input selects frequency **windows**, not which spins are included in the Hamiltonian or CSV. Linewidth and window half-width affect broadened plots but not the original transition CSV values.

Output filenames remain unchanged: `Sp_00000.dat`, `Res_<spin>_00000.dat`, `data_00000.jpg`, `group_curve_00000.dat` and `group_spectrum_00000.jpg`. The old equal-spin-weight strengths are **not** preserved as the default mode. Both plotted dynamic spectra omit the zero-frequency component; raw CSV data retain zero-frequency rows, with the complete invariant DC weight in the grouped CSV. Subsequent frames use `00001`, etc.

## Web UI

After **Calculate spectrum**, expand **Export numerical transition data (CSV)** below the spectrum image. The offered file follows the checkbox **Calculate contributions of spin groups**. The [web tutorial](https://nmrspec.org/tutorial#data-export) has a parallel column reference.

*Deployment status, 19 November 2026: the new ordinary CSV download and combined export panel are being tested on the separate test server. Do not assume this UI has already been deployed to the public site.*
