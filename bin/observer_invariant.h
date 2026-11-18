/*
 * SpinSpec experimental basis-invariant one-sided spectral strengths.
 *
 * Given exact or numerically degenerate energy subspaces P_a, P_b,
 * the nonzero-frequency positive-side line has strength
 *   S_ab = ||P_a O P_b||_F^2, a < b.
 * At zero frequency the half-sided convention is
 *   S_aa = (1/2) ||P_a O P_a||_F^2.
 * The diagonal matrix elements MUST be included for S_aa.
 *
 * Sum over all lines equals 1/2 Tr(O^2); this remains invariant
 * under any orthogonal eigenvector rotation inside a degenerate block.
 *
 * Operates on the GSL-independent observer_groups.h data structures.
 */
#ifndef SPINSPEC_OBSERVER_INVARIANT_H
#define SPINSPEC_OBSERVER_INVARIANT_H

#include "observer_groups.h"
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double energy;
    int eigen_index;
} SSObsSortedState;

typedef struct {
    int block_a;
    int block_b;
    int dim_a;
    int dim_b;
    double energy_a_hz;
    double energy_b_hz;
    double frequency_hz;
    double total_strength;
    double interference;
    double group_strength[SS_OBS_MAX_GROUPS];
} SSObsInvariantLine;

typedef struct {
    int group_count;
    int block_count;
    int line_count;
    double energy_tolerance_hz;
    SSObsInvariantLine *lines;
} SSObsInvariantSpectrum;

static int ss_obs_compare_sorted(const void *first, const void *second)
{
    const SSObsSortedState *a = (const SSObsSortedState *)first;
    const SSObsSortedState *b = (const SSObsSortedState *)second;
    return (a->energy > b->energy) - (a->energy < b->energy);
}

static void ss_obs_invariant_free(SSObsInvariantSpectrum *spectrum)
{
    if (!spectrum) return;
    free(spectrum->lines);
    memset(spectrum, 0, sizeof(*spectrum));
}

/* Zero-based index in a compact upper triangular matrix, diagonal included. */
static int ss_obs_invariant_index(int a, int b, int block_count)
{
    if (b < a) {
        int tmp = a;
        a = b;
        b = tmp;
    }
    return a * block_count - a * (a - 1) / 2 + (b - a);
}

/*
 * The expectation value <i|O_g|i> can be nonzero after a rotation
 * within a degenerate subspace, even when it was zero before rotation.
 */
static int ss_obs_invariant_diagonal(int dim, int group_count,
                                     const SSObsPair *pairs, int pair_count,
                                     const double *eigenvectors, int eigen_i,
                                     SSObsResult *result)
{
    double combined = 0.0, separate = 0.0;
    if (!pairs || !eigenvectors || !result || dim < 2 ||
        group_count < 1 || group_count > SS_OBS_MAX_GROUPS ||
        pair_count < 1 || eigen_i < 0 || eigen_i >= dim)
        return -1;
    memset(result, 0, sizeof(*result));
    for (int p = 0; p < pair_count; ++p) {
        int a = pairs[p].basis_a, b = pairs[p].basis_b;
        int group = pairs[p].group;
        if (a < 0 || a >= dim || b < 0 || b >= dim ||
            group < 0 || group >= group_count)
            return -1;
        result->group_amplitude[group] += 2.0 * pairs[p].weight *
            eigenvectors[a * dim + eigen_i] *
            eigenvectors[b * dim + eigen_i];
    }
    for (int g = 0; g < group_count; ++g) {
        double amplitude = result->group_amplitude[g];
        result->group_strength[g] = amplitude * amplitude;
        combined += amplitude;
        separate += amplitude * amplitude;
    }
    result->total_strength = combined * combined;
    result->interference = result->total_strength - separate;
    return 0;
}

static void ss_obs_invariant_add(SSObsInvariantLine *line,
                                  const SSObsResult *result,
                                  int group_count, double multiplier)
{
    line->total_strength += multiplier * result->total_strength;
    for (int g = 0; g < group_count; ++g)
        line->group_strength[g] += multiplier * result->group_strength[g];
}

/*
 * Build complete subspace-to-subspace strengths.
 *
 * Energy tolerance (Hz): max(1e-10, 64 * DBL_EPSILON * max |E_i|).
 * Cluster membership is decided relative to the FIRST sorted level in a
 * block, preventing accidental merging by a chain of close neighbours.
 * A tolerance this small resolves physics except splittings comparable
 * with the intrinsic floating-point resolution of diagonalization.
 *
 * eigenvectors[basis * dim + eigen] is a real orthogonal matrix.
 * eigenvalues[eigen] are frequencies in Hz (H/h).
 * Returns 0 on success and leaves output initialized for free().
 */
