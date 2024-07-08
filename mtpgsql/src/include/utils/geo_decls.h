/*-------------------------------------------------------------------------
 *
 * geo_decls.h - Declarations for various 2D constructs.
 *
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 *
 * NOTE
 *	  These routines do *not* use the float types from adt/.
 *
 *	  XXX These routines were not written by a numerical analyst.
 *
 *	  XXX I have made some attempt to flesh out the operators
 *		and data types. There are still some more to do. - tgl 97/04/19
 *
 *-------------------------------------------------------------------------
 */
#ifndef GEO_DECLS_H
#define GEO_DECLS_H

#include "access/attnum.h"

/*--------------------------------------------------------------------
 * Useful floating point utilities and constants.
 *-------------------------------------------------------------------*/


#define EPSILON					1.0E-06

#ifdef EPSILON
#define FPzero(A)				(fabs(A) <= EPSILON)
#define FPeq(A,B)				(fabs((A) - (B)) <= EPSILON)
#define FPlt(A,B)				((B) - (A) > EPSILON)
#define FPle(A,B)				((A) - (B) <= EPSILON)
#define FPgt(A,B)				((A) - (B) > EPSILON)
#define FPge(A,B)				((B) - (A) <= EPSILON)
#else
#define FPzero(A)				(A == 0)
#define FPnzero(A)				(A != 0)
#define FPeq(A,B)				(A == B)
#define FPne(A,B)				(A != B)
#define FPlt(A,B)				(A < B)
#define FPle(A,B)				(A <= B)
#define FPgt(A,B)				(A > B)
#define FPge(A,B)				(A >= B)
#endif

#define HYPOT(A, B)				sqrt((A) * (A) + (B) * (B))

/*---------------------------------------------------------------------
 * Point - (x,y)
 *-------------------------------------------------------------------*/
typedef struct
{
	double		x,
				y;
} Point;


/*---------------------------------------------------------------------
 * LSEG - A straight line, specified by endpoints.
 *-------------------------------------------------------------------*/
typedef struct
{
	Point		p[2];

	double		m;				/* precomputed to save time, not in tuple */
} LSEG;


/*---------------------------------------------------------------------
 * PATH - Specified by vertex points.
 *-------------------------------------------------------------------*/
typedef struct
{
	int32		size;			/* XXX varlena */
	int32		npts;
	int32		closed;			/* is this a closed polygon? */
	int32		dummy;			/* padding to make it double align */
	Point		p[1];			/* variable length array of POINTs */
} PATH;


/*---------------------------------------------------------------------
 * LINE - Specified by its general equation (Ax+By+C=0).
 *		If there is a y-intercept, it is C, which
 *		 incidentally gives a freebie point on the line
 *		 (if B=0, then C is the x-intercept).
 *		Slope m is precalculated to save time; if
 *		 the line is not vertical, m == A.
 *-------------------------------------------------------------------*/
typedef struct
{
	double		A,
				B,
				C;

	double		m;
} LINE;

typedef struct
{
 	Point loW,higH;
} BOX;
/*---------------------------------------------------------------------
 * POLYGON - Specified by an array of doubles defining the points,
 *		keeping the number of points and the bounding box for
 *		speed purposes.
 *-------------------------------------------------------------------*/
typedef struct
{
	int32		size;			/* XXX varlena */
	int32		npts;
	BOX			boundbox;
	Point		p[1];			/* variable length array of POINTs */
} POLYGON;

/*---------------------------------------------------------------------
 * CIRCLE - Specified by a center point and radius.
 *-------------------------------------------------------------------*/
typedef struct
{
	Point		center;
	double		radius;
} CIRCLE;

/*
 * in geo_ops.h
 */

/* public point routines */
PG_EXTERN Point *point_in(char *str);
PG_EXTERN char *point_out(Point *pt);
PG_EXTERN bool point_left(Point *pt1, Point *pt2);
PG_EXTERN bool point_right(Point *pt1, Point *pt2);
PG_EXTERN bool point_above(Point *pt1, Point *pt2);
PG_EXTERN bool point_below(Point *pt1, Point *pt2);
PG_EXTERN bool point_vert(Point *pt1, Point *pt2);
PG_EXTERN bool point_horiz(Point *pt1, Point *pt2);
PG_EXTERN bool point_eq(Point *pt1, Point *pt2);
PG_EXTERN bool point_ne(Point *pt1, Point *pt2);
PG_EXTERN int32 pointdist(Point *p1, Point *p2);
PG_EXTERN double *point_distance(Point *pt1, Point *pt2);
PG_EXTERN double *point_slope(Point *pt1, Point *pt2);

