/*
 * SpinSpec: spin-1/2 NMR transition frequencies and relative line strengths.
 *
 * The program parses a .set model, constructs the isotropic J-coupled spin
 * Hamiltonian in the product basis, diagonalizes it with GSL, and writes
 * discrete transitions and Lorentz-broadened display spectra.
 *
 * Hamiltonian eigenvalues are expressed in Hz (H/h); magnetic fields in mT.
 * The legacy detection operator sums single-spin flips with equal weights:
 * it does NOT include gyromagnetic weighting or state populations.
 *
 * Build (Linux): gcc engine_matrix.c -o engine_matrix -fopenmp -lgsl -lopenblas -lm
 */

#include <math.h> 
#include <stdio.h>
#include <string.h> 
#include <stdlib.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_eigen.h>
#include "engine_matrix.h"
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

/*
 * Calculate every pair of Hamiltonian eigenstates:
 *
 *   nu_ij = abs(eval[j] - eval[i])
 *   D_ij  = sum_{a<b; I[a,b]!=0}
 *             (V[a,i]*V[b,j] + V[b,i]*V[a,j])
 *   A_ij  = D_ij^2
 *
 * V is evec. This is the legacy equal-weight single-spin-flip operator,
 * proportional to sum_k I_kx; the common 1/2 factor is omitted.
 * Gyromagnetic moments, initial-state populations and receiver response
 * are not included. The resulting line strengths are relative quantities.
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
#pragma omp parallel for private(j, k, l, x1, x2) shared(fsp)
for (i = 0; i < StatesNumber - 1; i++) {
    for (j = i + 1; j < StatesNumber; j++) {
        double Dm = 0;
        for (k = 0; k < StatesNumber - 1; k++) {
            for (l = k + 1; l < StatesNumber; l++) {
                if (I[k*StatesNumber + l] != 0) {
                    Dm += gsl_matrix_get(evec, k, i) * gsl_matrix_get(evec, l, j)
                        + gsl_matrix_get(evec, l, i) * gsl_matrix_get(evec, k, j);


                }
            }
        }
        x1 = fabs(gsl_vector_get(eval, j) - gsl_vector_get(eval, i));
        x2 = Dm * Dm;

        #pragma omp critical
        {
            /*
             * Legacy final output column I[i,j] is indexed by product-basis
             * states, not Hamiltonian eigenstates; it must not be interpreted
             * as a physical assignment of the eigenstate transition.
             */
            fprintf(fsp, "%d %lf %lf %d %d %d\n", n, x1, x2, i + 1, j + 1, I[i*StatesNumber + j]);
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
	double x;

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
	      
	       for (i=0; i<2000; i++) Sgp[1][i]=0; // initialize the intensity grid
	       for (i=0; i<n; i++) {
		   j=0;
		   for (x = -WidthSpectrum; x < WidthSpectrum*0.999; x = x+WidthSpectrum/1000) {
		      if (i==0) Sgp[0][j] = x; // set the frequency grid on the first transition
		      Sgp[1][j] = Sgp[1][j] + Spr[2*i+1]/(1+pow((Spr[2*i]-(x+Freq[m])), 2)/WidthLine); // add this transition's Lorentzian profile
		      j++;
		   }    
		}   
		for (i=0; i<2000; i++) fprintf(fsi,"%lf %lf\n", Sgp[0][i], Sgp[1][i]);   
              fclose(fsi); 
	     }
	 free(Spr);

          char command[256];
          sprintf(command, "./matrix_graph %s %d", Mfile, Bc); // invoke the graphing helper for this field step
          system(command);
          
	 printf("Cycle\n");
    B0 = B0 + BS;
    Bc++; 
    }
  free(HG);    
  free(I); 
}