static int ss_obs_invariant_build(int dim, int group_count,
                                  const SSObsPair *pairs, int pair_count,
                                  const double *eigenvectors,
                                  const double *eigenvalues,
                                  SSObsInvariantSpectrum *output)
{
    SSObsSortedState *sorted = NULL;
    int *block_for_state = NULL, *block_dimensions = NULL;
    double *block_energy = NULL;
    int block_count = 0;
    double max_abs_e = 1.0;
    double tolerance;
    SSObsInvariantSpectrum built = {0};

    if (!output || dim < 2 || group_count < 1 ||
        group_count > SS_OBS_MAX_GROUPS ||
        !pairs || pair_count < 1 || !eigenvectors || !eigenvalues)
        return -1;
    memset(output, 0, sizeof(*output));
    if (dim > 4096) return -1; /* conservative memory guard */

    sorted = malloc((size_t)dim * sizeof(*sorted));
    block_for_state = malloc((size_t)dim * sizeof(*block_for_state));
    block_dimensions = calloc((size_t)dim, sizeof(*block_dimensions));
    block_energy = calloc((size_t)dim, sizeof(*block_energy));
    if (!sorted || !block_for_state || !block_dimensions || !block_energy)
        goto failure;

    for (int i = 0; i < dim; ++i) {
        if (!isfinite(eigenvalues[i])) goto failure;
        sorted[i].energy = eigenvalues[i];
        sorted[i].eigen_index = i;
        if (fabs(eigenvalues[i]) > max_abs_e)
            max_abs_e = fabs(eigenvalues[i]);
    }
    tolerance = fmax(1e-10, 64.0 * DBL_EPSILON * max_abs_e);
    qsort(sorted, (size_t)dim, sizeof(*sorted), ss_obs_compare_sorted);

    int first_in_block = 0;
    for (int i = 0; i < dim; ++i) {
        if (i == 0 || sorted[i].energy - sorted[first_in_block].energy > tolerance) {
            first_in_block = i;
            ++block_count;
        }
        int block = block_count - 1;
        block_for_state[sorted[i].eigen_index] = block;
        block_dimensions[block]++;
        block_energy[block] += sorted[i].energy;
    }
    for (int block = 0; block < block_count; ++block)
        block_energy[block] /= block_dimensions[block];

    built.group_count = group_count;
    built.block_count = block_count;
    built.line_count = block_count * (block_count + 1) / 2;
    built.energy_tolerance_hz = tolerance;
    built.lines = calloc((size_t)built.line_count, sizeof(*built.lines));
    if (!built.lines) goto failure;

    int line_no = 0;
    for (int block_a = 0; block_a < block_count; ++block_a) {
        for (int block_b = block_a; block_b < block_count; ++block_b) {
            SSObsInvariantLine *line = &built.lines[line_no++];
            line->block_a = block_a;
            line->block_b = block_b;
            line->dim_a = block_dimensions[block_a];
            line->dim_b = block_dimensions[block_b];
            line->energy_a_hz = block_energy[block_a];
            line->energy_b_hz = block_energy[block_b];
            line->frequency_hz = block_a == block_b ? 0.0 :
                fmax(0.0, block_energy[block_b] - block_energy[block_a]);
        }
    }

    /*
     * Sum all i<j matrix elements. For i and j in the same energy
     * block their contribution belongs to its zero-frequency power.
     */
    for (int i = 0; i < dim; ++i) {
        int block_i = block_for_state[i];
        SSObsResult diagonal;
        int dc_index = ss_obs_invariant_index(block_i, block_i, block_count);
        if (ss_obs_invariant_diagonal(dim, group_count, pairs, pair_count,
                                      eigenvectors, i, &diagonal) != 0)
            goto failure;
        ss_obs_invariant_add(&built.lines[dc_index], &diagonal, group_count, 0.5);
        for (int j = i + 1; j < dim; ++j) {
            SSObsResult observation;
            int index = ss_obs_invariant_index(block_i, block_for_state[j],
                                                block_count);
            if (ss_obs_transition(dim, group_count, pairs, pair_count,
                                  eigenvectors, i, j, &observation) != 0)
                goto failure;
            ss_obs_invariant_add(&built.lines[index], &observation,
                                  group_count, 1.0);
        }
    }
    for (int k = 0; k < built.line_count; ++k) {
        double separate = 0.0;
        for (int g = 0; g < group_count; ++g)
            separate += built.lines[k].group_strength[g];
        built.lines[k].interference =
            built.lines[k].total_strength - separate;
    }

    free(sorted);
    free(block_for_state);
    free(block_dimensions);
    free(block_energy);
    *output = built;
    return 0;

failure:
    free(sorted);
    free(block_for_state);
    free(block_dimensions);
    free(block_energy);
    ss_obs_invariant_free(&built);
    return -1;
}

#endif /* SPINSPEC_OBSERVER_INVARIANT_H */
