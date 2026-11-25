/*
 * SpinSpec: spin-1/2 NMR transition frequencies and relative line strengths.
 *
 * The program parses a .set model, constructs the isotropic J-coupled spin
 * Hamiltonian in the product basis, diagonalizes it with GSL, and writes
 * discrete transitions and Lorentz-broadened display spectra.
 *
 * Hamiltonian eigenvalues are expressed in Hz (H/h); magnetic fields in mT.
 * The ordinary and group-resolved modes use one gamma-weighted I_x observer.
 * Neither mode models state populations, preparation or receiver response.
 *
 * Build (Linux): gcc engine_matrix.c -o engine_matrix -fopenmp -lgsl -lopenblas -lm
 */

#include <math.h> 
#include <float.h>
#include <stdio.h>
#include <string.h> 
#include <stdlib.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_eigen.h>
#include <gsl/gsl_blas.h>
#include "engine_matrix.h"
#include "observer_groups.h"
#include "observer_invariant.h"
#include <ctype.h>
#include <time.h>
#define MAX_LINE_LENGTH 1024
#define MAX_WORD_LENGTH 256

char line[MAX_LINE_LENGTH];
char Title[MAX_WORD_LENGTH];

// Skip leading whitespace without modifying the input buffer.
char *skip_spaces(char *str) {
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

// Read the next input record; stop on EOF or an I/O error.
int read_line_or_exit(FILE *file, char *line, size_t line_size) {
    if (fgets(line, line_size, file) == NULL) {
        if (feof(file)) {

            printf("Input file is empty or end of file reached.\n");
            fclose(file);
            exit(0); 
        } else {

            perror("Error reading input line");
            fclose(file);
            exit(1); 
        }
    }
    return 1; 
}


/* Sort transition pairs by frequency. */
static int ss_compare_frequency(const void *a, const void *b)
{
    const double fa = ((const double *)a)[0];
    const double fb = ((const double *)b)[0];
    return (fa > fb) - (fa < fb);
}


static double ss_wall_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * ts.tv_nsec;
}

