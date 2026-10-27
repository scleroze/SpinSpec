#include "engine_matrix.h"
#include <stdio.h>
#include <stdlib.h>

double B0, sigma, freq, ampl, Hz=100, Bz=200;
int i;
int Color[] = {3,1,2,4,5,6,7,8,9};

void GnuGraph(int Sg, int Sgm[], double B0, int Bc, double V_size[]) {
  FILE *gp = popen("gnuplot -persist","w"); // Gnuplot process stream
  int H = 1080, W = 1920;
  fprintf(gp, "set terminal jpeg size %i,%i \n", W, H); // JPEG dimensions in pixels


  fprintf(gp, "set output \"data_%05d.jpg\"\n", Bc);
  fprintf(gp, "set key off\n");
  fprintf(gp, "set colorseq classic\n");

/*
 * Render one JPEG frame using Gnuplot multiplot.
 * The upper panel contains all nonzero transitions in Sp_<frame>.dat.
 * Lower panels show Lorentz-broadened Res_<spin>_<frame>.dat files.
 * Each lower panel is a frequency window, not a nucleus-selective signal.
 */
  fprintf(gp, "set multiplot\n");
  fprintf(gp, "set label \"Field strength %.5f mT\" at screen 0.8, 0.95 offset 0, 2 font \",16\"\n", B0);

// Upper panel: discrete stick spectrum.
  char flSp[256];
  sprintf(flSp, "Sp_%05d.dat", Bc);
  FILE *data = fopen(flSp, "r");
  if (!data) {
    perror("Cannot open spectrum data file");
    return;
  }
  char buf[256];
  fgets(buf, sizeof(buf), data); // skip the header
  double max_freq = 0.0; 
  int n1, n2, n3, n4;
  double freq, ampl;
  while (fscanf(data, "%d %lf %lf %d %d %d", &n1, &freq, &ampl, &n2, &n3, &n4) == 6) {
    if (ampl != 0.0 && freq > max_freq) max_freq = freq;
  }
  rewind(data);
  fgets(buf, sizeof(buf), data); // skip the header after rewinding
  fprintf(gp, "set xtics\n");            // enable x-axis ticks

  fprintf(gp, "unset ytics\n");
  fprintf(gp, "set border 1\n");
// Determine the upper panel's frequency-axis tick interval.
  double raw_step = max_freq / 10.0;
  int rounded_step = (int)(raw_step + 0.5);  // round to the nearest integer
  fprintf(gp, "set xtics 0,%i,%lf\n", rounded_step, max_freq * 1.05);
  fprintf(gp, "set xtics nomirror\n");
// Panel positions and dimensions use normalized screen coordinates.
  double H_top = 0.1;         // upper panel height
  double Y_top = 0.87;        // upper panel origin
  fprintf(gp, "set origin 0,%f\n", Y_top);
  fprintf(gp, "set size 1,%f\n", H_top);
  fprintf(gp, "set xrange [0:%lf]\n", max_freq * 1.05);
  fprintf(gp, "plot '-' with lines lc \"black\" lw 2 notitle\n");
  while (fscanf(data, "%d %lf %lf %d %d %d", &n1, &freq, &ampl, &n2, &n3, &n4) == 6) {
    if (ampl != 0.0) {
        fprintf(gp, "%lf %lf\n", freq, 0.0);
        fprintf(gp, "%lf %lf\n\n", freq, ampl);
    }
  }
  fprintf(gp, "e\n");
  fclose(data);
// End of the upper stick-spectrum panel.

  fprintf(gp, "set border 0\n");
  fprintf(gp, "set ytics\n");
  fprintf(gp, "unset xrange\n"); 
  fprintf(gp, "set xtics 2\n");
/*
 * Stack Sg lower panels. Vertical maxima are fixed by V_size[], as read
 * from the .set file. Curves are not independently normalized here.
 */

  double Hs = Y_top/Sg; // height of an individual lower panel
  double H_Sg; // annotation vertical coordinate
  double Shift_H = 0.7; // annotation placement coefficient
  double Sigma_Hz;
  double available = Y_top * 1.0;   // space available for lower panels
  double h = available / Sg;  // height of one lower panel

  for (i=0; i<Sg; i++) {
// Read chemical shift and center frequency from the Res-file header.
   char flf[256];
   sprintf(flf, "Res_%i_%05d.dat", Sgm[i], Bc);
   FILE *fsi = fopen(flf, "r"); // read the current broadened-spectrum file
   if (fsi == NULL) {
     perror("Error opening spectrum file");
     exit(EXIT_FAILURE);
   }
   if (fscanf(fsi, "%lf %lf", &sigma, &freq) != 2) {
     fprintf(stderr, "Error reading parameters from the first spectrum-file line\n");
     fclose(fsi);
     exit(EXIT_FAILURE);
   }
printf("parameters: sigma=%lf, freq=%lf", sigma, freq);
//--------------------------------------------------------------------------
   double origin_y = h * (Sg-i-1);
   fprintf(gp, "set origin 0,%lf\n", origin_y); // position this subplot
   fprintf(gp, "set size 1,%lf\n", h);
   fprintf(gp, "set border 3\n");
   fprintf(gp, "set ytics nomirror\n");
   H_Sg = Hs*(Sg-i-1)+Hs*Shift_H-0.05; // position the chemical-shift annotation
   printf("\n%i Y range %f\n",i , V_size[i]);
   fprintf(gp, "set yrange [0:%lf]\n", V_size[i]);
   fprintf(gp, "set label \"Spin %i\" at screen 0.07, %f offset 0, 0 font \",16\"\n", Sgm[i], H_Sg+0.03);
   fprintf(gp, "set label \"NMR frequency %.5f Hz\" at screen 0.7, %f offset 0, 0 font \",16\"\n", freq, H_Sg);   
   Sigma_Hz = freq*sigma*1e-6;
   fprintf(gp, "set label \"σ = %.2f ppm (%.5lf Hz)\" at screen 0.07, %f offset 0, 0 font \",16\"\n",sigma, Sigma_Hz, H_Sg);
   printf("%i Sgm %i\n", i, Sgm[i]);
   fprintf(gp, "set yrange [0:%lf]\n", V_size[i]);
   fprintf(gp, "plot \"Res_%i_%05d.dat\" skip 1 with l lt %i lw 2\n", Sgm[i], Bc, Color[i]);
   fprintf(gp, "unset label\n");
}
 
fprintf(gp, "unset multiplot\n");
pclose(gp); 
}