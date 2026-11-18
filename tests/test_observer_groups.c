#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../bin/observer_groups.h"

#define TOL 1e-12
static int near(double a, double b) { return fabs(a-b) < TOL; }

static void test_parser(void) {
    int groups[4], count;
    assert(ss_obs_parse_groups("0,0,1,1",4,groups,&count)==0);
    assert(count==2 && groups[0]==0 && groups[3]==1);
    assert(ss_obs_parse_groups("0, 1",2,groups,&count)==0);
    assert(ss_obs_parse_groups("0,0",3,groups,&count)!=0);
    assert(ss_obs_parse_groups("-1,0",2,groups,&count)!=0);
    assert(ss_obs_parse_groups("0,10",2,groups,&count)!=0);
    assert(ss_obs_parse_groups("0,1,",2,groups,&count)!=0);
    assert(ss_obs_parse_groups("0,a",2,groups,&count)!=0);
}
static void test_one_spin(void) {
    const int lookup[4]={0,1,1,0};
    int groups[1]={0};
    double gammas[1]={SS_OBS_GAMMA_REF_HZ_PER_MT};
    const double V[4]={1,0,0,1};
    SSObsPair pairs[1];
    SSObsResult r;
    int n=ss_obs_collect_pairs(2,1,1,groups,gammas,lookup,pairs,1);
    assert(n==1);
    assert(ss_obs_transition(2,1,pairs,n,V,0,1,&r)==0);
    assert(near(r.total_strength,0.25));
    assert(near(r.group_strength[0],0.25));
    assert(near(r.interference,0));
    gammas[0]=SS_OBS_GAMMA_REF_HZ_PER_MT/2;
    n=ss_obs_collect_pairs(2,1,1,groups,gammas,lookup,pairs,1);
    assert(n==1);
    assert(ss_obs_transition(2,1,pairs,n,V,0,1,&r)==0);
    assert(near(r.total_strength,0.0625));
}
static void test_bell_coherence(void) {
    /* Basis 00,01,10,11. Eigenstates 00, (01+10)/sqrt2,
       (01-10)/sqrt2, 11. Coherent cross terms can cancel. */
    const int flip[16]={0,2,1,0, 2,0,0,1, 1,0,0,2, 0,1,2,0};
    const double u=0.7071067811865475244;
    const double V[16]={1,0,0,0, 0,u,u,0, 0,u,-u,0, 0,0,0,1};
    int groups[2]={0,1};
    double gammas[2]={SS_OBS_GAMMA_REF_HZ_PER_MT,SS_OBS_GAMMA_REF_HZ_PER_MT};
    SSObsPair pairs[4];
    SSObsResult r;
    int n=ss_obs_collect_pairs(4,2,2,groups,gammas,flip,pairs,4);
    assert(n==4);
    assert(ss_obs_transition(4,2,pairs,n,V,0,1,&r)==0);
    assert(near(r.total_strength,0.5));
    assert(near(r.group_strength[0],0.125));
    assert(near(r.group_strength[1],0.125));
    assert(near(r.interference,0.25));
    assert(ss_obs_transition(4,2,pairs,n,V,0,2,&r)==0);
    assert(near(r.total_strength,0));
    assert(near(r.group_strength[0],0.125));
    assert(near(r.group_strength[1],0.125));
    assert(near(r.interference,-0.25));
    gammas[1]=-SS_OBS_GAMMA_REF_HZ_PER_MT;
    n=ss_obs_collect_pairs(4,2,2,groups,gammas,flip,pairs,4);
    assert(n==4);
    assert(ss_obs_transition(4,2,pairs,n,V,0,1,&r)==0);
    assert(near(r.total_strength,0));
    assert(near(r.interference,-0.25));
}
int main(void) {
    test_parser();
    test_one_spin();
    test_bell_coherence();
    puts("observer_groups: all analytical tests passed");
    return 0;
}
