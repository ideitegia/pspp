#include <config.h>

#ifndef HAVE_LIBPLOT
#include <libpspp/compiler.h>
#include <output/chart.h>
#include <stdlib.h>

plPlotter * pl_newpl_r (const char *type UNUSED, FILE *infile UNUSED, FILE *outfile UNUSED, FILE *errfile UNUSED, const plPlotterParams *plotter_params UNUSED) { abort (); }
int pl_deletepl_r (plPlotter *plotter UNUSED) { abort (); }

plPlotterParams * pl_newplparams (void UNUSED) { abort (); }
int pl_deleteplparams (plPlotterParams *plotter_params UNUSED) { abort (); }
plPlotterParams * pl_copyplparams (const plPlotterParams *plotter_params UNUSED) { abort (); }

int pl_setplparam (plPlotterParams *plotter_params UNUSED, const char *parameter UNUSED, void *value UNUSED) { abort (); }

int pl_arc_r (plPlotter *plotter UNUSED, int xc UNUSED, int yc UNUSED, int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED) { abort (); }
int pl_box_r (plPlotter *plotter UNUSED, int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED) { abort (); }
int pl_circle_r (plPlotter *plotter UNUSED, int x UNUSED, int y UNUSED, int r UNUSED) { abort (); }
int pl_closepl_r (plPlotter *plotter UNUSED) { abort (); }
int pl_cont_r (plPlotter *plotter UNUSED, int x UNUSED, int y UNUSED) { abort (); }
int pl_erase_r (plPlotter *plotter UNUSED) { abort (); }
int pl_label_r (plPlotter *plotter UNUSED, const char *s UNUSED) { abort (); }
int pl_line_r (plPlotter *plotter UNUSED, int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED) { abort (); }
int pl_linemod_r (plPlotter *plotter UNUSED, const char *s UNUSED) { abort (); }
int pl_move_r (plPlotter *plotter UNUSED, int x UNUSED, int y UNUSED) { abort (); }
int pl_openpl_r (plPlotter *plotter UNUSED) { abort (); }
int pl_point_r (plPlotter *plotter UNUSED, int x UNUSED, int y UNUSED) { abort (); }
int pl_space_r (plPlotter *plotter UNUSED, int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED) { abort (); }

