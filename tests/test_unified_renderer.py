#!/usr/bin/env python3
"""Check the unified drawing executable and independent signed Y bounds."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
ENGINE=ROOT/"bin/engine_matrix"
RENDER=ROOT/"bin/group_graph"


def run(model, field, grouped):
    with tempfile.TemporaryDirectory(prefix="spinspec-unified-") as name:
        job=Path(name)
        contents=(ROOT/"examples"/model).read_text().splitlines()
        contents[3]=str(field)
        graph_idx=next(i for i,v in enumerate(contents) if v.startswith("Graph "))
        selected=[int(x) for x in contents[graph_idx].split()[1:]]
        contents[graph_idx+1]=" ".join("0" for _ in selected)
        contents[graph_idx+2]="0.5 0"
        contents += ["YRange -0.25 2.5"]
        (job/"input.set").write_text("\n".join(contents)+"\n")
        (job/"group_graph").symlink_to(RENDER)
        # Intentionally no matrix_graph symlink: the unified path must work.
        environment=os.environ.copy()
        environment["OMP_NUM_THREADS"]="1"
        environment["OPENBLAS_NUM_THREADS"]="1"
        environment["SPINSPEC_Y_MIN"]=",".join(f"{s}:-0.25" for s in selected)
        environment["SPINSPEC_Y_MAX"]=",".join(f"{s}:2.5" for s in selected)
        if grouped:
            nspins=len(contents[1].split())
            groups=[0 if x<nspins//2 else 1 for x in range(nspins)]
            environment["SPINSPEC_OBSERVER_GROUPS"]=",".join(map(str,groups))
        p=subprocess.run([str(ENGINE),"input.set"],cwd=job,
                         env=environment,capture_output=True,text=True,timeout=90)
        assert p.returncode==0,(model,field,grouped,p.stderr[-5000:])
        ordinary=(job/"ordinary_plot_00000.gp")
        assert ordinary.is_file()
        plot=ordinary.read_text()
        assert plot.count("set yrange [-0.25:2.5]")==len(selected),plot[-4000:]
        assert (job/"data_00000.jpg").stat().st_size>3000
        if grouped:
            group=(job/"group_plot_00000.gp")
            assert group.is_file()
            assert "set yrange [-0.25:2.5]" in group.read_text()
            assert (job/"group_spectrum_00000.jpg").stat().st_size>3000
        else:
            assert not (job/"group_spectrum_00000.jpg").exists()
        assert (job/"Sp_00000.dat").is_file()
        print("PASS unified ordinary/grouped:",model,field,grouped)


if __name__=="__main__":
    run("two_spins.set",3000,False)
    run("three_spins.set",0,False)
    run("three_spins.set",0,True)
    print("UNIFIED RENDERER: PASS")