int main(int argc, char *argv[]) {
    int i,j,k;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input.set>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *Mfile = argv[1];
  
    FILE *file = fopen(Mfile, "r");
    if (file == NULL) {
        perror("Error opening input file");
        return 1;
    }

// Read the optional model title from the first .set record.
        read_line_or_exit(file, line, sizeof(line));


        if (line[0] == '#') {

            char *word_start = skip_spaces(line + 1);


            size_t i = 0;
            while (*word_start && !isspace((unsigned char)*word_start) && i < MAX_WORD_LENGTH - 1) {
                Title[i++] = *word_start++;
            }
            Title[i] = '\0'; 
        }

/*
 * Parse spin species: the second .set record lists gamma/(2*pi) in MHz/T.
 * The stored coefficients G[k] are converted to Hz/mT by multiplying by 1000.
 * S is the number of spin-1/2 nuclei; the Hilbert-space dimension is 2^S.
 */
    int S = 0;
    double TMP;
    const char *ptr = line;
    read_line_or_exit(file, line, sizeof(line));


      while (sscanf(ptr, "%lf", &TMP) == 1) {
        S++;
        while (*ptr != ' ' && *ptr != '\0') ptr++; 
        if (*ptr == ' ') ptr++;
      }

      int StatesNumber = (int)(pow(2,S));
      int C[S][StatesNumber];


      double *G = malloc(S * sizeof(double));
      if (G == NULL) {
        perror("Memory allocation failed");
        return 1;
      }
      

      ptr = line;
      double Gir;
      for (int i = 0; i < S; i++) {
        sscanf(ptr, "%lf", &Gir);
	G[i] = Gir*1000;
        while (*ptr != ' ' && *ptr != '\0') ptr++; 
        if (*ptr == ' ') ptr++;
      }
 
// Read one chemical shift per spin (ppm); shifts are dimensionless 1e-6.
    double *Sigma = malloc(S * sizeof(double));

    read_line_or_exit(file, line, sizeof(line));    


       if (Sigma == NULL) {
           perror("Memory allocation failed");
           return 1;
       }


       ptr = line;
       for (int i = 0; i < S; i++) {
          sscanf(ptr, "%lf", &Sigma[i]);
          while (*ptr != ' ' && *ptr != '\0') ptr++; 
        if (*ptr == ' ') ptr++;
       }
   
/*
 * Read the magnetic field in mT: either one value (single spectrum) or
 * start, stop, and increment (a linear field sweep).
 * This version of the GitHub source does not implement logarithmic sweeps.
 */
    double B0, B01, B02 = 0, BS;

    read_line_or_exit(file, line, sizeof(line));   
      ptr = line;
      if (strchr(line, ' ') != NULL) { 
    	sscanf(ptr, "%lf %lf %lf", &B01, &B02, &BS);
    	printf ("Field_3 B1 %lf, B2 %lf, Bs %lf\n", B01, B02, BS);
      }	
      else { 
    	sscanf(ptr, "%lf", &B01);
    	B02 = B01; 
    	printf ("Field_3 B0 %lf\n", B01);
      } 

/*
 * Read scalar J couplings in Hz as "spin_i spin_j J_ij".
 * Jm is initialized to zero; the Hamiltonian below reads Jm[k][l]
 * only for k < l. Input records must therefore use that index order.
 */
     double Jm[S][S]; // J-coupling parameters in Hz
     for (i=0; i < S; i++) { 
        for (j=0; j < S; j++) {
           Jm[j][i] = 0; Jm[i][j] = 0;
        }
     }

     const char *stopGraph = "Graph"; // stop reading couplings at the Graph record
     while (fgets(line, sizeof(line), file)) {

        if (strncmp(line, stopGraph, strlen(stopGraph)) == 0)
            break;

// Ignore blank records between J-coupling entries.
        char *p = line;
        while (isspace((unsigned char)*p))
            p++;

        if (*p == '\0')
        continue;

        if (sscanf(p, "%d %d %lf", &i, &j, &TMP) != 3) {
            fprintf(stderr, "Error: invalid J-coupling line: %s", line);
            fclose(file);
            return EXIT_FAILURE;
        }

        if (i < 0 || i >= S || j < 0 || j >= S) {
            fprintf(stderr,
                    "Error: spin index out of range in J-coupling: %d %d\n",
                    i, j);
            fclose(file);
            return EXIT_FAILURE;
        }

     Jm[i][j] = TMP;
     }

/*
 * The Graph record selects spins for display only; it does not select which
 * spins enter the Hamiltonian. Spin indices are zero-based in .set files.
 */

    int Sg=0;
    j=0; i=0;
    char *ptr1 = line + 6; // skip the literal 'Graph ' prefix
    while (sscanf(ptr1, "%d", &j) == 1) { 
        while (*ptr1 != ' ' && *ptr1 != '\0') { ptr1++; } 
        if (*ptr1 == ' ') {
            ptr1++;
        }
        i++;
    }
    Sg = i; 
    if (Sg <= 0) {
        fprintf(stderr, "Error: no spins specified in Graph line.\n");
        return EXIT_FAILURE;
    }

    int *Sgm = malloc(Sg * sizeof(int));
    if (Sgm == NULL) {
        perror("Memory allocation failed");
        return 1;
    }


    char *ptr2 = line + 6;
    for (int i = 0; i < Sg; i++) {
        sscanf(ptr2, "%d", &Sgm[i]);
       
        if (Sgm[i] < 0 || Sgm[i] >= S) {
            fprintf(stderr,
                    "Error: spin index out of range in Graph line: %d\n",
                    Sgm[i]);
            return EXIT_FAILURE;
        }
       
        while (*ptr2 != ' ' && *ptr2 != '\0') ptr2++; 
        if (*ptr2 == ' ') ptr2++;
    }

// Read vertical ranges for the separately displayed spin windows.


    if (fgets(line, sizeof(line), file) == NULL) {
        perror("Error reading input line");
        return 1;
    }


    i = 0;
    ptr = line;
    while (sscanf(ptr, "%lf", &TMP) == 1) {
        i++;
        while (*ptr != ' ' && *ptr != '\0') ptr++; // Advance to the next numeric value
        if (*ptr == ' ') ptr++;
    }
    

    double *V_size = malloc(Sg * sizeof(double));
    if (V_size == NULL) {
        perror("Memory allocation failed");
        return 1;
    }


    ptr = line;
    for (int i = 0; i < Sg; i++) {
        sscanf(ptr, "%lf", &V_size[i]);

        while (*ptr != ' ' && *ptr != '\0') ptr++; // Advance to the next numeric value
        if (*ptr == ' ') ptr++;
    }

/*
 * Read Lorentzian full width at half maximum (FWHM, Hz) and the displayed
 * half-width around each selected spin's reference frequency (Hz).
 * The Lorentzian half-width at half maximum is gamma = FWHM/2.
 */
double fwhm_hz, WidthLine, WidthSpectrum;
fscanf(file, "%lf %lf\n", &fwhm_hz, &WidthSpectrum);
   fclose(file);
double gamma = fwhm_hz / 2.0;
WidthLine = gamma * gamma;
/*
 * Construct the product basis for S spin-1/2 nuclei.
 * C[k][i] = 2*m_k, where m_k = +/-1/2 is the projection of spin k in
 * basis state i; thus each C entry is +1 or -1.
 * States are ordered by binary index, with the reversed column index
 * preserving this implementation's original basis ordering.
 */

int A, B;


for (i=0; i < StatesNumber; i++) {
   A = i;
   for (j=0; j < S; j++) {
      B = A%2; 
      if (B == 0) C[S-j-1][StatesNumber-i-1] = -1;
      else  C[S-j-1][StatesNumber-i-1] = B;
      A /= 2;
   }
}



int Bc = 0; // spectrum/frame index
double *HG = (double*)malloc(StatesNumber * StatesNumber * sizeof(double));
if (!HG) {
    perror("Memory allocation failed");
    exit(1);
}
double D;
int st, k1, k2;
int *I = (int*)malloc(StatesNumber * StatesNumber * sizeof(int));
if (I == NULL) {
    fprintf(stderr, "Error: memory allocation failed for transition matrix I.\n");
    return EXIT_FAILURE;
}
int l, m, n = 0;
double x1, x2; 
FILE *fsi, *fsp;
char fname[50];

/*
 * Sweep the magnetic field and compute a complete spectrum at each value.
 * Each field step generates Sp_<frame>.dat, Res_<spin>_<frame>.dat and
 * a JPEG image through the graphing helper.
 */
B0 = B01;
if (B02 == B01) { BS = 1; } // advance once for a single-field calculation
while (B0 <= B02) {
/*
 * Hamiltonian diagonal elements in frequency units (Hz):
 *
 * H_ii/h = (B0/2) sum_k G_k (1 - sigma_k*1e-6) C[k][i]
 *        + (1/4) sum_{k<l} J_kl C[k][i] C[l][i].
 *
 * B0 is in mT; G_k is in Hz/mT; J_kl is in Hz.
 * The sign convention for the Zeeman term is the one used by this code.
 */


for (int i = 0; i < StatesNumber; i++) {
    for (int j = 0; j < StatesNumber; j++) {
        HG[i * StatesNumber + j] = 0;  
    }
}

   for (i=0; i < StatesNumber; i++) {
      D = 0;
      for (j=0; j < S; j++) {

         D = D + (B0 * 0.5 * G[j] * (1-Sigma[j]/1e6) * C[j][i]); // chemical shifts in ppm

         for (k=j+1; k < S; k++) {
            if (C[j][i] == C[k][i]) D = D + (0.25 * Jm[j][k]);
            else D = D - (0.25 * Jm[j][k]);
        }
      }
      HG[i * StatesNumber + i] = D;
   }

/*
 * Build two distinct objects in the product basis:
 *   I[a,b] : 1-based index of the single spin flipped between states a,b;
 *            zero when the two basis states do not differ by one spin.
 *   HG[a,b]: isotropic J-coupling flip-flop terms between basis states
 *            differing by opposite flips of two coupled spins.
 *
 * I is a transition-operator lookup and is NOT diagonalized; its matrix
 * elements are transformed to the Hamiltonian eigenbasis below via evec.
 */


	   for (i=0; i < StatesNumber; i++) 
	      for (j=0; j < StatesNumber; j++) I[j*StatesNumber+i] = 0; // initialize the one-spin-flip lookup to zero

	for (i=0; i < StatesNumber; i++) {
	    for (j=i; j < StatesNumber; j++) {
		st = 0; k1 = 0; k2 = 0; D = 0;
		for (k=0; k < S; k++) {  
		    D = D + C[k][i] - C[k][j]; 
		     if (C[k][j] == C[k][i])  
		         st = st+1; 
		     else 
		         if (k1 == 0) k1 = k+1;  
		         else  k2 = k+1; 
		}
// A single spin flip is allowed in the product-basis transition operator.
		if (k1 != 0) 
		      if (k2 == 0) {   
		         I[j*StatesNumber+i]=k1; 
		         I[i*StatesNumber+j]=k1;
		      } 
// Off-diagonal J/2 exchange term for two oppositely flipped spins.
		if (D == 0) 
		    if (st == S-2) {  
		          HG[i * StatesNumber + j] = 0.5*Jm[k1-1][k2-1];
		          HG[j * StatesNumber + i] = 0.5*Jm[k1-1][k2-1];
		    } 
	     }            
	}











	    gsl_matrix *Am = gsl_matrix_alloc(StatesNumber, StatesNumber);


            for (int i = 0; i < StatesNumber; i++) {
                gsl_vector_view row_view = gsl_vector_view_array(&HG[i * StatesNumber], StatesNumber);
                gsl_matrix_set_row(Am, i, &row_view.vector);
            }
	    

	    
/*
 * Diagonalize the real symmetric Hamiltonian with GSL.
 * eval contains eigenvalues in Hz; column i of evec contains eigenstate i
 * expressed in the original product basis (GSL does not sort by default).
 */
	    gsl_vector *eval = gsl_vector_alloc(StatesNumber);
	    gsl_matrix *evec = gsl_matrix_alloc(StatesNumber, StatesNumber);

	    /* Exact diagonalization in sectors of conserved total Mz. */
{
    int count[S+1], start[S+2], next[S+1];
    int basis[StatesNumber], sector[StatesNumber];

    for (int q=0; q<=S; ++q) count[q]=0;

    for (int p=0; p<StatesNumber; ++p) {
        int sum=0;
        for (int q=0; q<S; ++q) {
            if (C[q][p] != 1 && C[q][p] != -1) {
                fprintf(stderr, "Invalid spin projection\n");
                return EXIT_FAILURE;
            }
            sum += C[q][p];
        }
        sector[p]=(S+sum)/2;
        count[sector[p]]++;
    }

    start[0]=0;
    for (int q=0; q<=S; ++q)
        start[q+1]=start[q]+count[q];

    for (int q=0; q<=S; ++q) next[q]=start[q];

    for (int p=0; p<StatesNumber; ++p)
        basis[next[sector[p]]++]=p;

    /* Check that inter-sector Hamiltonian elements vanish. */
    for (int p=0; p<StatesNumber; ++p)
        for (int q=p+1; q<StatesNumber; ++q)
            if (sector[p]!=sector[q] &&
                fabs(gsl_matrix_get(Am,p,q))>1e-10) {
                fprintf(stderr, "Hamiltonian violates Mz conservation\n");
                return EXIT_FAILURE;
            }

    gsl_matrix_set_zero(evec);

    for (int q=0; q<=S; ++q) {
        int dim=count[q];
        int off=start[q];

        fprintf(stderr,"BLOCK sector=%d dimension=%d\n",q,dim);

        if (dim==1) {
            int p=basis[off];
            gsl_vector_set(eval,off,gsl_matrix_get(Am,p,p));
            gsl_matrix_set(evec,p,off,1.0);
            continue;
        }

        gsl_matrix *H=gsl_matrix_alloc(dim,dim);
        gsl_matrix *V=gsl_matrix_alloc(dim,dim);
        gsl_vector *E=gsl_vector_alloc(dim);
        gsl_eigen_symmv_workspace *W=gsl_eigen_symmv_alloc(dim);

        for (int r=0; r<dim; ++r)
            for (int c=0; c<dim; ++c)
                gsl_matrix_set(H,r,c,
                    gsl_matrix_get(Am,basis[off+r],basis[off+c]));

        if (gsl_eigen_symmv(H,E,V,W)!=0) {
            fprintf(stderr,"Block diagonalization failed\n");
            return EXIT_FAILURE;
        }

        for (int c=0; c<dim; ++c) {
            gsl_vector_set(eval,off+c,gsl_vector_get(E,c));
            for (int r=0; r<dim; ++r)
                gsl_matrix_set(evec,basis[off+r],off+c,
                               gsl_matrix_get(V,r,c));
        }

        gsl_eigen_symmv_free(W);
        gsl_vector_free(E);
        gsl_matrix_free(V);
        gsl_matrix_free(H);
    }
}

    /* Match the energy-block tolerance of observer_invariant.h. The
     * finite-frequency strength sum, subtracted from Tr(O^2)/2, gives
     * the complete basis-invariant one-sided DC strength. This includes
     * diagonal eigenbasis elements, even for degenerate eigenspaces. */
    double max_abs_energy = 1.0;
    for (int state = 0; state < StatesNumber; ++state) {
        double energy = fabs(gsl_vector_get(eval, state));
        if (energy > max_abs_energy) max_abs_energy = energy;
    }
    const double observer_energy_tolerance =
        fmax(1e-10, 64.0 * DBL_EPSILON * max_abs_energy);
    double observer_total_power = 0.0;
    for (int spin = 0; spin < S; ++spin) {
        const double weight = G[spin] / SS_OBS_GAMMA_REF_HZ_PER_MT;
        observer_total_power += weight * weight;
    }
    observer_total_power *= (double)StatesNumber / 8.0;
    double observer_ac_power = 0.0;

/*
 * Calculate every pair of Hamiltonian eigenstates:
 *
 *   nu_ij = abs(eval[j] - eval[i])
 *   D_ij  = sum_{a<b; I[a,b]!=0}
 *             (V[a,i]*V[b,j] + V[b,i]*V[a,j])
 *   A_ij  = D_ij^2
 *
 * V is evec. The ordinary spectrum uses the SAME gamma-weighted operator
 * as the sum of all spin groups: O = sum_k (gamma_k/gamma_H) I_kx.
 * An I_kx flip has matrix element 1/2; retain the signed gamma ratio.
 * Populations and receiver response are not modeled; strengths are relative.
 */
	sprintf(fname, "Sp_%05d.dat", Bc); 
	fsp = fopen(fname, "w");
    if (fsp == NULL) {
        perror(fname);
        return EXIT_FAILURE;
    } 
	n=0;
        fprintf(fsp, "n     x1      x2   i j I\n");
        int progress = 0; // number of completed eigenstate pairs
        int total = StatesNumber * (StatesNumber - 1) / 2;

        char plot_filename[64];
        snprintf(plot_filename, sizeof(plot_filename),
                 "Sp_plot_%05d.dat", Bc);

        FILE *fsp_plot = fopen(plot_filename, "w");
        if (!fsp_plot) {
            perror(plot_filename);
            return EXIT_FAILURE;
        }

        fprintf(fsp_plot, "n     x1      x2   i j I\n");

        double *Spr = NULL;
        size_t spr_count = 0;
        size_t spr_capacity = 0;
 // total unordered eigenstate pairs

    /*
     * Gamma-weighted observer transformation:
     *
     *     Mobs = V^T O V
     *
     * O has nonzero matrix elements only for single-spin flips.
     * Full-precision transition strengths are retained.
     */

    gsl_matrix *OV = gsl_matrix_calloc(
        StatesNumber, StatesNumber
    );

    gsl_matrix *Mobs = gsl_matrix_alloc(
        StatesNumber, StatesNumber
    );

    if (!OV || !Mobs) {
        fprintf(stderr, "Observer allocation failed\n");
        return EXIT_FAILURE;
    }

    for (int a = 0; a < StatesNumber; ++a) {
        for (int b = a + 1; b < StatesNumber; ++b) {

            int spin = I[a * StatesNumber + b] - 1;
            if (spin < 0) continue;

            double weight = G[spin] /
                (2.0 * SS_OBS_GAMMA_REF_HZ_PER_MT);

            double *row_a = OV->data + a * OV->tda;
            double *row_b = OV->data + b * OV->tda;

            for (int col = 0; col < StatesNumber; ++col) {
                row_a[col] += weight *
                    gsl_matrix_get(evec, b, col);

                row_b[col] += weight *
                    gsl_matrix_get(evec, a, col);
            }
        }
    }

    if (gsl_blas_dgemm(
            CblasTrans, CblasNoTrans,
            1.0, evec, OV,
            0.0, Mobs) != 0) {

        fprintf(stderr, "BLAS transformation failed\n");
        return EXIT_FAILURE;
    }

    double ss_t_write = ss_wall_seconds();
for (i = 0; i < StatesNumber - 1; ++i) {
        for (j = i + 1; j < StatesNumber; ++j) {

            x1 = fabs(
                gsl_vector_get(eval, j) -
                gsl_vector_get(eval, i)
            );

            double amplitude = gsl_matrix_get(Mobs, i, j);
            x2 = amplitude * amplitude;

            /*
             * Accumulate unrounded finite-frequency strength.
             * This preserves the invariant DC calculation.
             */
            if (x1 > observer_energy_tolerance)
                observer_ac_power += x2;


            /* Full transition output is deferred. */

            ++n;
            ++progress;

            if (x1 > observer_energy_tolerance &&
                x2 >= 1e-12) {

                if (spr_count == spr_capacity) {
                    size_t next_capacity =
                        spr_capacity ? spr_capacity * 2 : 8192;

                    double *tmp = realloc(
                        Spr,
                        2 * next_capacity * sizeof(double)
                    );

                    if (!tmp) {
                        fprintf(stderr,
                                "Spr allocation failed\n");
                        exit(EXIT_FAILURE);
                    }

                    Spr = tmp;
                    spr_capacity = next_capacity;
                }

                Spr[2 * spr_count] = x1;
                Spr[2 * spr_count + 1] = x2;
                ++spr_count;
            }

            /*
             * Visualization only.
             * Full-precision Sp and broadened Res retain
             * all their original contributions.
             */
            if (x2 >= 5e-7) {
                fprintf(fsp_plot,
                    "%d %.17g %.17g %d %d %d\n",
                    n - 1, x1, x2,
                    i + 1, j + 1,
                    I[i * StatesNumber + j]
                );
            }

        }
    }


    /*
     * Parallel, deterministic high-precision transition writer.
     * Physics, observer_ac_power and Spr were computed serially.
     * Two workers format disjoint, consecutive state-pair ranges.
     */
    {
        double ss_t_parallel = ss_wall_seconds();

        const int N = StatesNumber;
        const size_t total_pairs =
            (size_t)N * (size_t)(N - 1) / 2;

        int split = 0;

        /* Split by transition count, not by number of rows. */
        while (split < N - 1) {
            size_t prefix =
                (size_t)split *
                (2 * (size_t)N - (size_t)split - 1) / 2;

            if (prefix >= total_pairs / 2)
                break;

            ++split;
        }

        FILE *parts[2] = {NULL, NULL};
        int failures[2] = {0, 0};

        #pragma omp parallel for schedule(static) num_threads(2)
        for (int worker = 0; worker < 2; ++worker) {

            const int begin = worker == 0 ? 0 : split;
            const int finish = worker == 0 ? split : N - 1;

            FILE *part = tmpfile();

            if (!part) {
                failures[worker] = 1;
                continue;
            }

            setvbuf(part, NULL, _IOFBF, 1 << 20);

            parts[worker] = part;

            for (int row = begin;
                 row < finish && !failures[worker];
                 ++row) {

                const double energy_i =
                    gsl_vector_get(eval, row);

                const size_t first_index =
                    (size_t)row *
                    (2 * (size_t)N - (size_t)row - 1) / 2;

                for (int col = row + 1; col < N; ++col) {

                    const double frequency = fabs(
                        gsl_vector_get(eval, col) - energy_i
                    );

                    const double amplitude =
                        gsl_matrix_get(Mobs, row, col);

                    const double strength =
                        amplitude * amplitude;

                    const int index = (int)(
                        first_index +
                        (size_t)(col - row - 1)
                    );

                    const int lookup = I[row * N + col];

                    int written;

                    if (strength == 0.0) {
                        written = fprintf(
                            part,
                            "%d %.17g 0 %d %d %d\n",
                            index, frequency,
                            row + 1, col + 1, lookup
                        );
                    } else {
                        written = fprintf(
                            part,
                            "%d %.17g %.17g %d %d %d\n",
                            index, frequency, strength,
                            row + 1, col + 1, lookup
                        );
                    }

                    if (written < 0) {
                        failures[worker] = 1;
                        break;
                    }
                }
            }

            if (fflush(part) != 0)
                failures[worker] = 1;

            if (fseek(part, 0, SEEK_SET) != 0)
                failures[worker] = 1;
        }

        int write_error = failures[0] || failures[1] ||
                          !parts[0] || !parts[1];

        const size_t buffer_size = 1 << 20;
        unsigned char *buffer = malloc(buffer_size);

        if (!buffer)
            write_error = 1;

        if (!write_error) {
            for (int worker = 0; worker < 2; ++worker) {

                size_t bytes;

                while ((bytes = fread(
                            buffer, 1, buffer_size,
                            parts[worker])) != 0) {

                    if (fwrite(buffer, 1, bytes, fsp) != bytes) {
                        write_error = 1;
                        break;
                    }
                }

                if (ferror(parts[worker]))
                    write_error = 1;

                if (write_error)
                    break;
            }
        }

        free(buffer);

        for (int worker = 0; worker < 2; ++worker)
            if (parts[worker])
                fclose(parts[worker]);

        if (write_error) {
            fprintf(stderr,
                "Parallel transition writer failed\n");
            return EXIT_FAILURE;
        }

        fprintf(stderr,
            "PROFILE parallel formatting: %.3f s\n",
            ss_wall_seconds() - ss_t_parallel);
    }

gsl_matrix_free(Mobs);
    gsl_matrix_free(OV);

    printf("\nDone!\n");


    /* DC = 1/2 Tr(O^2) - sum_{nondegenerate i<j} |O_ij|^2.
     * Tr(O^2)/2 = 2^S / 8 * sum_k (gamma_k/gamma_H)^2.
     * Computing this from the sum rule avoids arbitrary eigenvectors
     * in degenerate subspaces and costs O(1) extra memory. */
    double observer_dc_power = observer_total_power - observer_ac_power;
    if (fabs(observer_dc_power) <= 1e-10 * observer_total_power)
        observer_dc_power = 0.0;
    if (observer_dc_power < 0.0 || !isfinite(observer_dc_power)) {
        fprintf(stderr, "Invalid invariant DC strength: %.17g\n", observer_dc_power);
        return EXIT_FAILURE;
    }
    char dc_filename[64];
    snprintf(dc_filename, sizeof(dc_filename), "Sp_dc_%05d.dat", Bc);
    FILE *dc_file = fopen(dc_filename, "w");
    if (!dc_file) { perror(dc_filename); return EXIT_FAILURE; }
    fprintf(dc_file, "frequency_hz strength\n0 %.17g\n", observer_dc_power);
    fclose(dc_file);
    printf("Invariant DC strength: %.12g (sum-rule total %.12g)\n",
           observer_dc_power, observer_total_power);

    /*
     * Experimental, opt-in group decomposition of the SAME gamma-weighted
     * operator used by the ordinary Sp_*.dat. File paths stay unchanged. When enabled, write a separate
     * CSV for each magnetic-field step. Example:
     *
     *   SPINSPEC_OBSERVER_GROUPS=0,0,0,1 ./engine_matrix model.set
     *
     * This model includes coherent cross terms between groups, but omits
     * populations, preparation and detector response. The group-resolved
     * transition strengths are NOT additive without interference.
     */
    {
        const char *group_spec = getenv("SPINSPEC_OBSERVER_GROUPS");
        if (group_spec && *group_spec) {
            int group_ids[S], ngroups = 0;
            int pair_count;
            size_t pair_capacity = (size_t)S * (size_t)StatesNumber / 2;
            SSObsPair *obs_pairs = NULL;
            double *evec_rowmajor = NULL;
            FILE *groups_file = NULL;
            char group_filename[64];
            if (ss_obs_parse_groups(group_spec, S, group_ids, &ngroups) != 0) {
                fprintf(stderr, "Invalid SPINSPEC_OBSERVER_GROUPS: expected %d comma-separated group IDs (0..9).\n", S);
                exit(EXIT_FAILURE);
            }
            obs_pairs = malloc(pair_capacity * sizeof(*obs_pairs));
            evec_rowmajor = malloc((size_t)StatesNumber * (size_t)StatesNumber * sizeof(*evec_rowmajor));
            if (!obs_pairs || !evec_rowmajor) {
                fprintf(stderr, "Group observer: allocation failure.\n");
                free(obs_pairs); free(evec_rowmajor);
                exit(EXIT_FAILURE);
            }
            pair_count = ss_obs_collect_pairs(StatesNumber, S, ngroups,
                                               group_ids, G, I, obs_pairs,
                                               pair_capacity);
            if (pair_count < 0) {
                fprintf(stderr, "Group observer: invalid spin-flip lookup.\n");
                free(obs_pairs); free(evec_rowmajor);
                exit(EXIT_FAILURE);
            }
            for (int aa = 0; aa < StatesNumber; ++aa)
                for (int bb = 0; bb < StatesNumber; ++bb)
                    evec_rowmajor[(size_t)aa * StatesNumber + bb] =
                        gsl_matrix_get(evec, aa, bb);
            snprintf(group_filename, sizeof(group_filename),
                     "Sp_groups_%05d.csv", Bc);
            groups_file = fopen(group_filename, "w");
            if (!groups_file) {
                perror(group_filename);
                free(obs_pairs); free(evec_rowmajor);
                exit(EXIT_FAILURE);
            }
            fprintf(groups_file, "state_i,state_j,frequency_hz,total_strength,interference");
            for (int g = 0; g < ngroups; ++g)
                fprintf(groups_file, ",amplitude_g%d,strength_g%d", g, g);
            fprintf(groups_file, "\n");
            for (int ei = 0; ei < StatesNumber-1; ++ei) {
                for (int ej = ei+1; ej < StatesNumber; ++ej) {
                    SSObsResult obs;
                    double nu = fabs(gsl_vector_get(eval,ej) - gsl_vector_get(eval,ei));
                    if (ss_obs_transition(StatesNumber, ngroups, obs_pairs,
                                          pair_count, evec_rowmajor, ei, ej,
                                          &obs) != 0) {
                        fprintf(stderr, "Group observer: transition error.\n");
                        fclose(groups_file);
                        free(obs_pairs); free(evec_rowmajor);
                        exit(EXIT_FAILURE);
                    }
                    fprintf(groups_file, "%d,%d,%.12g,%.12g,%.12g",
                            ei+1, ej+1, nu, obs.total_strength, obs.interference);
                    for (int g = 0; g < ngroups; ++g)
                        fprintf(groups_file, ",%.12g,%.12g",
                                obs.group_amplitude[g], obs.group_strength[g]);
                    fputc('\n', groups_file);
                }
            }
            fclose(groups_file);
            /*
             * Subspace-aggregated, basis-invariant one-sided spectrum.
             * Includes 1/2 of all matrix elements within each degenerate
             * block at zero frequency, including diagonal elements.
             */
            {
                SSObsInvariantSpectrum invariant = {0};
                double *eigenvalues = malloc((size_t)StatesNumber * sizeof(*eigenvalues));
                char invariant_filename[64];
                FILE *invariant_file;
                if (!eigenvalues) {
                    fprintf(stderr, "Invariant observer: allocation failure.\n");
                    exit(EXIT_FAILURE);
                }
                for (int ei = 0; ei < StatesNumber; ++ei)
                    eigenvalues[ei] = gsl_vector_get(eval, ei);
                if (ss_obs_invariant_build(StatesNumber, ngroups, obs_pairs,
                        pair_count, evec_rowmajor, eigenvalues,
                        &invariant) != 0) {
                    fprintf(stderr, "Invariant observer: construction failure.\n");
                    free(eigenvalues);
                    exit(EXIT_FAILURE);
                }
                snprintf(invariant_filename, sizeof(invariant_filename),
                         "Sp_invariant_%05d.csv", Bc);
                invariant_file = fopen(invariant_filename, "w");
                if (!invariant_file) {
                    perror(invariant_filename);
                    ss_obs_invariant_free(&invariant);
                    free(eigenvalues);
                    exit(EXIT_FAILURE);
                }
                fprintf(invariant_file,
                    "block_a,block_b,dim_a,dim_b,energy_a_hz,energy_b_hz,"
                    "frequency_hz,total_strength,interference");
                for (int g = 0; g < ngroups; ++g)
                    fprintf(invariant_file, ",strength_g%d", g);
                fputc('\n', invariant_file);
                for (int k = 0; k < invariant.line_count; ++k) {
                    const SSObsInvariantLine *line = &invariant.lines[k];
                    fprintf(invariant_file,
                        "%d,%d,%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g",
                        line->block_a, line->block_b,
                        line->dim_a, line->dim_b, line->energy_a_hz,
                        line->energy_b_hz, line->frequency_hz,
                        line->total_strength, line->interference);
                    for (int g = 0; g < ngroups; ++g)
                        fprintf(invariant_file, ",%.17g",
                                line->group_strength[g]);
                    fputc('\n', invariant_file);
                }
                fclose(invariant_file);
                printf("Invariant observation written: %s, %d blocks "
                       "(tolerance %.6g Hz)\n", invariant_filename,
                       invariant.block_count,
                       invariant.energy_tolerance_hz);
                ss_obs_invariant_free(&invariant);
                free(eigenvalues);
            }
            free(obs_pairs);
            free(evec_rowmajor);
            printf("Experimental group observer written: %s (%d groups)\n",
                   group_filename, ngroups);
        }
    }



// Release eigensolver objects after writing the transition list.
	    gsl_vector_free(eval);
	    gsl_matrix_free(evec);
	    gsl_matrix_free(Am);

          fclose(fsp);
    fprintf(stderr,
        "PROFILE transitions and DC: %.3f s\n",
        ss_wall_seconds() - ss_t_write);
          fclose(fsp_plot);

          fprintf(stderr,
                  "FASTIO transitions for broadening: %zu / %d\n",
                  spr_count, n);

          double ss_t_group = ss_wall_seconds();
    /* Spr already contains full-precision transitions. */

/*
 * Group nearby transitions for Lorentzian broadening only.
 * Preserve Sp, Sp_plot and invariant DC output.
 *
 * Maximum frequency span within each group: 1e-12 Hz.
 * Strength sums and weighted frequency offsets use long double.
 */
{
    const double tolerance_hz = 1e-12;
    const size_t original_count = spr_count;
    size_t grouped_count = 0;
    double max_span_hz = 0.0;

    if (spr_count > 1) {
        qsort(Spr, spr_count, 2 * sizeof(double),
              ss_compare_frequency);
    }

    size_t pos = 0;

    while (pos < original_count) {
        const double base_frequency = Spr[2 * pos];
        long double strength_sum = 0.0L;
        long double weighted_offset = 0.0L;

        size_t next = pos;

        while (next < original_count &&
               Spr[2 * next] - base_frequency <= tolerance_hz) {

            const double frequency = Spr[2 * next];
            const double strength = Spr[2 * next + 1];

            strength_sum += (long double)strength;
            weighted_offset +=
                (long double)strength *
                (long double)(frequency - base_frequency);

            ++next;
        }

        const double span =
            Spr[2 * (next - 1)] - base_frequency;

        if (span > max_span_hz)
            max_span_hz = span;

        Spr[2 * grouped_count] =
            base_frequency +
            (double)(weighted_offset / strength_sum);

        Spr[2 * grouped_count + 1] =
            (double)strength_sum;

        ++grouped_count;
        pos = next;
    }

    spr_count = grouped_count;

    fprintf(stderr,
        "GROUPED transitions: %zu -> %zu; max span %.6g Hz\n",
        original_count, grouped_count, max_span_hz);
}



fprintf(stderr, "PROFILE grouping: %.3f s\n",
        ss_wall_seconds() - ss_t_group);

/*
 * Each selected spin gets a 2000-point frequency grid centered on
 * Freq[m] = abs(B0 * G[spin] * (1 - sigma[spin]*1e-6)).
 * The contributions from ALL calculated transitions are summed as
 * A/(1 + (nu - nu0)^2/gamma^2), gamma = FWHM/2.
 */
	/* The frequency grid is sampled by integer index for deterministic output. */

	    double Sgp[2][2000]; // frequency grid and Lorentzian intensity
	    double Freq[10];
	    char Kfile[50];

	    double ss_t_broad = ss_wall_seconds();
for (m=0; m<Sg; m++) { // one display window per selected spin
                printf("spin %i %d\n", m, Sgm[m]);
                Freq[m] = fabs(B0*G[Sgm[m]]*(1-Sigma[Sgm[m]]/1e6)); // reference Larmor frequency in Hz
                printf("frequency %i %lf\n", m, Freq[m]);
	       sprintf(Kfile, "Res_%d_%05d.dat", Sgm[m], Bc); // write the broadened display spectrum
	       fsi = fopen(Kfile, "w");
           if (fsi == NULL) {
               perror(Kfile);
               free(Spr);
               return EXIT_FAILURE;
           }
	       fprintf(fsi,"%lf %lf\n",Sigma[Sgm[m]] ,Freq[m]); // first record: chemical shift (ppm) and reference frequency (Hz)
	      
 	       /* Broaden all finite-frequency transitions and the separate
	        * invariant DC contribution; do not use basis-dependent raw
	        * i<j entries for numerically degenerate levels. */
	       for (i=0; i<2000; i++) {
	           Sgp[0][i] = -WidthSpectrum + WidthSpectrum*i/1000.0;
	           Sgp[1][i] = 0.0;
	       }
	       for (i=0; i<(int)spr_count; i++) {
	           if (Spr[2*i] <= observer_energy_tolerance) continue;
	           for (j=0; j<2000; j++) {
	               double diff = Spr[2*i] - (Sgp[0][j] + Freq[m]);
	               Sgp[1][j] += Spr[2*i+1]/(1.0 + diff*diff/WidthLine);
	           }
	       }
	       if (observer_dc_power > 0.0) {
	           for (j=0; j<2000; j++) {
	               const double observed_frequency = Sgp[0][j] + Freq[m];
	               Sgp[1][j] += observer_dc_power /
	                   (1.0 + observed_frequency*observed_frequency/WidthLine);
	           }
	       }
		for (i=0; i<2000; i++) fprintf(fsi,"%lf %lf\n", Sgp[0][i], Sgp[1][i]);   
              fclose(fsi); 
	     }
	 free(Spr);

          fprintf(stderr, "PROFILE broadening: %.3f s\n",
        ss_wall_seconds() - ss_t_broad);

    char command[256];
          sprintf(command, "./matrix_graph %s %d", Mfile, Bc); // invoke the graphing helper for this field step
          system(command);
          /* In optional group mode, render the invariant result through
             SpinSpec's native C/Gnuplot plotting path as a separate JPEG.
             Legacy data_*.jpg and Res_*.dat remain untouched. */
          {
              const char *groups_mode = getenv("SPINSPEC_OBSERVER_GROUPS");
              if (groups_mode && *groups_mode) {
                  char group_command[192];
                  snprintf(group_command, sizeof(group_command),
                           "./group_graph %d %.12g", Bc, fwhm_hz);
                  /* Pass Graph heights in spin-indexed form, rather than
                   * panel order: Graph 3 0 and Graph 0 3 must both work.
                   * A non-positive height requests automatic Y scaling. */
                  char heights_spec[4096] = {0};
                  size_t heights_len = 0;
                  for (int h = 0; h < Sg; ++h) {
                      int written = snprintf(heights_spec + heights_len,
                              sizeof(heights_spec) - heights_len,
                              "%s%d:%.17g", h ? "," : "", Sgm[h], V_size[h]);
                      if (written < 0 ||
                          (size_t)written >= sizeof(heights_spec) - heights_len) {
                          fprintf(stderr, "Graph heights list is too long.\n");
                          return EXIT_FAILURE;
                      }
                      heights_len += (size_t)written;
                  }
                  if (setenv("SPINSPEC_GRAPH_HEIGHTS", heights_spec, 1) != 0) {
                      perror("setenv SPINSPEC_GRAPH_HEIGHTS");
                      return EXIT_FAILURE;
                  }
                  int group_plot_status = system(group_command);
                  unsetenv("SPINSPEC_GRAPH_HEIGHTS");
                  if (group_plot_status != 0) {
                      fprintf(stderr, "Group spectrum plot failed.\n");
                      return EXIT_FAILURE;
                  }
              }
          }
          
	 printf("Cycle\n");
    B0 = B0 + BS;
    Bc++; 
    }
  free(HG);    
  free(I); 
}