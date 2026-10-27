/* Render one frame of a SpinSpec result using the Gnuplot backend.
 * Sg: number of displayed frequency windows.
 * Sgm: zero-based spin indices selecting their reference frequencies.
 * B0: magnetic field (mT); Bc: zero-based frame number.
 * V_size: upper amplitude limits for the individual display windows.
 */
void GnuGraph(int Sg, int Sgm[], double B0, int Bc, double V_size[]);
