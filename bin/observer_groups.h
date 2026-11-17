/*
 * SpinSpec experimental group-resolved transverse observation model.
 *
 * Header-only and GSL-independent. The eigenvector matrix V is row-major:
 * V[basis_state * dimension + eigenstate].
 *
 * All group operators are gamma-weighted sums of I_x. Each spin-flip
 * matrix element of I_x is 1/2. Normalize gamma to the proton value:
 * gamma/(2*pi) = 42.57747892 MHz/T = 42577.47892 Hz/mT.
 *
 * Transition strengths only: no populations, excitation, relaxation,
 * detection transfer function, or phase-sensitive FID is modeled.
 */
#ifndef SPINSPEC_OBSERVER_GROUPS_H
#define SPINSPEC_OBSERVER_GROUPS_H

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define SS_OBS_MAX_GROUPS 10
#define SS_OBS_GAMMA_REF_HZ_PER_MT 42577.47892

typedef struct {
    int basis_a;
    int basis_b;
    int group;
    double weight;   /* gamma_k / (2 * gamma_proton) */
} SSObsPair;

typedef struct {
    double group_amplitude[SS_OBS_MAX_GROUPS];
    double group_strength[SS_OBS_MAX_GROUPS];
    double interference;   /* sum_{g != h} D_g D_h, potentially negative */
    double total_strength; /* (sum_g D_g)^2 */
} SSObsResult;

/*
 * Parse zero-based group IDs, e.g. 0,0,1,1,2.
 * Exactly nspins integers in [0,9] are required.
 * Sparse IDs are permitted; group_count is max(ID)+1.
 * Returns 0 on success.
 */
static int ss_obs_parse_groups(const char *spec, int nspins,
                               int *spin_groups, int *group_count)
{
    const char *p = spec;
    int max_group = -1;
    if (!spec || !spin_groups || !group_count || nspins <= 0)
        return -1;
    for (int k = 0; k < nspins; ++k) {
        char *end = NULL;
        long value;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p < '0' || *p > '9') return -1;
        value = strtol(p, &end, 10);
        if (end == p || value < 0 || value >= SS_OBS_MAX_GROUPS)
            return -1;
        spin_groups[k] = (int)value;
        if ((int)value > max_group) max_group = (int)value;
        p = end;
        while (*p == ' ' || *p == '\t') ++p;
        if (k < nspins - 1) {
            if (*p != ',') return -1;
            ++p;
        }
    }
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '\0') return -1;
    *group_count = max_group + 1;
    return 0;
}

/*
 * Convert legacy spin-flip lookup I (1-based spin index or zero)
 * to sparse operator elements. Caller allocates capacity >= nspins*dim/2.
 * Returns pair count, -1 on invalid lookup or insufficient capacity.
 */
static int ss_obs_collect_pairs(int dim, int nspins, int ngroups,
                               const int *spin_groups, const double *gammas_hz_mt,
                               const int *flip_index, SSObsPair *pairs,
                               size_t capacity)
{
    size_t found = 0;
    if (dim < 2 || nspins < 1 || ngroups < 1 ||
        ngroups > SS_OBS_MAX_GROUPS ||
        !spin_groups || !gammas_hz_mt || !flip_index || !pairs)
        return -1;
    for (int a = 0; a < dim - 1; ++a) {
        for (int b = a + 1; b < dim; ++b) {
            int spin = flip_index[a * dim + b] - 1;
            if (spin < 0) continue;
            if (spin >= nspins || spin_groups[spin] < 0 ||
                spin_groups[spin] >= ngroups ||
                !isfinite(gammas_hz_mt[spin]) || found >= capacity)
                return -1;
            pairs[found].basis_a = a;
            pairs[found].basis_b = b;
            pairs[found].group = spin_groups[spin];
            pairs[found].weight =
                gammas_hz_mt[spin] / (2.0 * SS_OBS_GAMMA_REF_HZ_PER_MT);
            ++found;
        }
    }
    if (found != (size_t)nspins * (size_t)dim / 2)
        return -1;
    return (int)found;
}

/*
 * D_g = <i| sum_{k in g} (gamma_k/gamma_H) I_kx |j>.
 * A_g = D_g^2, A_total = (sum_g D_g)^2 and
 * A_cross = A_total - sum_g A_g = 2 sum_{g<h} D_g D_h.
 */
static int ss_obs_transition(int dim, int ngroups,
                             const SSObsPair *pairs, int pair_count,
                             const double *eigenvectors, int eigen_i, int eigen_j,
                             SSObsResult *result)
{
    double sum_amp = 0, sum_self = 0;
    if (dim < 2 || ngroups < 1 || ngroups > SS_OBS_MAX_GROUPS ||
        pair_count < 1 || !pairs || !eigenvectors || !result ||
        eigen_i < 0 || eigen_i >= dim || eigen_j < 0 || eigen_j >= dim ||
        eigen_i == eigen_j) return -1;
    memset(result, 0, sizeof(*result));
    for (int p = 0; p < pair_count; ++p) {
        int a = pairs[p].basis_a;
        int b = pairs[p].basis_b;
        int g = pairs[p].group;
        if (a < 0 || b < 0 || a >= dim || b >= dim || g < 0 || g >= ngroups)
            return -1;
        result->group_amplitude[g] += pairs[p].weight *
            (eigenvectors[a * dim + eigen_i] * eigenvectors[b * dim + eigen_j] +
             eigenvectors[b * dim + eigen_i] * eigenvectors[a * dim + eigen_j]);
    }
    for (int g = 0; g < ngroups; ++g) {
        double d = result->group_amplitude[g];
        result->group_strength[g] = d * d;
        sum_amp += d;
        sum_self += d * d;
    }
    result->total_strength = sum_amp * sum_amp;
    result->interference = result->total_strength - sum_self;
    return 0;
}

#endif /* SPINSPEC_OBSERVER_GROUPS_H */