/* private routines */
PG_EXTERN double point_dt(Point *pt1, Point *pt2);
PG_EXTERN double point_sl(Point *pt1, Point *pt2);

PG_EXTERN Point *point(float8 *x, float8 *y);
PG_EXTERN Point *point_add(Point *p1, Point *p2);
PG_EXTERN Point *point_sub(Point *p1, Point *p2);
PG_EXTERN Point *point_mul(Point *p1, Point *p2);
PG_EXTERN Point *point_div(Point *p1, Point *p2);

/* public lseg routines */
PG_EXTERN LSEG *lseg_in(char *str);
PG_EXTERN char *lseg_out(LSEG *ls);
PG_EXTERN bool lseg_intersect(LSEG *l1, LSEG *l2);
PG_EXTERN bool lseg_parallel(LSEG *l1, LSEG *l2);
PG_EXTERN bool lseg_perp(LSEG *l1, LSEG *l2);
PG_EXTERN bool lseg_vertical(LSEG *lseg);
PG_EXTERN bool lseg_horizontal(LSEG *lseg);
PG_EXTERN bool lseg_eq(LSEG *l1, LSEG *l2);
PG_EXTERN bool lseg_ne(LSEG *l1, LSEG *l2);
PG_EXTERN bool lseg_lt(LSEG *l1, LSEG *l2);
PG_EXTERN bool lseg_le(LSEG *l1, LSEG *l2);
PG_EXTERN bool lseg_gt(LSEG *l1, LSEG *l2);
PG_EXTERN bool lseg_ge(LSEG *l1, LSEG *l2);
PG_EXTERN LSEG *lseg_construct(Point *pt1, Point *pt2);
PG_EXTERN double *lseg_length(LSEG *lseg);
PG_EXTERN double *lseg_distance(LSEG *l1, LSEG *l2);
PG_EXTERN Point *lseg_center(LSEG *lseg);
PG_EXTERN Point *lseg_interpt(LSEG *l1, LSEG *l2);
PG_EXTERN double *dist_pl(Point *pt, LINE *line);
PG_EXTERN double *dist_ps(Point *pt, LSEG *lseg);
PG_EXTERN double *dist_ppath(Point *pt, PATH *path);
PG_EXTERN double *dist_pb(Point *pt, BOX *box);
PG_EXTERN double *dist_sl(LSEG *lseg, LINE *line);
PG_EXTERN double *dist_sb(LSEG *lseg, BOX *box);
PG_EXTERN double *dist_lb(LINE *line, BOX *box);
PG_EXTERN Point *close_lseg(LSEG *l1, LSEG *l2);
PG_EXTERN Point *close_pl(Point *pt, LINE *line);
PG_EXTERN Point *close_ps(Point *pt, LSEG *lseg);
PG_EXTERN Point *close_pb(Point *pt, BOX *box);
PG_EXTERN Point *close_sl(LSEG *lseg, LINE *line);
PG_EXTERN Point *close_sb(LSEG *lseg, BOX *box);
PG_EXTERN Point *close_ls(LINE *line, LSEG *lseg);
PG_EXTERN Point *close_lb(LINE *line, BOX *box);
PG_EXTERN bool on_pl(Point *pt, LINE *line);
PG_EXTERN bool on_ps(Point *pt, LSEG *lseg);
PG_EXTERN bool on_pb(Point *pt, BOX *box);
PG_EXTERN bool on_ppath(Point *pt, PATH *path);
PG_EXTERN bool on_sl(LSEG *lseg, LINE *line);
PG_EXTERN bool on_sb(LSEG *lseg, BOX *box);
PG_EXTERN bool inter_sl(LSEG *lseg, LINE *line);
PG_EXTERN bool inter_sb(LSEG *lseg, BOX *box);
PG_EXTERN bool inter_lb(LINE *line, BOX *box);

/* private lseg routines */

/* public line routines */
PG_EXTERN LINE *line_in(char *str);
PG_EXTERN char *line_out(LINE *line);
PG_EXTERN Point *line_interpt(LINE *l1, LINE *l2);
PG_EXTERN double *line_distance(LINE *l1, LINE *l2);
PG_EXTERN LINE *line_construct_pp(Point *pt1, Point *pt2);
PG_EXTERN bool line_intersect(LINE *l1, LINE *l2);
PG_EXTERN bool line_parallel(LINE *l1, LINE *l2);
PG_EXTERN bool line_perp(LINE *l1, LINE *l2);
PG_EXTERN bool line_vertical(LINE *line);
PG_EXTERN bool line_horizontal(LINE *line);
PG_EXTERN bool line_eq(LINE *l1, LINE *l2);

