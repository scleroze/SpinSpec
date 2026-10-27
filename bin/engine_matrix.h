#ifndef SVG_GENERATOR_H
#define SVG_GENERATOR_H

/* Write an SVG representation of a rows-by-cols integer matrix.
 * arr: matrix elements; t: generator-specific rendering parameter;
 * filename: destination SVG file.
 */
void generateSVG(int rows, int cols, int arr[rows][cols], int t, const char *filename);

/* Render a frame of calculated transitions and broadened spectra.
 * Sgm contains Sg spin indices (zero-based); B0 is in mT;
 * Bc is the zero-based frame number; V_size holds the y-axis limits.
 */
void GnuGraph(int Sg, int Sgm[], double B0, int Bc, double V_size[]);

#endif // SVG_GENERATOR_H