FILE* pl_outfile_r (plPlotter *plotter UNUSED, FILE* outfile UNUSED) { abort (); }/* OBSOLETE */
int pl_alabel_r (plPlotter *plotter UNUSED, int x_justify UNUSED, int y_justify UNUSED, const char *s UNUSED) { abort (); }
int pl_arcrel_r (plPlotter *plotter UNUSED, int dxc UNUSED, int dyc UNUSED, int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED) { abort (); }
int pl_bezier2_r (plPlotter *plotter UNUSED, int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED, int x2 UNUSED, int y2 UNUSED) { abort (); }
int pl_bezier2rel_r (plPlotter *plotter UNUSED, int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED, int dx2 UNUSED, int dy2 UNUSED) { abort (); }
int pl_bezier3_r (plPlotter *plotter UNUSED, int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED, int x2 UNUSED, int y2 UNUSED, int x3 UNUSED, int y3 UNUSED) { abort (); }
int pl_bezier3rel_r (plPlotter *plotter UNUSED, int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED, int dx2 UNUSED, int dy2 UNUSED, int dx3 UNUSED, int dy3 UNUSED) { abort (); }
int pl_bgcolor_r (plPlotter *plotter UNUSED, int red UNUSED, int green UNUSED, int blue UNUSED) { abort (); }
int pl_bgcolorname_r (plPlotter *plotter UNUSED, const char *name UNUSED) { abort (); }
int pl_boxrel_r (plPlotter *plotter UNUSED, int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED) { abort (); }
int pl_capmod_r (plPlotter *plotter UNUSED, const char *s UNUSED) { abort (); }
int pl_circlerel_r (plPlotter *plotter UNUSED, int dx UNUSED, int dy UNUSED, int r UNUSED) { abort (); }
int pl_closepath_r (plPlotter *plotter UNUSED) { abort (); }
int pl_color_r (plPlotter *plotter UNUSED, int red UNUSED, int green UNUSED, int blue UNUSED) { abort (); }
int pl_colorname_r (plPlotter *plotter UNUSED, const char *name UNUSED) { abort (); }
int pl_contrel_r (plPlotter *plotter UNUSED, int x UNUSED, int y UNUSED) { abort (); }
int pl_ellarc_r (plPlotter *plotter UNUSED, int xc UNUSED, int yc UNUSED, int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED) { abort (); }
int pl_ellarcrel_r (plPlotter *plotter UNUSED, int dxc UNUSED, int dyc UNUSED, int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED) { abort (); }
int pl_ellipse_r (plPlotter *plotter UNUSED, int x UNUSED, int y UNUSED, int rx UNUSED, int ry UNUSED, int angle UNUSED) { abort (); }
int pl_ellipserel_r (plPlotter *plotter UNUSED, int dx UNUSED, int dy UNUSED, int rx UNUSED, int ry UNUSED, int angle UNUSED) { abort (); }
int pl_endpath_r (plPlotter *plotter UNUSED) { abort (); }
int pl_endsubpath_r (plPlotter *plotter UNUSED) { abort (); }
int pl_fillcolor_r (plPlotter *plotter UNUSED, int red UNUSED, int green UNUSED, int blue UNUSED) { abort (); }
int pl_fillcolorname_r (plPlotter *plotter UNUSED, const char *name UNUSED) { abort (); }
int pl_fillmod_r (plPlotter *plotter UNUSED, const char *s UNUSED) { abort (); }
int pl_filltype_r (plPlotter *plotter UNUSED, int level UNUSED) { abort (); }
int pl_flushpl_r (plPlotter *plotter UNUSED) { abort (); }
int pl_fontname_r (plPlotter *plotter UNUSED, const char *s UNUSED) { abort (); }
int pl_fontsize_r (plPlotter *plotter UNUSED, int size UNUSED) { abort (); }
int pl_havecap_r (plPlotter *plotter UNUSED, const char *s UNUSED) { abort (); }
int pl_joinmod_r (plPlotter *plotter UNUSED, const char *s UNUSED) { abort (); }
int pl_labelwidth_r (plPlotter *plotter UNUSED, const char *s UNUSED) { abort (); }
int pl_linedash_r (plPlotter *plotter UNUSED, int n UNUSED, const int *dashes UNUSED, int offset UNUSED) { abort (); }
int pl_linerel_r (plPlotter *plotter UNUSED, int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED) { abort (); }
int pl_linewidth_r (plPlotter *plotter UNUSED, int size UNUSED) { abort (); }
int pl_marker_r (plPlotter *plotter UNUSED, int x UNUSED, int y UNUSED, int type UNUSED, int size UNUSED) { abort (); }
int pl_markerrel_r (plPlotter *plotter UNUSED, int dx UNUSED, int dy UNUSED, int type UNUSED, int size UNUSED) { abort (); }
int pl_moverel_r (plPlotter *plotter UNUSED, int x UNUSED, int y UNUSED) { abort (); }
int pl_orientation_r (plPlotter *plotter UNUSED, int direction UNUSED) { abort (); }
int pl_pencolor_r (plPlotter *plotter UNUSED, int red UNUSED, int green UNUSED, int blue UNUSED) { abort (); }
int pl_pencolorname_r (plPlotter *plotter UNUSED, const char *name UNUSED) { abort (); }
int pl_pentype_r (plPlotter *plotter UNUSED, int level UNUSED) { abort (); }
int pl_pointrel_r (plPlotter *plotter UNUSED, int dx UNUSED, int dy UNUSED) { abort (); }
int pl_restorestate_r (plPlotter *plotter UNUSED) { abort (); }
int pl_savestate_r (plPlotter *plotter UNUSED) { abort (); }
int pl_space2_r (plPlotter *plotter UNUSED, int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED, int x2 UNUSED, int y2 UNUSED) { abort (); }
int pl_textangle_r (plPlotter *plotter UNUSED, int angle UNUSED) { abort (); }

