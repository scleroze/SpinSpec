/*
 * SpinSpec graphing launcher.
 *
 * Reads the spin-species, field and display options from a .set file, then
 * calls GnuGraph() to render the Sp_<frame>.dat and Res_<spin>_<frame>.dat
 * files produced by engine_matrix. No Hamiltonian is constructed here.
 *
 * Units: magnetic field in mT; gamma/(2*pi) in MHz/T in the .set file,
 * converted to Hz/mT by multiplication by 1000.
 *
 * Build: gcc matrix_graph.c GnuGraph.c -o matrix_graph -lm
 */

#include <math.h> 
#include <stdio.h>
#include <string.h> 
#include <stdlib.h>
#include "matrix_graph.h"
#include <ctype.h>
#include <time.h>
#define MAX_LINE_LENGTH 1024
#define MAX_WORD_LENGTH 256

char line[MAX_LINE_LENGTH];
char Title[MAX_WORD_LENGTH];
    
// Skip leading whitespace in a mutable string.
char *skip_spaces(char *str) {
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

// Read an input record and terminate on EOF or a read error.
int read_line_or_exit(FILE *file, char *line, size_t line_size) {
    if (fgets(line, line_size, file) == NULL) {
        if (feof(file)) {
// End of the input file.
            printf("The file is empty or has ended.\n");
            fclose(file);
            exit(0); // end of file: retain the legacy exit status
        } else {
// An I/O error occurred while reading a line.
            perror("Error reading line.");
            fclose(file);
            exit(1); // terminate with a failure status
        }
    }
    return 1; // record read successfully
}

int main(int argc, char *argv[]) {
    int i,j,Bc;
    int S = 0;
    double TMP;
    const char *ptr = line;

    if (argc != 3) {
        fprintf(stderr,
                "Usage: %s <input.set> <spectrum_number>\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    
// Parse the same .set file used by the spectrum calculation.
    char *Mfile = argv[1]; // path to the input .set file
    Bc = atoi(argv[2]); // zero-based spectrum/frame number
    printf("Processing file %s, spectrum number %d\n", Mfile, Bc); 
    FILE *file = fopen(Mfile, "r");
    if (file == NULL) {
        perror("Error opening input file");
        return 1;
    }

/*
 * Read the optional title. Only the first whitespace-delimited word after
 * a leading '#' is copied into Title; this does not affect spectral data.
 */
        read_line_or_exit(file, line, sizeof(line));


        if (line[0] == '#') {

            char *word_start = skip_spaces(line + 1);


            size_t i = 0;
            while (*word_start && !isspace((unsigned char)*word_start) && i < MAX_WORD_LENGTH - 1) {
                Title[i++] = *word_start++;
            }
            Title[i] = '\0'; // terminate the extracted title



        }
        
/*
 * Read gamma/(2*pi) values for the S nuclei and count the spins.
 * The values in the .set file use MHz/T; G[] stores Hz/mT.
 */
    read_line_or_exit(file, line, sizeof(line));
// Count the real-valued entries on the gyromagnetic-ratio line.

      while (sscanf(ptr, "%lf", &TMP) == 1) {
        S++;
        while (*ptr != ' ' && *ptr != '\0') ptr++; // advance to the next value
        if (*ptr == ' ') ptr++;
      }
      
// Allocate gyromagnetic-ratio coefficients.
      double *G = malloc(S * sizeof(double));
      if (G == NULL) {
        perror("Memory allocation failed");
        return 1;
      }
      
// Fill the coefficient array in Hz/mT.
      ptr = line;
      double Gir;
      for (int i = 0; i < S; i++) {
        sscanf(ptr, "%lf", &Gir);
	G[i] = Gir*pow(10,3);
        while (*ptr != ' ' && *ptr != '\0') ptr++; // advance to the next value
        if (*ptr == ' ') ptr++;
      }
 
// Read the chemical shifts, one per spin, in ppm.
    double *Sigma = malloc(S * sizeof(double));

    read_line_or_exit(file, line, sizeof(line));    


       if (Sigma == NULL) {
           perror("Memory allocation failed");
           return 1;
       }


       ptr = line;
       for (int i = 0; i < S; i++) {
          sscanf(ptr, "%lf", &Sigma[i]);

          while (*ptr != ' ' && *ptr != '\0') ptr++; // advance to the next value
        if (*ptr == ' ') ptr++;
       }
   
/*
 * Parse the magnetic field in mT.
 * For a linear sweep, B01 is the starting field and BS is the field
 * increment in mT per frame. The field displayed for frame Bc is
 * B0 = B01 + Bc*BS. The separately tested logarithmic sweep is not
 * implemented in this GitHub version.
 */
    double B0, B01, B02=0, BS=0;
    read_line_or_exit(file, line, sizeof(line));
      ptr = line;
      if (strchr(line, ' ') != NULL) { // space-separated entries indicate a field sweep
    	sscanf(ptr, "%lf %lf %lf", &B01, &B02, &BS);

      }	
      else { // a single value requests one spectrum
    	sscanf(ptr, "%lf", &B01);
    	B02 = B01; // single-field case
      } 
      if (BS != 0) B0 = B01 + Bc*BS; // reconstruct the field label for this frame
      else B0 = B01;
      printf ("Bc=%d, B0=%lf, B01=%lf, BS%lf \n", Bc, B0, B01, BS);
      
    	        
     const char *stopGraph = "Graph"; // scan forward to the Graph section
     while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, stopGraph, strlen(stopGraph)) == 0) break; // stop at the Graph keyword
     }

/*
 * Parse the Graph line. Its zero-based spin indices select display windows;
 * they do not restrict which spins were used in the Hamiltonian.
 */

    int Sg=0;
    j=0; i=0;
    char *ptr1 = line + 6; // skip the 'Graph ' prefix
    while (sscanf(ptr1, "%d", &j) == 1) { // count the displayed spin indices
        while (*ptr1 != ' ' && *ptr1 != '\0') { ptr1++; } // advance to the next index
        if (*ptr1 == ' ') {
            ptr1++;
        }
        i++;

    }
    Sg = i; // number of requested display windows
    if (Sg <= 0) {
        fprintf(stderr, "Error: no spins specified in Graph line.\n");
        return EXIT_FAILURE;
    } 

// Allocate the list of displayed spin indices.
    int *Sgm = malloc(Sg * sizeof(int));
    if (Sgm == NULL) {
        perror("Memory allocation failed");
        return 1;
    }

// Read and validate each displayed spin index.
    char *ptr2 = line + 6;

    for (int i = 0; i < Sg; i++) {
        sscanf(ptr2, "%d", &Sgm[i]);
        if (Sgm[i] < 0 || Sgm[i] >= S) {
            fprintf(stderr,
                    "Error: spin index out of range in Graph line: %d\n",
                    Sgm[i]);
            return EXIT_FAILURE;
        }

        while (*ptr2 != ' ' && *ptr2 != '\0') ptr2++; // advance to the next index
        if (*ptr2 == ' ') ptr2++;
    }

/*
 * Read the maximum vertical amplitude for each display window.
 * Each value is passed to GnuGraph() as the fixed y-axis upper limit.
 */


    if (fgets(line, sizeof(line), file) == NULL) {
        perror("Error reading input line");
        return 1;
    }

// Count available floating-point values on the amplitude-range line.
    i = 0;
    ptr = line;
    while (sscanf(ptr, "%lf", &TMP) == 1) {
        i++;
        while (*ptr != ' ' && *ptr != '\0') ptr++; // advance to the next value
        if (*ptr == ' ') ptr++;
    }
    
// Allocate the per-window vertical limits.
    double *V_size = malloc(Sg * sizeof(double));
    if (V_size == NULL) {
        perror("Memory allocation failed");
        return 1;
    }

// Read the vertical limits.
    ptr = line;
    for (int i = 0; i < Sg; i++) {
        sscanf(ptr, "%lf", &V_size[i]);
        printf("%i v %f\n", i, V_size[i]);
        while (*ptr != ' ' && *ptr != '\0') ptr++; // advance to the next value
        if (*ptr == ' ') ptr++;
    }

/*
 * Read line broadening (FWHM, Hz) and spectral half-width (Hz) to follow
 * the shared .set-file format; this graphing function does not use them.
 */
double WidthLine, WidthSpectrum;


fscanf(file, "%lf %lf\n", &WidthLine, &WidthSpectrum);

   fclose(file);
// Render the discrete transitions and the selected broadened windows.



    GnuGraph(Sg, Sgm, B0, Bc, V_size);
    printf("\n-> Bc %d    B0 %f   B02 %f\n", Bc, B0, B02);    
    B0 = B0 + BS;
    Bc++; 
}