/* private line routines */

/* public box routines */
PG_EXTERN BOX *rect_in(char *str);
PG_EXTERN char *rect_out(BOX *box);
PG_EXTERN bool rect_same(BOX *box1, BOX *box2);
PG_EXTERN bool rect_overlap(BOX *box1, BOX *box2);
PG_EXTERN bool rect_overleft(BOX *box1, BOX *box2);
PG_EXTERN bool rect_left(BOX *box1, BOX *box2);
PG_EXTERN bool rect_right(BOX *box1, BOX *box2);
PG_EXTERN bool rect_overright(BOX *box1, BOX *box2);
PG_EXTERN bool rect_contained(BOX *box1, BOX *box2);
PG_EXTERN bool rect_contain(BOX *box1, BOX *box2);
PG_EXTERN bool rect_below(BOX *box1, BOX *box2);
PG_EXTERN bool rect_above(BOX *box1, BOX *box2);
PG_EXTERN bool rect_lt(BOX *box1, BOX *box2);
PG_EXTERN bool rect_gt(BOX *box1, BOX *box2);
PG_EXTERN bool rect_eq(BOX *box1, BOX *box2);
PG_EXTERN bool rect_le(BOX *box1, BOX *box2);
PG_EXTERN bool rect_ge(BOX *box1, BOX *box2);
PG_EXTERN Point *rect_center(BOX *box);
PG_EXTERN double *rect_area(BOX *box);
PG_EXTERN double *rect_width(BOX *box);
PG_EXTERN double *rect_height(BOX *box);
PG_EXTERN double *rect_distance(BOX *box1, BOX *box2);
PG_EXTERN Point *rect_center(BOX *box);
PG_EXTERN BOX *rect_intersect(BOX *box1, BOX *box2);
PG_EXTERN LSEG *rect_diagonal(BOX *box);

/* private routines */

PG_EXTERN double rect_dt(BOX *box1, BOX *box2);

PG_EXTERN BOX *rect(Point *p1, Point *p2);
PG_EXTERN BOX *rect_add(BOX *box, Point *p);
PG_EXTERN BOX *rect_sub(BOX *box, Point *p);
PG_EXTERN BOX *rect_mul(BOX *box, Point *p);
PG_EXTERN BOX *rect_div(BOX *box, Point *p);

/* public path routines */
PG_EXTERN PATH *path_in(char *str);
PG_EXTERN char *path_out(PATH *path);
PG_EXTERN bool path_n_lt(PATH *p1, PATH *p2);
PG_EXTERN bool path_n_gt(PATH *p1, PATH *p2);
PG_EXTERN bool path_n_eq(PATH *p1, PATH *p2);
PG_EXTERN bool path_n_le(PATH *p1, PATH *p2);
PG_EXTERN bool path_n_ge(PATH *p1, PATH *p2);
PG_EXTERN bool path_inter(PATH *p1, PATH *p2);
PG_EXTERN double *path_distance(PATH *p1, PATH *p2);
PG_EXTERN double *path_length(PATH *path);

PG_EXTERN bool path_isclosed(PATH *path);
PG_EXTERN bool path_isopen(PATH *path);
PG_EXTERN int4 path_npoints(PATH *path);

PG_EXTERN PATH *path_close(PATH *path);
PG_EXTERN PATH *path_open(PATH *path);
PG_EXTERN PATH *path_add(PATH *p1, PATH *p2);
PG_EXTERN PATH *path_add_pt(PATH *path, Point *point);
PG_EXTERN PATH *path_sub_pt(PATH *path, Point *point);
PG_EXTERN PATH *path_mul_pt(PATH *path, Point *point);
PG_EXTERN PATH *path_div_pt(PATH *path, Point *point);

PG_EXTERN Point *path_center(PATH *path);
PG_EXTERN POLYGON *path_poly(PATH *path);

PG_EXTERN PATH *upgradepath(PATH *path);
PG_EXTERN bool isoldpath(PATH *path);