double pl_ffontname_r (plPlotter *plotter UNUSED, const char *s UNUSED) { abort (); }
double pl_ffontsize_r (plPlotter *plotter UNUSED, double size UNUSED) { abort (); }
double pl_flabelwidth_r (plPlotter *plotter UNUSED, const char *s UNUSED) { abort (); }
double pl_ftextangle_r (plPlotter *plotter UNUSED, double angle UNUSED) { abort (); }
int pl_farc_r (plPlotter *plotter UNUSED, double xc UNUSED, double yc UNUSED, double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED) { abort (); }
int pl_farcrel_r (plPlotter *plotter UNUSED, double dxc UNUSED, double dyc UNUSED, double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED) { abort (); }
int pl_fbezier2_r (plPlotter *plotter UNUSED, double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED, double x2 UNUSED, double y2 UNUSED) { abort (); }
int pl_fbezier2rel_r (plPlotter *plotter UNUSED, double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED, double dx2 UNUSED, double dy2 UNUSED) { abort (); }
int pl_fbezier3_r (plPlotter *plotter UNUSED, double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED, double x2 UNUSED, double y2 UNUSED, double x3 UNUSED, double y3 UNUSED) { abort (); }
int pl_fbezier3rel_r (plPlotter *plotter UNUSED, double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED, double dx2 UNUSED, double dy2 UNUSED, double dx3 UNUSED, double dy3 UNUSED) { abort (); }
int pl_fbox_r (plPlotter *plotter UNUSED, double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED) { abort (); }
int pl_fboxrel_r (plPlotter *plotter UNUSED, double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED) { abort (); }
int pl_fcircle_r (plPlotter *plotter UNUSED, double x UNUSED, double y UNUSED, double r UNUSED) { abort (); }
int pl_fcirclerel_r (plPlotter *plotter UNUSED, double dx UNUSED, double dy UNUSED, double r UNUSED) { abort (); }
int pl_fcont_r (plPlotter *plotter UNUSED, double x UNUSED, double y UNUSED) { abort (); }
int pl_fcontrel_r (plPlotter *plotter UNUSED, double dx UNUSED, double dy UNUSED) { abort (); }
int pl_fellarc_r (plPlotter *plotter UNUSED, double xc UNUSED, double yc UNUSED, double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED) { abort (); }
int pl_fellarcrel_r (plPlotter *plotter UNUSED, double dxc UNUSED, double dyc UNUSED, double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED) { abort (); }
int pl_fellipse_r (plPlotter *plotter UNUSED, double x UNUSED, double y UNUSED, double rx UNUSED, double ry UNUSED, double angle UNUSED) { abort (); }
int pl_fellipserel_r (plPlotter *plotter UNUSED, double dx UNUSED, double dy UNUSED, double rx UNUSED, double ry UNUSED, double angle UNUSED) { abort (); }
int pl_flinedash_r (plPlotter *plotter UNUSED, int n UNUSED, const double *dashes UNUSED, double offset UNUSED) { abort (); }
int pl_fline_r (plPlotter *plotter UNUSED, double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED) { abort (); }
int pl_flinerel_r (plPlotter *plotter UNUSED, double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED) { abort (); }
int pl_flinewidth_r (plPlotter *plotter UNUSED, double size UNUSED) { abort (); }
int pl_fmarker_r (plPlotter *plotter UNUSED, double x UNUSED, double y UNUSED, int type UNUSED, double size UNUSED) { abort (); }
int pl_fmarkerrel_r (plPlotter *plotter UNUSED, double dx UNUSED, double dy UNUSED, int type UNUSED, double size UNUSED) { abort (); }
int pl_fmove_r (plPlotter *plotter UNUSED, double x UNUSED, double y UNUSED) { abort (); }
int pl_fmoverel_r (plPlotter *plotter UNUSED, double dx UNUSED, double dy UNUSED) { abort (); }
int pl_fpoint_r (plPlotter *plotter UNUSED, double x UNUSED, double y UNUSED) { abort (); }
int pl_fpointrel_r (plPlotter *plotter UNUSED, double dx UNUSED, double dy UNUSED) { abort (); }
int pl_fspace_r (plPlotter *plotter UNUSED, double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED) { abort (); }
int pl_fspace2_r (plPlotter *plotter UNUSED, double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED, double x2 UNUSED, double y2 UNUSED) { abort (); }

int pl_fconcat_r (plPlotter *plotter UNUSED, double m0 UNUSED, double m1 UNUSED, double m2 UNUSED, double m3 UNUSED, double m4 UNUSED, double m5 UNUSED) { abort (); }
int pl_fmiterlimit_r (plPlotter *plotter UNUSED, double limit UNUSED) { abort (); }
int pl_frotate_r (plPlotter *plotter UNUSED, double theta UNUSED) { abort (); }
int pl_fscale_r (plPlotter *plotter UNUSED, double x UNUSED, double y UNUSED) { abort (); }
int pl_fsetmatrix_r (plPlotter *plotter UNUSED, double m0 UNUSED, double m1 UNUSED, double m2 UNUSED, double m3 UNUSED, double m4 UNUSED, double m5 UNUSED) { abort (); }
int pl_ftranslate_r (plPlotter *plotter UNUSED, double x UNUSED, double y UNUSED) { abort (); }

int pl_newpl (const char *type UNUSED, FILE *infile UNUSED, FILE *outfile UNUSED, FILE *errfile UNUSED) { abort (); }
int pl_selectpl (int handle UNUSED) { abort (); }
int pl_deletepl (int handle UNUSED) { abort (); }

int pl_parampl (const char *parameter UNUSED, void *value UNUSED) { abort (); }

