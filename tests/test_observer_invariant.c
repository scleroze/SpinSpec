#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../bin/observer_invariant.h"
#define EPS 1e-11
static int close_enough(double a, double b) { return fabs(a-b)<EPS; }

/* One spin, exactly degenerate: O=I_x. Identity and Hadamard
   eigenvectors have different off-diagonal entries, but identical
   Tr(O^2)/2=0.25 zero-frequency spectral strength. */
static void test_exact_degeneracy_rotation(void) {
    const int flips[4]={0,1,1,0};
    const int group[1]={0};
    const double gamma[1]={SS_OBS_GAMMA_REF_HZ_PER_MT};
    const double energies[2]={0,0};
    const double identity[4]={1,0,0,1};
    const double u=0.7071067811865475244;
    const double rotated[4]={u,u,u,-u};
    SSObsPair pairs[1];
    const int count=ss_obs_collect_pairs(2,1,1,group,gamma,flips,pairs,1);
    assert(count==1);
    for (int mode=0;mode<2;++mode) {
        SSObsInvariantSpectrum result={0};
        assert(ss_obs_invariant_build(2,1,pairs,count,
            mode?rotated:identity,energies,&result)==0);
        assert(result.block_count==1 && result.line_count==1);
        assert(result.lines[0].dim_a==2 && result.lines[0].dim_b==2);
        assert(close_enough(result.lines[0].frequency_hz,0));
        assert(close_enough(result.lines[0].total_strength,0.25));
        assert(close_enough(result.lines[0].group_strength[0],0.25));
        assert(close_enough(result.lines[0].interference,0));
        ss_obs_invariant_free(&result);
    }
}
static void test_nonzero_field_and_roundoff(void) {
    const int flips[4]={0,1,1,0};
    const int group[1]={0};
    const double gamma[1]={SS_OBS_GAMMA_REF_HZ_PER_MT};
    const double identity[4]={1,0,0,1};
    SSObsPair pairs[1];
    const int count=ss_obs_collect_pairs(2,1,1,group,gamma,flips,pairs,1);
    const double split[2]={-2,2};
    const double numerical_degenerate[2]={100,100+1e-11};
    SSObsInvariantSpectrum r={0};
    assert(ss_obs_invariant_build(2,1,pairs,count,identity,split,&r)==0);
    assert(r.block_count==2 && r.line_count==3);
    assert(close_enough(r.lines[1].frequency_hz,4));
    assert(close_enough(r.lines[1].total_strength,0.25));
    assert(close_enough(r.lines[0].total_strength,0));
    assert(close_enough(r.lines[2].total_strength,0));
    ss_obs_invariant_free(&r);
    assert(ss_obs_invariant_build(2,1,pairs,count,identity,numerical_degenerate,&r)==0);
    assert(r.block_count==1);
    assert(close_enough(r.lines[0].total_strength,0.25));
    ss_obs_invariant_free(&r);
}
static void test_group_interference_and_dc(void) {
    /* Two independent spin flips with opposite signed gamma. H=0;
       independent basis rotations cannot change the subspace trace. */
    const int flips[16]={0,2,1,0, 2,0,0,1, 1,0,0,2, 0,1,2,0};
    const int groups[2]={0,1};
    const double gammas[2]={SS_OBS_GAMMA_REF_HZ_PER_MT,
                             -SS_OBS_GAMMA_REF_HZ_PER_MT};
    const double energies[4]={0,0,0,0};
    const double identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    SSObsPair pairs[4];
    SSObsInvariantSpectrum r={0};
    int n=ss_obs_collect_pairs(4,2,2,groups,gammas,flips,pairs,4);
    assert(n==4);
    assert(ss_obs_invariant_build(4,2,pairs,n,identity,energies,&r)==0);
    assert(r.block_count==1);
    assert(close_enough(r.lines[0].group_strength[0],0.5));
    assert(close_enough(r.lines[0].group_strength[1],0.5));
    assert(close_enough(r.lines[0].total_strength,1.0));
    assert(close_enough(r.lines[0].interference,0));
    ss_obs_invariant_free(&r);
}
int main(void) {
    test_exact_degeneracy_rotation();
    test_nonzero_field_and_roundoff();
    test_group_interference_and_dc();
    puts("observer_invariant: degenerate subspace, DC, rotation and sum-rule tests passed");
    return 0;
}
