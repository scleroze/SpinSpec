#!/usr/bin/env python3
"""Compare the invariant C spectrum with independent NumPy projectors.

No zero-frequency lines are skipped. For each Hamiltonian energy
subspace, positive-frequency block strength uses squared Frobenius
norms, and DC block strength uses half the squared Frobenius norm.
"""
from __future__ import annotations
import csv
import math
import os
from pathlib import Path
import subprocess
import numpy as np
from test_multispin_reference import EXAMPLES, GAMMA_REF_HZ_PER_MT, read_model

I2=np.eye(2,dtype=float)
X=np.array([[0,.5],[.5,0]],dtype=float)
Z=np.diag([.5,-.5])

def kron_spin(op,spin,n):
    result=np.array([[1.]],dtype=float)
    for k in range(n):
        result=np.kron(result,op if k==spin else I2)
    return result

def reference(gammas,shifts,couplings,field,groups):
    n=len(gammas)
    dim=1<<n
    ix=[kron_spin(X,k,n) for k in range(n)]
    iz=[kron_spin(Z,k,n) for k in range(n)]
    h=np.zeros((dim,dim))
    for k in range(n):
        h+=field*gammas[k]*(1-shifts[k]*1e-6)*iz[k]
    for k,l,j in couplings:
        # I_k.I_l = I_z I_z + (I_+ I_- + I_- I_+)/2.
        xp=np.array([[0,1],[0,0]],dtype=float)
        xm=xp.T
        # Spin raising/lowering on distinct sites commute.
        upk=kron_spin(xp,k,n)
        dnk=kron_spin(xm,k,n)
        upl=kron_spin(xp,l,n)
        dnl=kron_spin(xm,l,n)
        h+=j*(iz[k]@iz[l]+(upk@dnl+dnk@upl)/2)
    ev,v=np.linalg.eigh(h)
    tol=max(1e-10,64*np.finfo(float).eps*max(1,max(abs(ev))))
    starts=[0]
    for i in range(1,dim):
        if ev[i]-ev[starts[-1]]>tol:
            starts.append(i)
    starts.append(dim)
    operators=[sum((gammas[k]/GAMMA_REF_HZ_PER_MT)*ix[k]
                    for k in range(n) if groups[k]==g)
               for g in range(max(groups)+1)]
    # Special case empty group for sparse IDs
    operators=[o if isinstance(o,np.ndarray) else np.zeros((dim,dim)) for o in operators]
    lines=[]
    for a in range(len(starts)-1):
        for b in range(a,len(starts)-1):
            sa,sb=slice(starts[a],starts[a+1]),slice(starts[b],starts[b+1])
            q=[v[:,sa].T@op@v[:,sb] for op in operators]
            multiplier=.5 if a==b else 1.
            per=np.array([multiplier*np.sum(matrix**2) for matrix in q])
            full=multiplier*np.sum(sum(q)**2)
            lines.append(dict(
                block_a=a,block_b=b,
                dim_a=starts[a+1]-starts[a],dim_b=starts[b+1]-starts[b],
                frequency_hz=0. if a==b else float(np.mean(ev[sb])-np.mean(ev[sa])),
                total_strength=full,interference=full-float(per.sum()),
                group_strength=per
            ))
    return lines,operators

def run_one(filename,field,groups):
    source_lines,gammas,shifts,couplings=read_model(filename)
    source_lines[3]=str(field)
    Path("invariant_model.set").write_text("\n".join(source_lines)+"\n",encoding="utf-8")
    path=Path("Sp_invariant_00000.csv")
    path.unlink(missing_ok=True)
    env=dict(os.environ,SPINSPEC_OBSERVER_GROUPS=",".join(map(str,groups)),OMP_NUM_THREADS="2")
    run=subprocess.run(["./engine_matrix","invariant_model.set"],env=env,
                       capture_output=True,text=True,timeout=180)
    if run.returncode: raise AssertionError(run.stdout[-1000:]+run.stderr[-1000:])
    with path.open(newline="") as stream: actual=list(csv.DictReader(stream))
    expected,operators=reference(gammas,shifts,couplings,field,groups)
    assert len(actual)==len(expected),(filename,field,len(actual),len(expected))
    max_error=0.
    for i,(row,target) in enumerate(zip(actual,expected)):
        for field_name in ("block_a","block_b","dim_a","dim_b"):
            assert int(row[field_name])==target[field_name],(filename,field,i,field_name,row[field_name],target[field_name])
        f1=float(row["frequency_hz"])
        f2=target["frequency_hz"]
        assert abs(f1-f2)<max(2e-5,3e-10*max(abs(f1),abs(f2))), (filename,field,i,f1,f2)
        for key in ("total_strength","interference"):
            x,y=float(row[key]),target[key]
            max_error=max(max_error,abs(x-y))
            assert abs(x-y)<1e-7*max(1,abs(x),abs(y)),(filename,field,i,key,x,y)
        for g,value in enumerate(target["group_strength"]):
            x=float(row[f"strength_g{g}"])
            max_error=max(max_error,abs(x-value))
            assert abs(x-value)<1e-7*max(1,abs(x),abs(value)),(filename,field,i,g,x,value)
    # Exact one-sided Frobenius-norm sum rule; includes DC!
    for g,op in enumerate(operators):
        observed=sum(float(r[f"strength_g{g}"]) for r in actual)
        predicted=np.sum(op**2)/2
        assert abs(observed-predicted)<1e-7*max(1,abs(predicted)),(filename,field,g,observed,predicted)
    combined=sum(operators)
    total=sum(float(r["total_strength"]) for r in actual)
    expected_total=np.sum(combined**2)/2
    assert abs(total-expected_total)<1e-7*max(1,abs(expected_total))
    print(f"PASS invariant: {filename}, B={field:g} mT, "
          f"blocks={len({r['block_a'] for r in actual})}, "
          f"rows={len(actual)}, max dA={max_error:.3g}, "
          f"total={total:.8g}",flush=True)

def main():
    scenarios=[
        ("two_spins.set",0.,[0,1]),
        ("two_spins.set",0.05,[0,1]),
        ("three_spins.set",0.,[0,0,1]),
        ("Trimethyl_phosphate (one brench).set",0.,[0,0,0,1]),
        ("Trimethyl_phosphate (one brench).set",.05,[0,0,0,1]),
        ("Triethyle_phosphate (one brench).set",0.,[0,0,0,0,0,1]),
        ("2_2_2_TriFluoro_Ethanol.set",0.,[0,0,1,1,1,0]),
        ("2_2_3_3_TetraFluoro_1-Propanol.set",.05,[0,0,0,1,1,2,1,0]),
    ]
    for name,b,g in scenarios:run_one(name,b,g)
    print("PASS: invariant DC and non-DC spectra agree with independent NumPy projectors",flush=True)
if __name__=="__main__":main()