int pl_arc (int xc UNUSED, int yc UNUSED, int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED) { abort (); }
int pl_box (int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED) { abort (); }
int pl_circle (int x UNUSED, int y UNUSED, int r UNUSED) { abort (); }
int pl_closepl (void UNUSED) { abort (); }
int pl_cont (int x UNUSED, int y UNUSED) { abort (); }
int pl_erase (void UNUSED) { abort (); }
int pl_label (const char *s UNUSED) { abort (); }
int pl_line (int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED) { abort (); }
int pl_linemod (const char *s UNUSED) { abort (); }
int pl_move (int x UNUSED, int y UNUSED) { abort (); }
int pl_openpl (void UNUSED) { abort (); }
int pl_point (int x UNUSED, int y UNUSED) { abort (); }
int pl_space (int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED) { abort (); }

FILE* pl_outfile (FILE* outfile UNUSED) { abort (); }/* OBSOLETE */
int pl_alabel (int x_justify UNUSED, int y_justify UNUSED, const char *s UNUSED) { abort (); }
int pl_arcrel (int dxc UNUSED, int dyc UNUSED, int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED) { abort (); }
int pl_bezier2 (int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED, int x2 UNUSED, int y2 UNUSED) { abort (); }
int pl_bezier2rel (int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED, int dx2 UNUSED, int dy2 UNUSED) { abort (); }
int pl_bezier3 (int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED, int x2 UNUSED, int y2 UNUSED, int x3 UNUSED, int y3 UNUSED) { abort (); }
int pl_bezier3rel (int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED, int dx2 UNUSED, int dy2 UNUSED, int dx3 UNUSED, int dy3 UNUSED) { abort (); }
int pl_bgcolor (int red UNUSED, int green UNUSED, int blue UNUSED) { abort (); }
int pl_bgcolorname (const char *name UNUSED) { abort (); }
int pl_boxrel (int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED) { abort (); }
int pl_capmod (const char *s UNUSED) { abort (); }
int pl_circlerel (int dx UNUSED, int dy UNUSED, int r UNUSED) { abort (); }
int pl_closepath (void UNUSED) { abort (); }
int pl_color (int red UNUSED, int green UNUSED, int blue UNUSED) { abort (); }
int pl_colorname (const char *name UNUSED) { abort (); }
int pl_contrel (int x UNUSED, int y UNUSED) { abort (); }
int pl_ellarc (int xc UNUSED, int yc UNUSED, int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED) { abort (); }
int pl_ellarcrel (int dxc UNUSED, int dyc UNUSED, int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED) { abort (); }
int pl_ellipse (int x UNUSED, int y UNUSED, int rx UNUSED, int ry UNUSED, int angle UNUSED) { abort (); }
int pl_ellipserel (int dx UNUSED, int dy UNUSED, int rx UNUSED, int ry UNUSED, int angle UNUSED) { abort (); }
int pl_endpath (void UNUSED) { abort (); }
int pl_endsubpath (void UNUSED) { abort (); }
int pl_fillcolor (int red UNUSED, int green UNUSED, int blue UNUSED) { abort (); }
int pl_fillcolorname (const char *name UNUSED) { abort (); }
int pl_fillmod (const char *s UNUSED) { abort (); }
int pl_filltype (int level UNUSED) { abort (); }
int pl_flushpl (void UNUSED) { abort (); }
int pl_fontname (const char *s UNUSED) { abort (); }
int pl_fontsize (int size UNUSED) { abort (); }
int pl_havecap (const char *s UNUSED) { abort (); }
int pl_joinmod (const char *s UNUSED) { abort (); }
int pl_labelwidth (const char *s UNUSED) { abort (); }
int pl_linedash (int n UNUSED, const int *dashes UNUSED, int offset UNUSED) { abort (); }
int pl_linerel (int dx0 UNUSED, int dy0 UNUSED, int dx1 UNUSED, int dy1 UNUSED) { abort (); }
int pl_linewidth (int size UNUSED) { abort (); }
int pl_marker (int x UNUSED, int y UNUSED, int type UNUSED, int size UNUSED) { abort (); }
int pl_markerrel (int dx UNUSED, int dy UNUSED, int type UNUSED, int size UNUSED) { abort (); }
int pl_moverel (int x UNUSED, int y UNUSED) { abort (); }
int pl_orientation (int direction UNUSED) { abort (); }
int pl_pencolor (int red UNUSED, int green UNUSED, int blue UNUSED) { abort (); }
int pl_pencolorname (const char *name UNUSED) { abort (); }
int pl_pentype (int level UNUSED) { abort (); }
int pl_pointrel (int dx UNUSED, int dy UNUSED) { abort (); }
int pl_restorestate (void UNUSED) { abort (); }
int pl_savestate (void UNUSED) { abort (); }
int pl_space2 (int x0 UNUSED, int y0 UNUSED, int x1 UNUSED, int y1 UNUSED, int x2 UNUSED, int y2 UNUSED) { abort (); }
int pl_textangle (int angle UNUSED) { abort (); }

