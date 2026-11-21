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

	    gsl_eigen_symmv_workspace *workspace = gsl_eigen_symmv_alloc(StatesNumber);
	    gsl_eigen_symmv(Am, eval, evec, workspace);

	    gsl_eigen_symmv_free(workspace);

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
        int total = StatesNumber * (StatesNumber - 1) / 2; // total unordered eigenstate pairs
#pragma omp parallel for private(j, k, l, x1, x2) shared(fsp) reduction(+:observer_ac_power)
for (i = 0; i < StatesNumber - 1; i++) {
    for (j = i + 1; j < StatesNumber; j++) {
        double Dm = 0;
        for (k = 0; k < StatesNumber - 1; k++) {
            for (l = k + 1; l < StatesNumber; l++) {
                if (I[k*StatesNumber + l] != 0) {
                    int spin = I[k*StatesNumber + l] - 1;
                    double weight = G[spin] / (2.0 * SS_OBS_GAMMA_REF_HZ_PER_MT);
                    Dm += weight * (
                        gsl_matrix_get(evec, k, i) * gsl_matrix_get(evec, l, j)
                        + gsl_matrix_get(evec, l, i) * gsl_matrix_get(evec, k, j));


                }
            }
        }
        x1 = fabs(gsl_vector_get(eval, j) - gsl_vector_get(eval, i));
        x2 = Dm * Dm;
        if (x1 > observer_energy_tolerance) observer_ac_power += x2;

        #pragma omp critical
        {
            /*
             * Legacy final output column I[i,j] is indexed by product-basis
             * states, not Hamiltonian eigenstates; it must not be interpreted
             * as a physical assignment of the eigenstate transition.
             */
            fprintf(fsp, "%d %.17g %.17g %d %d %d\n", n, x1, x2, i + 1, j + 1, I[i*StatesNumber + j]);
            n++;
            progress++;

            float percent = (progress * 100.0) / total;
            printf("\rProgress: [%-50s] %.2f%%", 
                   "==================================================" + (50 - (int)(percent / 2)), 
                   percent);
            fflush(stdout);
        }
    }
}
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

/*
 * Read discrete transitions and construct Lorentz-broadened display curves.
 * Each Res file uses the same complete transition list; it is shifted to a
 * spin-specific reference frequency but is NOT a nucleus-selective spectrum.
 */
	   int a, b, c, d;
// Count data rows in Sp_<frame>.dat, excluding the header.
	   if ((fsi = fopen(fname, "r")) == NULL) {
            perror(fname);
            return EXIT_FAILURE;
        }
	   n=0;     
	   while(fscanf(fsi, "%*[^\n]%*c") != EOF) 
	       n++; 
            n=n-1;
	   if (ferror(fsi)) printf("err\n");
	   fscanf(fsi, "%*[^\n]%*c"); 
	   fclose(fsi);
	 	   

           int rows = n;  // 
           double *Spr = malloc(2 * rows * sizeof(double));
           if (!Spr) {
              perror("Memory allocation failed");
              exit(1);
           }

           i = 0;
           fsi = fopen(fname, "r");
           if (fsi == NULL) {
               perror(fname);
               return EXIT_FAILURE;
           }

           char header[256];
           fgets(header, sizeof(header), fsi);

           while (fscanf(fsi, "%d %lf %lf %d %d %d\n", &a, &x1, &x2, &b, &c, &d) != EOF) {
                  Spr[2*i] = x1;
                  Spr[2*i+1] = x2;
                  i++;
           } 
           
           fclose(fsi);  

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
	       for (i=0; i<n; i++) {
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
                  if (system(group_command) != 0) {
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