/*
 * SpinSpec v1.1: Gnuplot rendering of invariant group strengths.
 * Input: Sp_invariant_<frame>.csv produced by experimental observer.
 * Outputs: group_curve_<frame>.dat, group_spectrum_<frame>.jpg.
 * The zero-frequency component is displayed from invariant energy blocks
 * and retained in the CSV. No arbitrary degenerate basis is selected.
 * All spectra are relative transition strengths, NOT FID amplitudes.
 *
 * Usage: ./group_graph 0 0.2
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#define MAX_GROUPS 10
#define MAX_POINTS 2401
#define MAX_LINE 2048
typedef struct {
    double frequency, total, cross;
    double group[MAX_GROUPS];
} Line;

static double lorentzian(double x,double f,double gamma) {
    double z=(x-f)/gamma;
    return 1.0/(1.0+z*z);
}

int main(int argc,char **argv) {
    if(argc<2||argc>4) {
        fprintf(stderr,"usage: %s FRAME [FWHM_HZ] [SPECTRAL_HALF_WIDTH_HZ]\n",argv[0]);
        return 2;
    }
    char *end=NULL;
    long frame=strtol(argv[1],&end,10);
    if(!end||*end||frame<0||frame>99999)return 2;
    double fwhm=argc>2?strtod(argv[2],&end):0.2;
    if(argc>2&&(!end||*end))return 2;
    if(!isfinite(fwhm)||fwhm<=0) {
        fprintf(stderr,"FWHM must be positive\n");
        return 2;
    }
    double requested_half_width=0.0;
    if(argc>3) {
        requested_half_width=strtod(argv[3],&end);
        if(!end||*end||!isfinite(requested_half_width)||requested_half_width<=0) {
            fprintf(stderr,"Spectral half-width must be positive\n");
            return 2;
        }
    }

    char in[128],out[128],curve[128];
    snprintf(in,sizeof(in),"Sp_invariant_%05ld.csv",frame);
    snprintf(out,sizeof(out),"group_spectrum_%05ld.jpg",frame);
    snprintf(curve,sizeof(curve),"group_curve_%05ld.dat",frame);
    FILE *fp=fopen(in,"r");
    if(!fp){perror(in);return 1;}
    char header[MAX_LINE];
    if(!fgets(header,sizeof(header),fp)){fclose(fp);return 1;}
    int groups=0;
    for(char *p=header;(p=strstr(p,"strength_g"))!=NULL;p+=10)groups++;
    if(groups<1||groups>MAX_GROUPS){
        fprintf(stderr,"Invalid number of group columns: %d\n",groups);
        fclose(fp);return 1;
    }
    size_t count=0,capacity=256;
    Line *lines=malloc(capacity*sizeof(*lines));
    if(!lines){fclose(fp);return 1;}
    double max_frequency=0,max_strength=0;
    char text[MAX_LINE];
    while(fgets(text,sizeof(text),fp)) {
        /* Strictly parse the nine fixed columns and all group strengths. */
        double dummy[9+MAX_GROUPS];
        int fields=0;
        char *save=NULL;
        for(char *tok=strtok_r(text,",\r\n",&save);tok;tok=strtok_r(NULL,",\r\n",&save)) {
            if(fields>=9+groups){fields=-999;break;}
            char *tail=NULL;
            dummy[fields]=strtod(tok,&tail);
            if(tail==tok||*tail||!isfinite(dummy[fields])){fields=-999;break;}
            fields++;
        }
        if(fields!=9+groups) {
            fprintf(stderr,"Invalid CSV row in %s\n",in);
            free(lines);fclose(fp);return 1;
        }
        double f=dummy[6],amp=dummy[7];
        if(f<0||amp< -1e-8){free(lines);fclose(fp);return 1;}
        /* Keep invariant DC exactly as supplied by the block projector. */
        if(count==capacity) {
            capacity*=2;
            Line *temp=realloc(lines,capacity*sizeof(*lines));
            if(!temp){free(lines);fclose(fp);return 1;}
            lines=temp;
        }
        Line *l=&lines[count++];
        memset(l,0,sizeof(*l));
        l->frequency=f;
        l->total=amp;
        l->cross=dummy[8];
        for(int g=0;g<groups;g++)l->group[g]=dummy[9+g];
        if(f>max_frequency)max_frequency=f;
        if(amp>max_strength)max_strength=amp;
    }
    fclose(fp);

    /* Display all significantly intense AC transitions; trivial zero-strength
       transitions at large Larmor-related gaps must not enlarge the graph. */
    double max_visible=0,min_visible=HUGE_VAL;
    double cutoff=fmax(1e-12,max_strength*1e-7);
    for(size_t i=0;i<count;i++) {
        if(lines[i].total>cutoff) {
            if(lines[i].frequency>max_visible)max_visible=lines[i].frequency;
            if(lines[i].frequency<min_visible)min_visible=lines[i].frequency;
        }
    }
    if(max_visible<=0) {
        max_visible=fmax(30.,max_frequency);
        min_visible=0;
    }
    /* Include 0 Hz on the frequency axis for zero-field J spectra. */
    double xmax=requested_half_width>0?requested_half_width:
                max_visible*1.06+fmax(2.,2*fwhm);
    double xmin=-xmax;
    if(xmax<=0||!isfinite(xmax)) { free(lines); return 1; }
    FILE *data=fopen(curve,"w");
    if(!data){perror(curve);free(lines);return 1;}
    /* Single data table: freq, total, groups, signed interference. */
    for(int k=0;k<MAX_POINTS;k++) {
        double x=xmin+(xmax-xmin)*k/(MAX_POINTS-1.0);
        double total=0,cross=0,gvals[MAX_GROUPS]={0};
        for(size_t i=0;i<count;i++) {
            const Line *l=&lines[i];
            if(l->frequency<xmin-40*fwhm||l->frequency>xmax+40*fwhm)continue;
            double w=lorentzian(x,l->frequency,fwhm/2);
            total+=w*l->total;cross+=w*l->cross;
            for(int g=0;g<groups;g++)gvals[g]+=w*l->group[g];
        }
        fprintf(data,"%.12g %.12g",x,total);
        for(int g=0;g<groups;g++)fprintf(data," %.12g",gvals[g]);
        fprintf(data," %.12g\n",cross);
    }
    fclose(data);

    /*
     * The original multi-panel presentation: one panel per observer group.
     * Each panel always overlays the SAME total, its own group strength and
     * the signed interference term. DC is included in every curve.
     *
     * For readability, derive independent panel y-limits from AC line
     * strengths. The (often large) DC peak remains in the data and may be
     * clipped visually; no intensity value is renormalized or discarded.
     */
    double ylo[MAX_GROUPS], yhi[MAX_GROUPS];
    static const char *group_color[MAX_GROUPS]={
        "#008d3b", "#246bd2", "#b05d00", "#8a50b9", "#db4444",
        "#00968b", "#9b8629", "#c25292", "#5b7264", "#444444"
    };
    for(int g=0;g<groups;g++) {
        double pos=0.0,neg=0.0,dc_pos=0.0;
        for(size_t k=0;k<count;k++) {
            const Line *l=&lines[k];
            if(l->frequency<=1e-9) {
                dc_pos=fmax(dc_pos,fmax(l->total,l->group[g]));
                continue;
            }
            if(l->frequency>xmax+40*fwhm)continue;
            pos=fmax(pos,fmax(l->total,fmax(l->group[g],l->cross)));
            neg=fmin(neg,l->cross);
        }
        if(pos==0.0)pos=dc_pos;
        yhi[g]=pos*1.20+fmax(0.02,pos*0.04);
        ylo[g]=neg*1.20-fmax(0.02,fabs(neg)*0.04);
        if(yhi[g]<=0.0)yhi[g]=1.0;
    }

    /* The narrow stick plot above the group panels reproduces the former
     * visual arrangement. Stick heights are unbroadened invariant strengths.
     * DC is represented below in the group curves, not in this AC-only strip.
     */
    char sticks[128];
    snprintf(sticks,sizeof(sticks),"group_sticks_%05ld.dat",frame);
    FILE *fpsticks=fopen(sticks,"w");
    if(!fpsticks){perror(sticks);free(lines);return 1;}
    for(size_t k=0;k<count;k++) {
        const Line *l=&lines[k];
        if(l->frequency>1e-9 && l->frequency<=xmax && l->total>1e-14)
            fprintf(fpsticks,"%.17g %.17g\n",l->frequency,l->total);
    }
    fclose(fpsticks);

    FILE *gp=popen("gnuplot","w");
    if(!gp){perror("gnuplot");free(lines);return 1;}
    int height=180+470*groups;
    fprintf(gp,"set terminal jpeg size 1800,%d enhanced font 'Sans,16'\n",height);
    fprintf(gp,"set output '%s'\n",out);
    fprintf(gp,"set multiplot title 'SpinSpec: group-resolved spectrum (including DC)' font 'Sans,20'\n");
    fprintf(gp,"set colorsequence classic\n");
    fprintf(gp,"set grid xtics ytics\n");

    /* Top: compact positive-frequency stick overview. */
    fprintf(gp,"set origin 0,0.855\nset size 1,0.105\n");
    fprintf(gp,"set xrange [0:%.12g]\n",xmax);
    fprintf(gp,"set yrange [0:*]\n");
    fprintf(gp,"unset key\nunset ytics\nunset xlabel\nunset ylabel\n");
    fprintf(gp,"set border 1\nunset grid\n");
    fprintf(gp,"plot '%s' using 1:2 with impulses lw 2 lc rgb 'black' notitle\n",sticks);

    /* Below: one panel for each group with a common frequency axis. */
    double panel_height=0.79/(double)groups;
    for(int g=0;g<groups;g++) {
        double origin_y=0.045+(groups-1-g)*panel_height;
        fprintf(gp,"set origin 0,%.12g\nset size 1,%.12g\n",
                origin_y,panel_height);
        fprintf(gp,"set xrange [%.12g:%.12g]\n",xmin,xmax);
        fprintf(gp,"set yrange [%.12g:%.12g]\n",ylo[g],yhi[g]);
        fprintf(gp,"set border 15\nset ytics\nset xtics\nset grid xtics ytics\n");
        fprintf(gp,"set zeroaxis lc rgb '#777777'\n");
        fprintf(gp,"set key right top horizontal\n");
        fprintf(gp,"set ylabel 'Group %d' font 'Sans,16'\n",g);
        if(g==groups-1)
            fprintf(gp,"set xlabel 'Frequency (Hz)'\n");
        else
            fprintf(gp,"unset xlabel\n");
        fprintf(gp,"plot '%s' using 1:2 with lines lw 2 lc rgb '#444444' title 'Total', ",curve);
        fprintf(gp,"'' using 1:%d with lines lw 2 lc rgb '%s' title 'Group %d', ",
                g+3,group_color[g],g);
        fprintf(gp,"'' using 1:%d with lines lw 2 lc rgb '#00afc8' title 'Interference'\n",
                groups+3);
    }
    fprintf(gp,"unset multiplot\nset output\n");
    int status=pclose(gp);
    free(lines);
    if(status!=0){
        fprintf(stderr,"Gnuplot failed (status=%d)\n",status);
        return 1;
    }
    printf("SpinSpec group graph written: %s (%d panels/groups, invariant DC included)\n",out,groups);
    return 0;
}