double pl_ffontname (const char *s UNUSED) { abort (); }
double pl_ffontsize (double size UNUSED) { abort (); }
double pl_flabelwidth (const char *s UNUSED) { abort (); }
double pl_ftextangle (double angle UNUSED) { abort (); }
int pl_farc (double xc UNUSED, double yc UNUSED, double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED) { abort (); }
int pl_farcrel (double dxc UNUSED, double dyc UNUSED, double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED) { abort (); }
int pl_fbezier2 (double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED, double x2 UNUSED, double y2 UNUSED) { abort (); }
int pl_fbezier2rel (double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED, double dx2 UNUSED, double dy2 UNUSED) { abort (); }
int pl_fbezier3 (double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED, double x2 UNUSED, double y2 UNUSED, double x3 UNUSED, double y3 UNUSED) { abort (); }
int pl_fbezier3rel (double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED, double dx2 UNUSED, double dy2 UNUSED, double dx3 UNUSED, double dy3 UNUSED) { abort (); }
int pl_fbox (double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED) { abort (); }
int pl_fboxrel (double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED) { abort (); }
int pl_fcircle (double x UNUSED, double y UNUSED, double r UNUSED) { abort (); }
int pl_fcirclerel (double dx UNUSED, double dy UNUSED, double r UNUSED) { abort (); }
int pl_fcont (double x UNUSED, double y UNUSED) { abort (); }
int pl_fcontrel (double dx UNUSED, double dy UNUSED) { abort (); }
int pl_fellarc (double xc UNUSED, double yc UNUSED, double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED) { abort (); }
int pl_fellarcrel (double dxc UNUSED, double dyc UNUSED, double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED) { abort (); }
int pl_fellipse (double x UNUSED, double y UNUSED, double rx UNUSED, double ry UNUSED, double angle UNUSED) { abort (); }
int pl_fellipserel (double dx UNUSED, double dy UNUSED, double rx UNUSED, double ry UNUSED, double angle UNUSED) { abort (); }
int pl_flinedash (int n UNUSED, const double *dashes UNUSED, double offset UNUSED) { abort (); }
int pl_fline (double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED) { abort (); }
int pl_flinerel (double dx0 UNUSED, double dy0 UNUSED, double dx1 UNUSED, double dy1 UNUSED) { abort (); }
int pl_flinewidth (double size UNUSED) { abort (); }
int pl_fmarker (double x UNUSED, double y UNUSED, int type UNUSED, double size UNUSED) { abort (); }
int pl_fmarkerrel (double dx UNUSED, double dy UNUSED, int type UNUSED, double size UNUSED) { abort (); }
int pl_fmove (double x UNUSED, double y UNUSED) { abort (); }
int pl_fmoverel (double dx UNUSED, double dy UNUSED) { abort (); }
int pl_fpoint (double x UNUSED, double y UNUSED) { abort (); }
int pl_fpointrel (double dx UNUSED, double dy UNUSED) { abort (); }
int pl_fspace (double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED) { abort (); }
int pl_fspace2 (double x0 UNUSED, double y0 UNUSED, double x1 UNUSED, double y1 UNUSED, double x2 UNUSED, double y2 UNUSED) { abort (); }

int pl_fconcat (double m0 UNUSED, double m1 UNUSED, double m2 UNUSED, double m3 UNUSED, double m4 UNUSED, double m5 UNUSED) { abort (); }
int pl_fmiterlimit (double limit UNUSED) { abort (); }
int pl_frotate (double theta UNUSED) { abort (); }
int pl_fscale (double x UNUSED, double y UNUSED) { abort (); }
int pl_fsetmatrix (double m0 UNUSED, double m1 UNUSED, double m2 UNUSED, double m3 UNUSED, double m4 UNUSED, double m5 UNUSED) { abort (); }
int pl_ftranslate (double x UNUSED, double y UNUSED) { abort (); }


void *_pl_get_hershey_font_info (plPlotter *plotter UNUSED) { abort (); }
void *_pl_get_ps_font_info (plPlotter *plotter UNUSED) { abort (); }
void *_pl_get_pcl_font_info (plPlotter *plotter UNUSED) { abort (); }
void *_pl_get_stick_font_info (plPlotter *plotter UNUSED) { abort (); }
#endif  /* !HAVE_LIBPLOT */
