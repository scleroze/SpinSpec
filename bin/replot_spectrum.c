/*
 * SpinSpec: render Lorentz-broadened display curves from SAVED transitions.
 * This executable never constructs or diagonalizes a Hamiltonian.
 *
 * Input (cwd): Sp_00000.dat, Sp_dc_00000.dat,
 *              Sp_tolerance_00000.dat, plot_params.dat.
 * Output:     Res_<spin>_00000.dat for the requested Graph panels.
 * The FastAPI layer invokes matrix_graph and optional group_graph afterwards.
 *
 * plot_params.dat format (whitespace-separated):
 *   spin_count panel_count field_mT FWHM_Hz width_Hz left_Hz right_Hz sum_abs_J_Hz
 *   spin_count rows of: gamma_MHz_per_T chemical_shift_ppm
 *   panel_count rows of: spin_index
 * A NaN left/right means automatic. All other values must be finite.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#define MAX_SPINS 128
#define GRID_POINTS 2000

typedef struct { double frequency, strength; } Transition;

static int by_frequency(const void *a, const void *b) {
    const double f1 = ((const Transition *)a)->frequency;
    const double f2 = ((const Transition *)b)->frequency;
    return (f1 > f2) - (f1 < f2);
}

static double auto_half_width(const Transition *lines, size_t count,
                              double reference, double fwhm, double total_abs_j) {
    const double radius = fmax(40.0, fmax(50.0*fwhm,
                                         2.0*total_abs_j + 10.0*fwhm));
    double peak = 0.0;
    for (size_t i=0; i<count; ++i) {
        if (fabs(lines[i].frequency-reference) <= radius &&
            lines[i].strength > peak) peak = lines[i].strength;
    }
    const double minimum = fmax(10.0, 12.0*fwhm);
    if (peak <= 0.0) return minimum;
    const double cutoff = fmax(1e-15, peak*1e-8);
    double outer = 0.0;
    for (size_t i=0; i<count; ++i) {
        const double delta = fabs(lines[i].frequency-reference);
        if (delta <= radius && lines[i].strength >= cutoff && delta > outer)
            outer = delta;
    }
    return fmax(minimum, outer*1.12 + fmax(4.0*fwhm, 2.0));
}

static int read_dc_and_tolerance(double *dc, double *tol) {
    FILE *fp = fopen("Sp_dc_00000.dat", "r");
    if (!fp) { perror("Sp_dc_00000.dat"); return -1; }
    char header[256];
    int good = fgets(header, sizeof(header), fp) &&
        fscanf(fp, "0 %lf", dc) == 1;
    fclose(fp);
    if (!good || !isfinite(*dc) || *dc < 0) return -1;
    fp = fopen("Sp_tolerance_00000.dat", "r");
    if (!fp) { perror("Sp_tolerance_00000.dat"); return -1; }
    good = fscanf(fp, "%lf", tol) == 1;
    fclose(fp);
    return good && isfinite(*tol) && *tol > 0 ? 0 : -1;
}

static int read_transitions(Transition **out, size_t *len, double tolerance) {
    FILE *fp = fopen("Sp_00000.dat", "r");
    if (!fp) { perror("Sp_00000.dat"); return -1; }
    char header[256];
    if (!fgets(header, sizeof(header), fp)) { fclose(fp); return -1; }
    size_t cap=8192, n=0;
    Transition *all = malloc(cap*sizeof(*all));
    if (!all) { fclose(fp); return -1; }
    int index, state_i, state_j, lookup;
    double frequency, strength;
    int fields;
    while ((fields=fscanf(fp, "%d %lf %lf %d %d %d", &index,
                          &frequency, &strength, &state_i, &state_j, &lookup)) == 6) {
        if (!isfinite(frequency) || !isfinite(strength) ||
            frequency < 0.0 || strength < 0.0) {
            fprintf(stderr, "Malformed saved transition.\n");
            free(all); fclose(fp); return -1;
        }
        if (frequency <= tolerance || strength < 1e-12) continue;
        if (n == cap) {
            if (cap > 10000000 / 2) { free(all); fclose(fp); return -1; }
            cap *= 2;
            Transition *temp = realloc(all, cap*sizeof(*all));
            if (!temp) { free(all); fclose(fp); return -1; }
            all = temp;
        }
        all[n++] = (Transition){frequency,strength};
    }
    fclose(fp);
    if (fields != EOF) {
        fprintf(stderr, "Saved transition list has invalid fields.\n");
        free(all); return -1;
    }
    if (n > 1) qsort(all,n,sizeof(*all),by_frequency);
    /* Exactly the same 1e-12 Hz clustering as engine_matrix.c. */
    size_t grouped=0;
    for (size_t i=0; i<n;) {
        const double base=all[i].frequency;
        long double sum=0.0L, offset=0.0L;
        size_t j=i;
        while (j<n && all[j].frequency-base <= 1e-12) {
            sum += (long double)all[j].strength;
            offset += (long double)all[j].strength *
                      (long double)(all[j].frequency-base);
            j++;
        }
        all[grouped].frequency = base+(double)(offset/sum);
        all[grouped].strength = (double)sum;
        grouped++;
        i=j;
    }
    *out=all; *len=grouped;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr,"Usage: %s plot_params.dat\n",argv[0]);
        return 2;
    }
    FILE *config=fopen(argv[1],"r");
    if (!config) { perror(argv[1]); return 2; }
    int nspins, npanels;
    double b0, fwhm, width, left, right, sum_abs_j;
    if (fscanf(config,"%d %d %lf %lf %lf %lf %lf %lf",
               &nspins,&npanels,&b0,&fwhm,&width,&left,&right,&sum_abs_j)!=8 ||
        nspins<=0 || nspins>MAX_SPINS || npanels<=0 || npanels>MAX_SPINS ||
        !isfinite(b0) || !isfinite(fwhm) || fwhm<=0 ||
        !isfinite(width) || width<0 ||
        (!isfinite(left) && !isnan(left)) ||
        (!isfinite(right) && !isnan(right)) ||
        !isfinite(sum_abs_j) || sum_abs_j<0 ||
        (isfinite(left) && isfinite(right) && left>=right)) {
        fprintf(stderr,"Invalid plot_params header.\n"); fclose(config); return 2;
    }
    double gamma[MAX_SPINS], shift[MAX_SPINS];
    int selected[MAX_SPINS], seen[MAX_SPINS]={0};
    for (int i=0;i<nspins;i++) {
        if (fscanf(config,"%lf %lf",&gamma[i],&shift[i])!=2 ||
            !isfinite(gamma[i]) || !isfinite(shift[i])) {
            fprintf(stderr,"Invalid spin parameters.\n"); fclose(config); return 2;
        }
    }
    for (int p=0;p<npanels;p++) {
        if (fscanf(config,"%d",&selected[p])!=1 ||
            selected[p]<0 || selected[p]>=nspins || seen[selected[p]]++) {
            fprintf(stderr,"Invalid Graph spin.\n"); fclose(config); return 2;
        }
    }
    fclose(config);

    double dc=0,tolerance=0;
    if (read_dc_and_tolerance(&dc,&tolerance)!=0) {
        fprintf(stderr,"Cannot load invariant DC or energy tolerance.\n"); return 1;
    }
    Transition *lines=NULL;
    size_t count=0;
    if (read_transitions(&lines,&count,tolerance)!=0) return 1;
    const double width2 = (fwhm*0.5)*(fwhm*0.5);
    for(int p=0;p<npanels;p++) {
        const int spin=selected[p];
        const double reference=fabs(b0*gamma[spin]*1000.0*(1.0-shift[spin]*1e-6));
        const double half = width>0 ? width : auto_half_width(lines,count,reference,fwhm,sum_abs_j);
        double x0=-half, x1=half;
        if (width==0) x0=fmax(x0,-reference);
        if (isfinite(left)) x0=left;
        if (isfinite(right)) x1=right;
        if (!(x0<x1)) {
            fprintf(stderr,"Invalid selected X window for spin %d.\n",spin);
            free(lines); return 1;
        }
        char filename[64];
        snprintf(filename,sizeof(filename),"Res_%d_00000.dat",spin);
        FILE *fp=fopen(filename,"w");
        if (!fp) { perror(filename); free(lines); return 1; }
        fprintf(fp,"%lf %lf\n",shift[spin],reference);
        for (int j=0;j<GRID_POINTS;j++) {
            const double x=x0+(x1-x0)*j/(GRID_POINTS-1.0);
            const double absolute=x+reference;
            double y=0;
            for (size_t i=0;i<count;i++) {
                const double d=lines[i].frequency-absolute;
                y += lines[i].strength/(1.0+d*d/width2);
            }
            if (dc>0) y += dc/(1.0+absolute*absolute/width2);
            /* Match the historical Res file precision. */
            fprintf(fp,"%lf %lf\n",x,y);
        }
        if (fclose(fp)!=0) { perror(filename); free(lines); return 1; }
        fprintf(stderr,"REPLOT spin=%d xrange=[%.12g,%.12g] transitions=%zu\n",
                spin,x0,x1,count);
    }
    free(lines);
    return 0;
}