/* public polygon routines */
PG_EXTERN POLYGON *poly_in(char *s);
PG_EXTERN char *poly_out(POLYGON *poly);
PG_EXTERN bool poly_left(POLYGON *polya, POLYGON *polyb);
PG_EXTERN bool poly_overleft(POLYGON *polya, POLYGON *polyb);
PG_EXTERN bool poly_right(POLYGON *polya, POLYGON *polyb);
PG_EXTERN bool poly_overright(POLYGON *polya, POLYGON *polyb);
PG_EXTERN bool poly_same(POLYGON *polya, POLYGON *polyb);
PG_EXTERN bool poly_overlap(POLYGON *polya, POLYGON *polyb);
PG_EXTERN bool poly_contain(POLYGON *polya, POLYGON *polyb);
PG_EXTERN bool poly_contained(POLYGON *polya, POLYGON *polyb);
PG_EXTERN bool poly_contain_pt(POLYGON *poly, Point *p);
PG_EXTERN bool pt_contained_poly(Point *p, POLYGON *poly);

PG_EXTERN double *poly_distance(POLYGON *polya, POLYGON *polyb);
PG_EXTERN int4 poly_npoints(POLYGON *poly);
PG_EXTERN Point *poly_center(POLYGON *poly);
PG_EXTERN BOX *poly_box(POLYGON *poly);
PG_EXTERN PATH *poly_path(POLYGON *poly);
PG_EXTERN POLYGON *rect_poly(BOX *box);

PG_EXTERN POLYGON *upgradepoly(POLYGON *poly);
PG_EXTERN POLYGON *revertpoly(POLYGON *poly);

/* private polygon routines */

/* public circle routines */
PG_EXTERN CIRCLE *circle_in(char *str);
PG_EXTERN char *circle_out(CIRCLE *circle);
PG_EXTERN bool circle_same(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_overlap(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_overleft(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_left(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_right(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_overright(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_contained(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_contain(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_below(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_above(CIRCLE *circle1, CIRCLE *circle2);

PG_EXTERN bool circle_eq(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_ne(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_lt(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_gt(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_le(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_ge(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN bool circle_contain_pt(CIRCLE *circle, Point *point);
PG_EXTERN bool pt_contained_circle(Point *point, CIRCLE *circle);
PG_EXTERN CIRCLE *circle_add_pt(CIRCLE *circle, Point *point);
PG_EXTERN CIRCLE *circle_sub_pt(CIRCLE *circle, Point *point);
PG_EXTERN CIRCLE *circle_mul_pt(CIRCLE *circle, Point *point);
PG_EXTERN CIRCLE *circle_div_pt(CIRCLE *circle, Point *point);
PG_EXTERN double *circle_diameter(CIRCLE *circle);
PG_EXTERN double *circle_radius(CIRCLE *circle);
PG_EXTERN double *circle_distance(CIRCLE *circle1, CIRCLE *circle2);
PG_EXTERN double *dist_pc(Point *point, CIRCLE *circle);
PG_EXTERN double *dist_cpoly(CIRCLE *circle, POLYGON *poly);
PG_EXTERN Point *circle_center(CIRCLE *circle);
PG_EXTERN CIRCLE *circle(Point *center, float8 *radius);
PG_EXTERN CIRCLE *rect_circle(BOX *box);
PG_EXTERN BOX *circle_box(CIRCLE *circle);
PG_EXTERN CIRCLE *poly_circle(POLYGON *poly);
PG_EXTERN POLYGON *circle_poly(int npts, CIRCLE *circle);

/* private routines */
PG_EXTERN double *circle_area(CIRCLE *circle);
PG_EXTERN double circle_dt(CIRCLE *circle1, CIRCLE *circle2);

/* geo_selfuncs.c */
PG_EXTERN float64 areasel(Oid opid, Oid relid, AttrNumber attno,
		Datum value, int32 flag);
PG_EXTERN float64 areajoinsel(Oid opid, Oid relid1, AttrNumber attno1,
			Oid relid2, AttrNumber attno2);
PG_EXTERN float64 positionsel(Oid opid, Oid relid, AttrNumber attno,
			Datum value, int32 flag);
PG_EXTERN float64 positionjoinsel(Oid opid, Oid relid1, AttrNumber attno1,
				Oid relid2, AttrNumber attno2);
PG_EXTERN float64 contsel(Oid opid, Oid relid, AttrNumber attno,
		Datum value, int32 flag);
PG_EXTERN float64 contjoinsel(Oid opid, Oid relid1, AttrNumber attno1,
			Oid relid2, AttrNumber attno2);

#endif	 /* GEO_DECLS_H */
