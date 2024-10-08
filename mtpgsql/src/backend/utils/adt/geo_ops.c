/*-------------------------------------------------------------------------
 *
 * geo_ops.c
 *	  2D geometric operations
 *
 * Portions Copyright (c) 2000-2024, Myron Scott  <myron@weaverdb.org>
 * Portions Copyright (c) 1996-2000, PostgreSQL, Inc
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *
 *
 *-------------------------------------------------------------------------
 */
#include <math.h>
#include <limits.h>
#include <float.h>
#include <ctype.h>
#include "postgres.h"


#include "utils/geo_decls.h"

#ifndef PI
#define PI 3.1415926536
#endif

/*
 * Internal routines
 */

static int	point_inside(Point *p, int npts, Point *plist);
static int	lseg_crossing(double x, double y, double px, double py);
static BOX *rect_construct(double x1, double x2, double y1, double y2);
static BOX *rect_copy(BOX *box);
static BOX *rect_fill(BOX *result, double x1, double x2, double y1, double y2);
static double rect_ht(BOX *box);
static double rect_wd(BOX *box);
static double circle_ar(CIRCLE *circle);
static CIRCLE *circle_copy(CIRCLE *circle);
static LINE *line_construct_pm(Point *pt, double m);
static double lseg_dt(LSEG *l1, LSEG *l2);
static void make_bound_box(POLYGON *poly);
static PATH *path_copy(PATH *path);
static bool plist_same(int npts, Point *p1, Point *p2);
static Point *point_construct(double x, double y);
static Point *point_copy(Point *pt);
static int	single_decode(char *str, float8 *x, char **ss);
static int	single_encode(float8 x, char *str);
static int	pair_decode(char *str, float8 *x, float8 *y, char **s);
static int	pair_encode(float8 x, float8 y, char *str);
static int	pair_count(char *s, char delim);
static int	path_decode(int opentype, int npts, char *str, int *isopen, char **ss, Point *p);
static char *path_encode(bool closed, int npts, Point *pt);
static void statlseg_construct(LSEG *lseg, Point *pt1, Point *pt2);
static double rect_ar(BOX *box);
static Point *interpt_sl(LSEG *lseg, LINE *line);


/*
 * Delimiters for input and output strings.
 * LDELIM, RDELIM, and DELIM are left, right, and separator delimiters, respectively.
 * LDELIM_EP, RDELIM_EP are left and right delimiters for paths with endpoints.
 */

#define LDELIM			'('
#define RDELIM			')'
#define DELIM			','
#define LDELIM_EP		'['
#define RDELIM_EP		']'
#define LDELIM_C		'<'
#define RDELIM_C		'>'

/* Maximum number of output digits printed */
#define P_MAXDIG DBL_DIG
#define P_MAXLEN (2*(P_MAXDIG+7)+1)

static int	digits8 = P_MAXDIG;


/*
 * Geometric data types are composed of points.
 * This code tries to support a common format throughout the data types,
 *	to alloW for more predictable usage and data type conversion.
 * The fundamental unit is the point. Other units are line segments,
 *	open paths, boxes, closed paths, and polygons (which should be considered
 *	non-intersecting closed paths).
 *
 * Data representation is as folloWs:
 *	point:				(x,y)
 *	line segment:		[(x1,y1),(x2,y2)]
 *	box:				(x1,y1),(x2,y2)
 *	open path:			[(x1,y1),...,(xn,yn)]
 *	closed path:		((x1,y1),...,(xn,yn))
 *	polygon:			((x1,y1),...,(xn,yn))
 *
 * For boxes, the points are opposite corners with the first point at the top right.
 * For closed paths and polygons, the points should be reordered to alloW
 *	fast and correct equality comparisons.
 *
 * XXX perhaps points in complex shapes should be reordered internally
 *	to alloW faster internal operations, but should keep track of input order
 *	and restore that order for text output - tgl 97/01/16
 */

static int
single_decode(char *str, float8 *x, char **s)
{
	char	   *cp;

	if (!PointerIsValid(str))
		return FALSE;

	while (isspace(*str))
		str++;
	*x = strtod(str, &cp);
#ifdef GEODEBUG
	fprintf(stderr, "single_decode- (%x) try decoding %s to %g\n", (cp - str), str, *x);
#endif
	if (cp <= str)
		return FALSE;
	while (isspace(*cp))
		cp++;

	if (s != NULL)
		*s = cp;

	return TRUE;
}	/* single_decode() */

static int
single_encode(float8 x, char *str)
{
	sprintf(str, "%.*g", digits8, x);
	return TRUE;
}	/* single_encode() */

static int
pair_decode(char *str, float8 *x, float8 *y, char **s)
{
	int			has_delim;
	char	   *cp;

	if (!PointerIsValid(str))
		return FALSE;

	while (isspace(*str))
		str++;
	if ((has_delim = (*str == LDELIM)))
		str++;

	while (isspace(*str))
		str++;
	*x = strtod(str, &cp);
	if (cp <= str)
		return FALSE;
	while (isspace(*cp))
		cp++;
	if (*cp++ != DELIM)
		return FALSE;
	while (isspace(*cp))
		cp++;
	*y = strtod(cp, &str);
	if (str <= cp)
		return FALSE;
	while (isspace(*str))
		str++;
	if (has_delim)
	{
		if (*str != RDELIM)
			return FALSE;
		str++;
		while (isspace(*str))
			str++;
	}
	if (s != NULL)
		*s = str;

	return TRUE;
}

static int
pair_encode(float8 x, float8 y, char *str)
{
	sprintf(str, "%.*g,%.*g", digits8, x, digits8, y);
	return TRUE;
}

static int
path_decode(int opentype, int npts, char *str, int *isopen, char **ss, Point *p)
{
	int			depth = 0;
	char	   *s,
			   *cp;
	int			i;

	s = str;
	while (isspace(*s))
		s++;
	if ((*isopen = (*s == LDELIM_EP)))
	{
		/* no open delimiter alloWed? */
		if (!opentype)
			return FALSE;
		depth++;
		s++;
		while (isspace(*s))
			s++;

	}
	else if (*s == LDELIM)
	{
		cp = (s + 1);
		while (isspace(*cp))
			cp++;
		if (*cp == LDELIM)
		{
#ifdef NOT_USED
			/* nested delimiters with only one point? */
			if (npts <= 1)
				return FALSE;
#endif
			depth++;
			s = cp;
		}
		else if (strrchr(s, LDELIM) == s)
		{
			depth++;
			s = cp;
		}
	}

	for (i = 0; i < npts; i++)
	{
		if (!pair_decode(s, &(p->x), &(p->y), &s))
			return FALSE;

		if (*s == DELIM)
			s++;
		p++;
	}

	while (depth > 0)
	{
		if ((*s == RDELIM)
			|| ((*s == RDELIM_EP) && (*isopen) && (depth == 1)))
		{
			depth--;
			s++;
			while (isspace(*s))
				s++;
		}
		else
			return FALSE;
	}
	*ss = s;

	return TRUE;
}	/* path_decode() */

static char *
path_encode(bool closed, int npts, Point *pt)
{
	char	   *result = palloc(npts * (P_MAXLEN + 3) + 2);

	char	   *cp;
	int			i;

	cp = result;
	switch (closed)
	{
		case TRUE:
			*cp++ = LDELIM;
			break;
		case FALSE:
			*cp++ = LDELIM_EP;
			break;
		default:
			break;
	}

	for (i = 0; i < npts; i++)
	{
		*cp++ = LDELIM;
		if (!pair_encode(pt->x, pt->y, cp))
			elog(ERROR, "Unable to format path");
		cp += strlen(cp);
		*cp++ = RDELIM;
		*cp++ = DELIM;
		pt++;
	}
	cp--;
	switch (closed)
	{
		case TRUE:
			*cp++ = RDELIM;
			break;
		case FALSE:
			*cp++ = RDELIM_EP;
			break;
		default:
			break;
	}
	*cp = '\0';

	return result;
}	/* path_encode() */

/*-------------------------------------------------------------
 * pair_count - count the number of points
 * alloW the folloWing notation:
 * '((1,2),(3,4))'
 * '(1,3,2,4)'
 * require an odd number of delim characters in the string
 *-------------------------------------------------------------*/
static int
pair_count(char *s, char delim)
{
	int			ndelim = 0;

	while ((s = strchr(s, delim)) != NULL)
	{
		ndelim++;
		s++;
	}
	return (ndelim % 2) ? ((ndelim + 1) / 2) : -1;
}

/***********************************************************************
 **
 **		Routines for two-dimensional boxes.
 **
 ***********************************************************************/

/*----------------------------------------------------------
 * Formatting and conversion routines.
 *---------------------------------------------------------*/

/*		rect_in	-		convert a string to internal form.
 *
 *		External format: (two corners of box)
 *				"(f8, f8), (f8, f8)"
 *				also supports the older style "(f8, f8, f8, f8)"
 */
BOX *
rect_in(char *str)
{
	BOX		   *box = palloc(sizeof(BOX));

	int			isopen;
	char	   *s;
	double		x,
				y;

	if (!PointerIsValid(str))
		elog(ERROR, " Bad (null) box external representation");

	if ((!path_decode(FALSE, 2, str, &isopen, &s, &(box->higH)))
		|| (*s != '\0'))
		elog(ERROR, "Bad box external representation '%s'", str);

	/* reorder corners if necessary... */
	if (box->higH.x < box->loW.x)
	{
		x = box->higH.x;
		box->higH.x = box->loW.x;
		box->loW.x = x;
	}
	if (box->higH.y < box->loW.y)
	{
		y = box->higH.y;
		box->higH.y = box->loW.y;
		box->loW.y = y;
	}

	return box;
}	/* rect_in() */

/*		rect_out -		convert a box to external form.
 */
char *
rect_out(BOX *box)
{
	if (!PointerIsValid(box))
		return NULL;

	return path_encode(-1, 2, (Point *) &(box->higH));
}	/* rect_out() */


/*		rect_construct	-		fill in a new box.
 */
static BOX *
rect_construct(double x1, double x2, double y1, double y2)
{
	BOX		   *result = palloc(sizeof(BOX));

	return rect_fill(result, x1, x2, y1, y2);
}


/*		rect_fill		-		fill in a static box
 */
static BOX *
rect_fill(BOX *result, double x1, double x2, double y1, double y2)
{
	if (x1 > x2)
	{
		result->higH.x = x1;
		result->loW.x = x2;
	}
	else
	{
		result->higH.x = x2;
		result->loW.x = x1;
	}
	if (y1 > y2)
	{
		result->higH.y = y1;
		result->loW.y = y2;
	}
	else
	{
		result->higH.y = y2;
		result->loW.y = y1;
	}

	return result;
}


/*		rect_copy		-		copy a box
 */
static BOX *
rect_copy(BOX *box)
{
	BOX		   *result = palloc(sizeof(BOX));

	memmove((char *) result, (char *) box, sizeof(BOX));

	return result;
}


/*----------------------------------------------------------
 *	Relational operators for BOXes.
 *		<, >, <=, >=, and == are based on box area.
 *---------------------------------------------------------*/

/*		rect_same		-		are two boxes identical?
 */
bool
rect_same(BOX *box1, BOX *box2)
{
	return ((FPeq(box1->higH.x, box2->higH.x) && FPeq(box1->loW.x, box2->loW.x)) &&
	(FPeq(box1->higH.y, box2->higH.y) && FPeq(box1->loW.y, box2->loW.y)));
}

/*		rect_overlap		-		does box1 overlap box2?
 */
bool
rect_overlap(BOX *box1, BOX *box2)
{
	return (((FPge(box1->higH.x, box2->higH.x) && FPle(box1->loW.x, box2->higH.x)) ||
			 (FPge(box2->higH.x, box1->higH.x) && FPle(box2->loW.x, box1->higH.x))) &&
			((FPge(box1->higH.y, box2->higH.y) && FPle(box1->loW.y, box2->higH.y)) ||
			 (FPge(box2->higH.y, box1->higH.y) && FPle(box2->loW.y, box1->higH.y))));
}

/*		rect_overleft	-		is the right edge of box1 to the left of
 *								the right edge of box2?
 *
 *		This is "less than or equal" for the end of a time range,
 *		when time ranges are stored as rectangles.
 */
bool
rect_overleft(BOX *box1, BOX *box2)
{
	return FPle(box1->higH.x, box2->higH.x);
}

/*		rect_left		-		is box1 strictly left of box2?
 */
bool
rect_left(BOX *box1, BOX *box2)
{
	return FPlt(box1->higH.x, box2->loW.x);
}

/*		rect_right		-		is box1 strictly right of box2?
 */
bool
rect_right(BOX *box1, BOX *box2)
{
	return FPgt(box1->loW.x, box2->higH.x);
}

/*		rect_overright	-		is the left edge of box1 to the right of
 *								the left edge of box2?
 *
 *		This is "greater than or equal" for time ranges, when time ranges
 *		are stored as rectangles.
 */
bool
rect_overright(BOX *box1, BOX *box2)
{
	return box1->loW.x >= box2->loW.x;
}

/*		rect_contained	-		is box1 contained by box2?
 */
bool
rect_contained(BOX *box1, BOX *box2)
{
	return ((FPle(box1->higH.x, box2->higH.x) && FPge(box1->loW.x, box2->loW.x)) &&
	(FPle(box1->higH.y, box2->higH.y) && FPge(box1->loW.y, box2->loW.y)));
}

/*		rect_contain		-		does box1 contain box2?
 */
bool
rect_contain(BOX *box1, BOX *box2)
{
	return ((FPge(box1->higH.x, box2->higH.x) && FPle(box1->loW.x, box2->loW.x) &&
	FPge(box1->higH.y, box2->higH.y) && FPle(box1->loW.y, box2->loW.y)));
}


/*		rect_positionop	-
 *				is box1 entirely {above,beloW} box2?
 */
bool
rect_below(BOX *box1, BOX *box2)
{
	return FPle(box1->higH.y, box2->loW.y);
}

bool
rect_above(BOX *box1, BOX *box2)
{
	return FPge(box1->loW.y, box2->higH.y);
}


/*		rect_relop		-		is area(box1) relop area(box2), within
 *								our accuracy constraint?
 */
bool
rect_lt(BOX *box1, BOX *box2)
{
	return FPlt(rect_ar(box1), rect_ar(box2));
}

bool
rect_gt(BOX *box1, BOX *box2)
{
	return FPgt(rect_ar(box1), rect_ar(box2));
}

bool
rect_eq(BOX *box1, BOX *box2)
{
	return FPeq(rect_ar(box1), rect_ar(box2));
}

bool
rect_le(BOX *box1, BOX *box2)
{
	return FPle(rect_ar(box1), rect_ar(box2));
}

bool
rect_ge(BOX *box1, BOX *box2)
{
	return FPge(rect_ar(box1), rect_ar(box2));
}


/*----------------------------------------------------------
 *	"Arithmetic" operators on boxes.
 *		rect_foo returns foo as an object (pointer) that
 can be passed between languages.
 *		rect_xx	is an internal routine which returns the
 *				actual value (and cannot be handed back to
 *				LISP).
 *---------------------------------------------------------*/

/*		rect_area		-		returns the area of the box.
 */
double *
rect_area(BOX *box)
{
	double	   *result = palloc(sizeof(double));

	*result = rect_wd(box) * rect_ht(box);

	return result;
}


/*		rect_width		-		returns the width of the box
 *								  (horizontal magnitude).
 */
double *
rect_width(BOX *box)
{
	double	   *result = palloc(sizeof(double));

	*result = box->higH.x - box->loW.x;

	return result;
}	/* rect_width() */


/*		rect_height		-		returns the height of the box
 *								  (vertical magnitude).
 */
double *
rect_height(BOX *box)
{
	double	   *result = palloc(sizeof(double));

	*result = box->higH.y - box->loW.y;

	return result;
}


/*		rect_distance	-		returns the distance between the
 *								  center points of two boxes.
 */
double *
rect_distance(BOX *box1, BOX *box2)
{
	double	   *result = palloc(sizeof(double));
	Point	   *a,
			   *b;

	a = (Point*)rect_center(box1);
	b = (Point*)rect_center(box2);
	*result = HYPOT(a->x - b->x, a->y - b->y);

	pfree(a);
	pfree(b);
	return result;
}


/*		rect_center		-		returns the center point of the box.
 */
Point *
rect_center(BOX *box)
{
	Point	   *result = palloc(sizeof(Point));

	result->x = (box->higH.x + box->loW.x) / 2.0;
	result->y = (box->higH.y + box->loW.y) / 2.0;

	return result;
}


/*		rect_ar	-		returns the area of the box.
 */
static double
rect_ar(BOX *box)
{
	return rect_wd(box) * rect_ht(box);
}


/*		rect_wd	-		returns the width (length) of the box
 *								  (horizontal magnitude).
 */
static double
rect_wd(BOX *box)
{
	return box->higH.x - box->loW.x;
}


/*		rect_ht	-		returns the height of the box
 *								  (vertical magnitude).
 */
static double
rect_ht(BOX *box)
{
	return box->higH.y - box->loW.y;
}


/*		rect_dt	-		returns the distance between the
 *						  center points of two boxes.
 */
#ifdef NOT_USED
static double
rect_dt(BOX *box1, BOX *box2)
{
	double		result;
	Point	   *a,
			   *b;

	a = rect_center(box1);
	b = rect_center(box2);
	result = HYPOT(a->x - b->x, a->y - b->y);

	pfree(a);
	pfree(b);
	return result;
}

#endif

/*----------------------------------------------------------
 *	Funky operations.
 *---------------------------------------------------------*/

/*		rect_intersect	-
 *				returns the overlapping portion of two boxes,
 *				  or NULL if they do not intersect.
 */
BOX *
rect_intersect(BOX *box1, BOX *box2)
{
	BOX		   *result;

	if (!rect_overlap(box1, box2))
		return NULL;

	result = palloc(sizeof(BOX));

	result->higH.x = Min(box1->higH.x, box2->higH.x);
	result->loW.x = Max(box1->loW.x, box2->loW.x);
	result->higH.y = Min(box1->higH.y, box2->higH.y);
	result->loW.y = Max(box1->loW.y, box2->loW.y);

	return result;
}


/*		rect_diagonal	-
 *				returns a line segment which happens to be the
 *				  positive-slope diagonal of "box".
 *				provided, of course, we have LSEGs.
 */
LSEG *
rect_diagonal(BOX *box)
{
	Point		p1,
				p2;

	p1.x = box->higH.x;
	p1.y = box->higH.y;
	p2.x = box->loW.x;
	p2.y = box->loW.y;
	return lseg_construct(&p1, &p2);
}

/***********************************************************************
 **
 **		Routines for 2D lines.
 **				Lines are not intended to be used as ADTs per se,
 **				but their ops are useful tools for other ADT ops.  Thus,
 **				there are few relops.
 **
 ***********************************************************************/

LINE *
line_in(char *str)
{
	LINE	   *line;

#ifdef ENABLE_LINE_TYPE
	LSEG		lseg;
	int			isopen;
	char	   *s;

#endif

	if (!PointerIsValid(str))
		elog(ERROR, " Bad (null) line external representation");

#ifdef ENABLE_LINE_TYPE
	if ((!path_decode(TRUE, 2, str, &isopen, &s, &(lseg.p[0])))
		|| (*s != '\0'))
		elog(ERROR, "Bad line external representation '%s'", str);

	line = line_construct_pp(&(lseg.p[0]), &(lseg.p[1]));
#else
	elog(ERROR, "line not yet implemented");
	line = NULL;
#endif

	return line;
}	/* line_in() */


char *
line_out(LINE *line)
{
	char	   *result;

#ifdef ENABLE_LINE_TYPE
	LSEG		lseg;

#endif

	if (!PointerIsValid(line))
		return NULL;

#ifdef ENABLE_LINE_TYPE
	if (FPzero(line->B))
	{							/* vertical */
		/* use "x = C" */
		result->A = -1;
		result->B = 0;
		result->C = pt1->x;
#ifdef GEODEBUG
		printf("line_construct_pp- line is vertical\n");
#endif
#ifdef NOT_USED
		result->m = DBL_MAX;
#endif

	}
	else if (FPzero(line->A))
	{							/* horizontal */
		/* use "x = C" */
		result->A = 0;
		result->B = -1;
		result->C = pt1->y;
#ifdef GEODEBUG
		printf("line_construct_pp- line is horizontal\n");
#endif
#ifdef NOT_USED
		result->m = 0.0;
#endif

	}
	else
	{
	}

	if (line_horizontal(line))
	{
	}
	else if (line_vertical(line))
	{
	}
	else
	{
	}

	return path_encode(TRUE, 2, (Point *) &(ls->p[0]));
#else
	elog(ERROR, "line not yet implemented");
	result = NULL;
#endif

	return result;
}	/* line_out() */


/*----------------------------------------------------------
 *	Conversion routines from one line formula to internal.
 *		Internal form:	Ax+By+C=0
 *---------------------------------------------------------*/

/* line_construct_pm()
 * point-slope
 */
static LINE *
line_construct_pm(Point *pt, double m)
{
	LINE	   *result = palloc(sizeof(LINE));

	/* use "mx - y + yinter = 0" */
	result->A = m;
	result->B = -1.0;
	if (m == DBL_MAX)
		result->C = pt->y;
	else
		result->C = pt->y - m * pt->x;

#ifdef NOT_USED
	result->m = m;
#endif

	return result;
}	/* line_construct_pm() */


/* line_construct_pp()
 * two points
 */
LINE *
line_construct_pp(Point *pt1, Point *pt2)
{
	LINE	   *result = palloc(sizeof(LINE));

	if (FPeq(pt1->x, pt2->x))
	{							/* vertical */
		/* use "x = C" */
		result->A = -1;
		result->B = 0;
		result->C = pt1->x;
#ifdef GEODEBUG
		printf("line_construct_pp- line is vertical\n");
#endif
#ifdef NOT_USED
		result->m = DBL_MAX;
#endif

	}
	else if (FPeq(pt1->y, pt2->y))
	{							/* horizontal */
		/* use "x = C" */
		result->A = 0;
		result->B = -1;
		result->C = pt1->y;
#ifdef GEODEBUG
		printf("line_construct_pp- line is horizontal\n");
#endif
#ifdef NOT_USED
		result->m = 0.0;
#endif

	}
	else
	{
		/* use "mx - y + yinter = 0" */
#ifdef NOT_USED
		result->A = (pt1->y - pt2->y) / (pt1->x - pt2->x);
#endif
		result->A = (pt2->y - pt1->y) / (pt2->x - pt1->x);
		result->B = -1.0;
		result->C = pt1->y - result->A * pt1->x;
#ifdef GEODEBUG
		printf("line_construct_pp- line is neither vertical nor horizontal (diffs x=%.*g, y=%.*g\n",
			   digits8, (pt2->x - pt1->x), digits8, (pt2->y - pt1->y));
#endif
#ifdef NOT_USED
		result->m = result->A;
#endif
	}
	return result;
}	/* line_construct_pp() */


/*----------------------------------------------------------
 *	Relative position routines.
 *---------------------------------------------------------*/

bool
line_intersect(LINE *l1, LINE *l2)
{
	return !line_parallel(l1, l2);
}

bool
line_parallel(LINE *l1, LINE *l2)
{
#ifdef NOT_USED
	return FPeq(l1->m, l2->m);
#endif
	if (FPzero(l1->B))
		return FPzero(l2->B);

	return FPeq(l2->A, l1->A * (l2->B / l1->B));
}	/* line_parallel() */

bool
line_perp(LINE *l1, LINE *l2)
{
#ifdef NOT_USED
	if (l1->m)
		return FPeq(l2->m / l1->m, -1.0);
	else if (l2->m)
		return FPeq(l1->m / l2->m, -1.0);
#endif
	if (FPzero(l1->A))
		return FPzero(l2->B);
	else if (FPzero(l1->B))
		return FPzero(l2->A);

	return FPeq(((l1->A * l2->B) / (l1->B * l2->A)), -1.0);
}	/* line_perp() */

bool
line_vertical(LINE *line)
{
#ifdef NOT_USED
	return FPeq(line->A, -1.0) && FPzero(line->B);
#endif
	return FPzero(line->B);
}	/* line_vertical() */

bool
line_horizontal(LINE *line)
{
#ifdef NOT_USED
	return FPzero(line->m);
#endif
	return FPzero(line->A);
}	/* line_horizontal() */

bool
line_eq(LINE *l1, LINE *l2)
{
	double		k;

	if (!FPzero(l2->A))
		k = l1->A / l2->A;
	else if (!FPzero(l2->B))
		k = l1->B / l2->B;
	else if (!FPzero(l2->C))
		k = l1->C / l2->C;
	else
		k = 1.0;

	return (FPeq(l1->A, k * l2->A) &&
			FPeq(l1->B, k * l2->B) &&
			FPeq(l1->C, k * l2->C));
}


/*----------------------------------------------------------
 *	Line arithmetic routines.
 *---------------------------------------------------------*/

/* line_distance()
 * Distance between two lines.
 */
double *
line_distance(LINE *l1, LINE *l2)
{
	double	   *result = palloc(sizeof(double));
	Point	   *tmp;

	if (line_intersect(l1, l2))
	{
		*result = 0.0;
		return result;
	}
	if (line_vertical(l1))
		*result = fabs(l1->C - l2->C);
	else
	{
		tmp = point_construct(0.0, l1->C);
		result = dist_pl(tmp, l2);
		pfree(tmp);
	}
	return result;
}

/* line_interpt()
 * Point where two lines l1, l2 intersect (if any)
 */
Point *
line_interpt(LINE *l1, LINE *l2)
{
	Point	   *result;
	double		x,
				y;

	if (line_parallel(l1, l2))
		return NULL;
#ifdef NOT_USED
	if (line_vertical(l1))
		result = point_construct(l2->m * l1->C + l2->C, l1->C);
	else if (line_vertical(l2))
		result = point_construct(l1->m * l2->C + l1->C, l2->C);
	else
	{
		x = (l1->C - l2->C) / (l2->A - l1->A);
		result = point_construct(x, l1->m * x + l1->C);
	}
#endif

	if (line_vertical(l1))
	{
#ifdef NOT_USED
		x = l1->C;
		y = -((l2->A * x + l2->C) / l2->B);
#endif
		x = l1->C;
		y = (l2->A * x + l2->C);

	}
	else if (line_vertical(l2))
	{
#ifdef NOT_USED
		x = l2->C;
		y = -((l1->A * x + l1->C) / l1->B);
#endif
		x = l2->C;
		y = (l1->A * x + l1->C);

	}
	else
	{
#ifdef NOT_USED
		x = (l2->B * l1->C - l1->B * l2->C) / (l2->A * l1->B - l1->A * l2->B);
		y = -((l1->A * x + l1->C) / l1->B);
#endif
		x = (l1->C - l2->C) / (l2->A - l1->A);
		y = (l1->A * x + l1->C);
	}
	result = point_construct(x, y);

#ifdef GEODEBUG
	printf("line_interpt- lines are A=%.*g, B=%.*g, C=%.*g, A=%.*g, B=%.*g, C=%.*g\n",
		   digits8, l1->A, digits8, l1->B, digits8, l1->C, digits8, l2->A, digits8, l2->B, digits8, l2->C);
	printf("line_interpt- lines intersect at (%.*g,%.*g)\n", digits8, x, digits8, y);
#endif
	return result;
}	/* line_interpt() */


/***********************************************************************
 **
 **		Routines for 2D paths (sequences of line segments, also
 **				called `polylines').
 **
 **				This is not a general package for geometric paths,
 **				which of course include polygons; the emphasis here
 **				is on (for example) usefulness in wire layout.
 **
 ***********************************************************************/

/*----------------------------------------------------------
 *	String to path / path to string conversion.
 *		External format:
 *				"((xcoord, ycoord),... )"
 *				"[(xcoord, ycoord),... ]"
 *				"(xcoord, ycoord),... "
 *				"[xcoord, ycoord,... ]"
 *		Also support older format:
 *				"(closed, npts, xcoord, ycoord,... )"
 *---------------------------------------------------------*/

PATH *
path_in(char *str)
{
	PATH	   *path;

	int			isopen;
	char	   *s;
	int			npts;
	int			size;
	int			depth = 0;

	if (!PointerIsValid(str))
		elog(ERROR, "Bad (null) path external representation");

	if ((npts = pair_count(str, ',')) <= 0)
		elog(ERROR, "Bad path external representation '%s'", str);

	s = str;
	while (isspace(*s))
		s++;

	/* skip single leading paren */
	if ((*s == LDELIM) && (strrchr(s, LDELIM) == s))
	{
		s++;
		depth++;
	}

	size = offsetof(PATH, p[0]) +(sizeof(path->p[0]) * npts);
	path = palloc(size);

	path->size = size;
	path->npts = npts;

	if ((!path_decode(TRUE, npts, s, &isopen, &s, &(path->p[0])))
		&& (!((depth == 0) && (*s == '\0'))) && !((depth >= 1) && (*s == RDELIM)))
		elog(ERROR, "Bad path external representation '%s'", str);

	path->closed = (!isopen);

	return path;
}	/* path_in() */


char *
path_out(PATH *path)
{
	if (!PointerIsValid(path))
		return NULL;

	return path_encode(path->closed, path->npts, (Point *) &(path->p[0]));
}	/* path_out() */


/*----------------------------------------------------------
 *	Relational operators.
 *		These are based on the path cardinality,
 *		as stupid as that sounds.
 *
 *		Better relops and access methods coming soon.
 *---------------------------------------------------------*/

bool
path_n_lt(PATH *p1, PATH *p2)
{
	return (p1->npts < p2->npts);
}

bool
path_n_gt(PATH *p1, PATH *p2)
{
	return (p1->npts > p2->npts);
}

bool
path_n_eq(PATH *p1, PATH *p2)
{
	return (p1->npts == p2->npts);
}

bool
path_n_le(PATH *p1, PATH *p2)
{
	return (p1->npts <= p2->npts);
}

bool
path_n_ge(PATH *p1, PATH *p2)
{
	return (p1->npts >= p2->npts);
}


/*----------------------------------------------------------
 * Conversion operators.
 *---------------------------------------------------------*/

bool
path_isclosed(PATH *path)
{
	if (!PointerIsValid(path))
		return FALSE;

	return path->closed;
}	/* path_isclosed() */

bool
path_isopen(PATH *path)
{
	if (!PointerIsValid(path))
		return FALSE;

	return !path->closed;
}	/* path_isopen() */


int4
path_npoints(PATH *path)
{
	if (!PointerIsValid(path))
		return 0;

	return path->npts;
}	/* path_npoints() */

PATH *
path_close(PATH *path)
{
	PATH	   *result;

	if (!PointerIsValid(path))
		return NULL;

	result = path_copy(path);
	result->closed = TRUE;

	return result;
}	/* path_close() */


PATH *
path_open(PATH *path)
{
	PATH	   *result;

	if (!PointerIsValid(path))
		return NULL;

	result = path_copy(path);
	result->closed = FALSE;

	return result;
}	/* path_open() */


static PATH *
path_copy(PATH *path)
{
	PATH	   *result;
	int			size;

	size = offsetof(PATH, p[0]) +(sizeof(path->p[0]) * path->npts);
	result = palloc(size);

	memmove((char *) result, (char *) path, size);
	return result;
}	/* path_copy() */


/* path_inter -
 *		Does p1 intersect p2 at any point?
 *		Use bounding boxes for a quick (O(n)) check, then do a
 *		O(n^2) iterative edge check.
 */
bool
path_inter(PATH *p1, PATH *p2)
{
	BOX			b1,
				b2;
	int			i,
				j;
	LSEG		seg1,
				seg2;

	b1.higH.x = b1.loW.x = p1->p[0].x;
	b1.higH.y = b1.loW.y = p1->p[0].y;
	for (i = 1; i < p1->npts; i++)
	{
		b1.higH.x = Max(p1->p[i].x, b1.higH.x);
		b1.higH.y = Max(p1->p[i].y, b1.higH.y);
		b1.loW.x = Min(p1->p[i].x, b1.loW.x);
		b1.loW.y = Min(p1->p[i].y, b1.loW.y);
	}
	b2.higH.x = b2.loW.x = p2->p[0].x;
	b2.higH.y = b2.loW.y = p2->p[0].y;
	for (i = 1; i < p2->npts; i++)
	{
		b2.higH.x = Max(p2->p[i].x, b2.higH.x);
		b2.higH.y = Max(p2->p[i].y, b2.higH.y);
		b2.loW.x = Min(p2->p[i].x, b2.loW.x);
		b2.loW.y = Min(p2->p[i].y, b2.loW.y);
	}
	if (!rect_overlap(&b1, &b2))
		return FALSE;

	/* pairwise check lseg intersections */
	for (i = 0; i < p1->npts - 1; i++)
	{
		for (j = 0; j < p2->npts - 1; j++)
		{
			statlseg_construct(&seg1, &p1->p[i], &p1->p[i + 1]);
			statlseg_construct(&seg2, &p2->p[j], &p2->p[j + 1]);
			if (lseg_intersect(&seg1, &seg2))
				return TRUE;
		}
	}

	/* if we dropped through, no two segs intersected */
	return FALSE;
}	/* path_inter() */

/* path_distance()
 * This essentially does a cartesian product of the lsegs in the
 *	two paths, and finds the min distance between any two lsegs
 */
double *
path_distance(PATH *p1, PATH *p2)
{
	double	   *min = NULL,
			   *tmp;
	int			i,
				j;
	LSEG		seg1,
				seg2;

/*
	statlseg_construct(&seg1, &p1->p[0], &p1->p[1]);
	statlseg_construct(&seg2, &p2->p[0], &p2->p[1]);
	min = lseg_distance(&seg1, &seg2);
*/

	for (i = 0; i < p1->npts - 1; i++)
		for (j = 0; j < p2->npts - 1; j++)
		{
			statlseg_construct(&seg1, &p1->p[i], &p1->p[i + 1]);
			statlseg_construct(&seg2, &p2->p[j], &p2->p[j + 1]);

			tmp = lseg_distance(&seg1, &seg2);
			if ((min == NULL) || (*min < *tmp))
			{
				if (min != NULL)
					pfree(min);
				min = tmp;
			}
			else
				pfree(tmp);
		}

	return min;
}	/* path_distance() */


/*----------------------------------------------------------
 *	"Arithmetic" operations.
 *---------------------------------------------------------*/

double *
path_length(PATH *path)
{
	double	   *result;
	int			i;

	result = palloc(sizeof(double));

	*result = 0;
	for (i = 0; i < (path->npts - 1); i++)
		*result += point_dt(&path->p[i], &path->p[i + 1]);

	return result;
}	/* path_length() */


#ifdef NOT_USED
double
path_ln(PATH *path)
{
	double		result;
	int			i;

	result = 0;
	for (i = 0; i < (path->npts - 1); i++)
		result += point_dt(&path->p[i], &path->p[i + 1]);

	return result;
}	/* path_ln() */

#endif

/***********************************************************************
 **
 **		Routines for 2D points.
 **
 ***********************************************************************/

/*----------------------------------------------------------
 *	String to point, point to string conversion.
 *		External format:
 *				"(x,y)"
 *				"x,y"
 *---------------------------------------------------------*/

Point *
point_in(char *str)
{
	Point	   *point;

	double		x,
				y;
	char	   *s;

	if (!PointerIsValid(str))
		elog(ERROR, "Bad (null) point external representation");

	if (!pair_decode(str, &x, &y, &s) || (strlen(s) > 0))
		elog(ERROR, "Bad point external representation '%s'", str);

	point = palloc(sizeof(Point));

	point->x = x;
	point->y = y;

	return point;
}	/* point_in() */

char *
point_out(Point *pt)
{
	if (!PointerIsValid(pt))
		return NULL;

	return path_encode(-1, 1, pt);
}	/* point_out() */


static Point *
point_construct(double x, double y)
{
	Point	   *result = palloc(sizeof(Point));

	result->x = x;
	result->y = y;
	return result;
}


static Point *
point_copy(Point *pt)
{
	Point	   *result;

	if (!PointerIsValid(pt))
		return NULL;

	result = palloc(sizeof(Point));

	result->x = pt->x;
	result->y = pt->y;
	return result;
}


/*----------------------------------------------------------
 *	Relational operators for Points.
 *		Since we do have a sense of coordinates being
 *		"equal" to a given accuracy (point_vert, point_horiz),
 *		the other ops must preserve that sense.  This means
 *		that results may, strictly speaking, be a lie (unless
 *		EPSILON = 0.0).
 *---------------------------------------------------------*/

bool
point_left(Point *pt1, Point *pt2)
{
	return FPlt(pt1->x, pt2->x);
}

bool
point_right(Point *pt1, Point *pt2)
{
	return FPgt(pt1->x, pt2->x);
}

bool
point_above(Point *pt1, Point *pt2)
{
	return FPgt(pt1->y, pt2->y);
}

bool
point_below(Point *pt1, Point *pt2)
{
	return FPlt(pt1->y, pt2->y);
}

bool
point_vert(Point *pt1, Point *pt2)
{
	return FPeq(pt1->x, pt2->x);
}

bool
point_horiz(Point *pt1, Point *pt2)
{
	return FPeq(pt1->y, pt2->y);
}

bool
point_eq(Point *pt1, Point *pt2)
{
	return point_horiz(pt1, pt2) && point_vert(pt1, pt2);
}

bool
point_ne(Point *pt1, Point *pt2)
{
	return !point_eq(pt1, pt2);
}

/*----------------------------------------------------------
 *	"Arithmetic" operators on points.
 *---------------------------------------------------------*/

int32
pointdist(Point *p1, Point *p2)
{
	int32		result;

	result = point_dt(p1, p2);
	return result;
}

double *
point_distance(Point *pt1, Point *pt2)
{
	double	   *result = palloc(sizeof(double));

	*result = HYPOT(pt1->x - pt2->x, pt1->y - pt2->y);
	return result;
}


double
point_dt(Point *pt1, Point *pt2)
{
#ifdef GEODEBUG
	printf("point_dt- segment (%f,%f),(%f,%f) length is %f\n",
		   pt1->x, pt1->y, pt2->x, pt2->y, HYPOT(pt1->x - pt2->x, pt1->y - pt2->y));
#endif
	return HYPOT(pt1->x - pt2->x, pt1->y - pt2->y);
}

double *
point_slope(Point *pt1, Point *pt2)
{
	double	   *result = palloc(sizeof(double));

	if (point_vert(pt1, pt2))
		*result = (double) DBL_MAX;
	else
		*result = (pt1->y - pt2->y) / (pt1->x - pt1->x);
	return result;
}


double
point_sl(Point *pt1, Point *pt2)
{
	return (point_vert(pt1, pt2)
			? (double) DBL_MAX
			: (pt1->y - pt2->y) / (pt1->x - pt2->x));
}


/***********************************************************************
 **
 **		Routines for 2D line segments.
 **
 ***********************************************************************/

/*----------------------------------------------------------
 *	String to lseg, lseg to string conversion.
 *		External forms: "[(x1, y1), (x2, y2)]"
 *						"(x1, y1), (x2, y2)"
 *						"x1, y1, x2, y2"
 *		closed form ok	"((x1, y1), (x2, y2))"
 *		(old form)		"(x1, y1, x2, y2)"
 *---------------------------------------------------------*/

LSEG *
lseg_in(char *str)
{
	LSEG	   *lseg;

	int			isopen;
	char	   *s;

	if (!PointerIsValid(str))
		elog(ERROR, " Bad (null) lseg external representation");

	lseg = palloc(sizeof(LSEG));

	if ((!path_decode(TRUE, 2, str, &isopen, &s, &(lseg->p[0])))
		|| (*s != '\0'))
		elog(ERROR, "Bad lseg external representation '%s'", str);

#ifdef NOT_USED
	lseg->m = point_sl(&lseg->p[0], &lseg->p[1]);
#endif

	return lseg;
}	/* lseg_in() */


char *
lseg_out(LSEG *ls)
{
	if (!PointerIsValid(ls))
		return NULL;

	return path_encode(FALSE, 2, (Point *) &(ls->p[0]));
}	/* lseg_out() */


/* lseg_construct -
 *		form a LSEG from two Points.
 */
LSEG *
lseg_construct(Point *pt1, Point *pt2)
{
	LSEG	   *result = palloc(sizeof(LSEG));

	result->p[0].x = pt1->x;
	result->p[0].y = pt1->y;
	result->p[1].x = pt2->x;
	result->p[1].y = pt2->y;

#ifdef NOT_USED
	result->m = point_sl(pt1, pt2);
#endif

	return result;
}

/* like lseg_construct, but assume space already allocated */
static void
statlseg_construct(LSEG *lseg, Point *pt1, Point *pt2)
{
	lseg->p[0].x = pt1->x;
	lseg->p[0].y = pt1->y;
	lseg->p[1].x = pt2->x;
	lseg->p[1].y = pt2->y;

#ifdef NOT_USED
	lseg->m = point_sl(pt1, pt2);
#endif
}

double *
lseg_length(LSEG *lseg)
{
	double	   *result;

	if (!PointerIsValid(lseg))
		return NULL;

	result = point_distance(&lseg->p[0], &lseg->p[1]);

	return result;
}	/* lseg_length() */

/*----------------------------------------------------------
 *	Relative position routines.
 *---------------------------------------------------------*/

/*
 **  find intersection of the two lines, and see if it falls on
 **  both segments.
 */
bool
lseg_intersect(LSEG *l1, LSEG *l2)
{
	LINE	   *ln;
	Point	   *interpt;
	bool		retval;

	ln = line_construct_pp(&l2->p[0], &l2->p[1]);
	interpt = interpt_sl(l1, ln);

	if (interpt != NULL && on_ps(interpt, l2))	/* interpt on l1 and l2 */
		retval = TRUE;
	else
		retval = FALSE;
	if (interpt != NULL)
		pfree(interpt);
	pfree(ln);
	return retval;
}

bool
lseg_parallel(LSEG *l1, LSEG *l2)
{
#ifdef NOT_USED
	return FPeq(l1->m, l2->m);
#endif
	return (FPeq(point_sl(&(l1->p[0]), &(l1->p[1])),
				 point_sl(&(l2->p[0]), &(l2->p[1]))));
}	/* lseg_parallel() */

/* lseg_perp()
 * Determine if two line segments are perpendicular.
 *
 * This code did not get the correct answer for
 *	'((0,0),(0,1))'::lseg ?-| '((0,0),(1,0))'::lseg
 * So, modified it to check explicitly for slope of vertical line
 *	returned by point_sl() and the results seem better.
 * - thomas 1998-01-31
 */
bool
lseg_perp(LSEG *l1, LSEG *l2)
{
	double		m1,
				m2;

	m1 = point_sl(&(l1->p[0]), &(l1->p[1]));
	m2 = point_sl(&(l2->p[0]), &(l2->p[1]));

#ifdef GEODEBUG
	printf("lseg_perp- slopes are %g and %g\n", m1, m2);
#endif
	if (FPzero(m1))
		return FPeq(m2, DBL_MAX);
	else if (FPzero(m2))
		return FPeq(m1, DBL_MAX);

	return FPeq(m1 / m2, -1.0);
}	/* lseg_perp() */

bool
lseg_vertical(LSEG *lseg)
{
	return FPeq(lseg->p[0].x, lseg->p[1].x);
}

bool
lseg_horizontal(LSEG *lseg)
{
	return FPeq(lseg->p[0].y, lseg->p[1].y);
}


bool
lseg_eq(LSEG *l1, LSEG *l2)
{
	return (FPeq(l1->p[0].x, l2->p[0].x) &&
			FPeq(l1->p[1].y, l2->p[1].y) &&
			FPeq(l1->p[0].x, l2->p[0].x) &&
			FPeq(l1->p[1].y, l2->p[1].y));
}	/* lseg_eq() */

bool
lseg_ne(LSEG *l1, LSEG *l2)
{
	return (!FPeq(l1->p[0].x, l2->p[0].x) ||
			!FPeq(l1->p[1].y, l2->p[1].y) ||
			!FPeq(l1->p[0].x, l2->p[0].x) ||
			!FPeq(l1->p[1].y, l2->p[1].y));
}	/* lseg_ne() */

bool
lseg_lt(LSEG *l1, LSEG *l2)
{
	return FPlt(point_dt(&l1->p[0], &l1->p[1]), point_dt(&l2->p[0], &l2->p[1]));
}	/* lseg_lt() */

bool
lseg_le(LSEG *l1, LSEG *l2)
{
	return FPle(point_dt(&l1->p[0], &l1->p[1]), point_dt(&l2->p[0], &l2->p[1]));
}	/* lseg_le() */

bool
lseg_gt(LSEG *l1, LSEG *l2)
{
	return FPgt(point_dt(&l1->p[0], &l1->p[1]), point_dt(&l2->p[0], &l2->p[1]));
}	/* lseg_gt() */

bool
lseg_ge(LSEG *l1, LSEG *l2)
{
	return FPge(point_dt(&l1->p[0], &l1->p[1]), point_dt(&l2->p[0], &l2->p[1]));
}	/* lseg_ge() */


/*----------------------------------------------------------
 *	Line arithmetic routines.
 *---------------------------------------------------------*/

/* lseg_distance -
 *		If two segments don't intersect, then the closest
 *		point will be from one of the endpoints to the other
 *		segment.
 */
double *
lseg_distance(LSEG *l1, LSEG *l2)
{
	double	   *result = palloc(sizeof(double));

	*result = lseg_dt(l1, l2);

	return result;
}

/* lseg_dt()
 * Distance between two line segments.
 * Must check both sets of endpoints to ensure minimum distance is found.
 * - thomas 1998-02-01
 */
static double
lseg_dt(LSEG *l1, LSEG *l2)
{
	double	   *d,
				result;

	if (lseg_intersect(l1, l2))
		return 0.0;

	d = dist_ps(&l1->p[0], l2);
	result = *d;
	pfree(d);
	d = dist_ps(&l1->p[1], l2);
	result = Min(result, *d);
	pfree(d);
	d = dist_ps(&l2->p[0], l1);
	result = Min(result, *d);
	pfree(d);
	d = dist_ps(&l2->p[1], l1);
	result = Min(result, *d);
	pfree(d);

	return result;
}	/* lseg_dt() */


Point *
lseg_center(LSEG *lseg)
{
	Point	   *result;

	if (!PointerIsValid(lseg))
		return NULL;

	result = palloc(sizeof(Point));

	result->x = (lseg->p[0].x - lseg->p[1].x) / 2;
	result->y = (lseg->p[0].y - lseg->p[1].y) / 2;

	return result;
}	/* lseg_center() */


/* lseg_interpt -
 *		Find the intersection point of two segments (if any).
 *		Find the intersection of the appropriate lines; if the
 *		point is not on a given segment, there is no valid segment
 *		intersection point at all.
 * If there is an intersection, then check explicitly for matching
 *	endpoints since there may be rounding effects with annoying
 *	lsb residue. - tgl 1997-07-09
 */
Point *
lseg_interpt(LSEG *l1, LSEG *l2)
{
	Point	   *result;
	LINE	   *tmp1,
			   *tmp2;

	if (!PointerIsValid(l1) || !PointerIsValid(l2))
		return NULL;

	tmp1 = line_construct_pp(&l1->p[0], &l1->p[1]);
	tmp2 = line_construct_pp(&l2->p[0], &l2->p[1]);
	result = line_interpt(tmp1, tmp2);
	if (PointerIsValid(result))
	{
		if (on_ps(result, l1))
		{
			if ((FPeq(l1->p[0].x, l2->p[0].x) && FPeq(l1->p[0].y, l2->p[0].y))
				|| (FPeq(l1->p[0].x, l2->p[1].x) && FPeq(l1->p[0].y, l2->p[1].y)))
			{
				result->x = l1->p[0].x;
				result->y = l1->p[0].y;

			}
			else if ((FPeq(l1->p[1].x, l2->p[0].x) && FPeq(l1->p[1].y, l2->p[0].y))
					 || (FPeq(l1->p[1].x, l2->p[1].x) && FPeq(l1->p[1].y, l2->p[1].y)))
			{
				result->x = l1->p[1].x;
				result->y = l1->p[1].y;
			}
		}
		else
		{
			pfree(result);
			result = NULL;
		}
	}
	pfree(tmp1);
	pfree(tmp2);

	return result;
}	/* lseg_interpt() */

/***********************************************************************
 **
 **		Routines for position comparisons of differently-typed
 **				2D objects.
 **
 ***********************************************************************/

#define ABOVE	1
#define BELOW	0
#define UNDEF	-1


/*---------------------------------------------------------------------
 *		dist_
 *				Minimum distance from one object to another.
 *-------------------------------------------------------------------*/

double *
dist_pl(Point *pt, LINE *line)
{
	double	   *result = palloc(sizeof(double));

	*result = (line->A * pt->x + line->B * pt->y + line->C) /
		HYPOT(line->A, line->B);

	return result;
}

double *
dist_ps(Point *pt, LSEG *lseg)
{
	double		m;				/* slope of perp. */
	LINE	   *ln;
	double	   *result,
			   *tmpdist;
	Point	   *ip;

/*
 * Construct a line perpendicular to the input segment
 * and through the input point
 */
	if (lseg->p[1].x == lseg->p[0].x)
		m = 0;
	else if (lseg->p[1].y == lseg->p[0].y)
	{							/* slope is infinite */
		m = (double) DBL_MAX;
	}
	else
	{
#ifdef NOT_USED
		m = (-1) * (lseg->p[1].y - lseg->p[0].y) /
			(lseg->p[1].x - lseg->p[0].x);
#endif
		m = ((lseg->p[0].y - lseg->p[1].y) / (lseg->p[1].x - lseg->p[0].x));
	}
	ln = line_construct_pm(pt, m);

#ifdef GEODEBUG
	printf("dist_ps- line is A=%g B=%g C=%g from (point) slope (%f,%f) %g\n",
		   ln->A, ln->B, ln->C, pt->x, pt->y, m);
#endif

/*
 * Calculate distance to the line segment
 *	or to the endpoints of the segment.
 */

	/* intersection is on the line segment? */
	if ((ip = interpt_sl(lseg, ln)) != NULL)
	{
		result = point_distance(pt, ip);
#ifdef GEODEBUG
		printf("dist_ps- distance is %f to intersection point is (%f,%f)\n",
			   *result, ip->x, ip->y);
#endif

		/* otherwise, intersection is not on line segment */
	}
	else
	{
		result = point_distance(pt, &lseg->p[0]);
		tmpdist = point_distance(pt, &lseg->p[1]);
		if (*tmpdist < *result)
			*result = *tmpdist;
		pfree(tmpdist);
	}

	if (ip != NULL)
		pfree(ip);
	pfree(ln);
	return result;
}


/*
 ** Distance from a point to a path
 */
double *
dist_ppath(Point *pt, PATH *path)
{
	double	   *result;
	double	   *tmp;
	int			i;
	LSEG		lseg;

	switch (path->npts)
	{
			/* no points in path? then result is undefined... */
		case 0:
			result = NULL;
			break;
			/* one point in path? then get distance between two points... */
		case 1:
			result = point_distance(pt, &path->p[0]);
			break;
		default:
			/* make sure the path makes sense... */
			Assert(path->npts > 1);

			/*
			 * the distance from a point to a path is the smallest
			 * distance from the point to any of its constituent segments.
			 */
			result = palloc(sizeof(double));
			for (i = 0; i < path->npts - 1; i++)
			{
				statlseg_construct(&lseg, &path->p[i], &path->p[i + 1]);
				tmp = dist_ps(pt, &lseg);
				if (i == 0 || *tmp < *result)
					*result = *tmp;
				pfree(tmp);
			}
			break;
	}
	return result;
}

double *
dist_pb(Point *pt, BOX *box)
{
	Point	   *tmp;
	double	   *result;

	tmp = close_pb(pt, box);
	result = point_distance(tmp, pt);
	pfree(tmp);

	return result;
}


double *
dist_sl(LSEG *lseg, LINE *line)
{
	double	   *result,
			   *d2;

	if (inter_sl(lseg, line))
	{
		result = palloc(sizeof(double));
		*result = 0.0;

	}
	else
	{
		result = dist_pl(&lseg->p[0], line);
		d2 = dist_pl(&lseg->p[1], line);
		if (*d2 > *result)
		{
			pfree(result);
			result = d2;
		}
		else
			pfree(d2);
	}

	return result;
}


double *
dist_sb(LSEG *lseg, BOX *box)
{
	Point	   *tmp;
	double	   *result;

	tmp = close_sb(lseg, box);
	if (tmp == NULL)
	{
		result = palloc(sizeof(double));
		*result = 0.0;
	}
	else
	{
		result = dist_pb(tmp, box);
		pfree(tmp);
	}

	return result;
}


double *
dist_lb(LINE *line, BOX *box)
{
	Point	   *tmp;
	double	   *result;

	tmp = close_lb(line, box);
	if (tmp == NULL)
	{
		result = palloc(sizeof(double));
		*result = 0.0;
	}
	else
	{
		result = dist_pb(tmp, box);
		pfree(tmp);
	}

	return result;
}


double *
dist_cpoly(CIRCLE *circle, POLYGON *poly)
{
	double	   *result;
	int			i;
	double	   *d;
	LSEG		seg;

	if (!PointerIsValid(circle) || !PointerIsValid(poly))
		elog(ERROR, "Invalid (null) input for distance");

	if (point_inside(&(circle->center), poly->npts, poly->p))
	{
#ifdef GEODEBUG
		printf("dist_cpoly- center inside of polygon\n");
#endif
		result = palloc(sizeof(double));

		*result = 0;
		return result;
	}

	/* initialize distance with segment between first and last points */
	seg.p[0].x = poly->p[0].x;
	seg.p[0].y = poly->p[0].y;
	seg.p[1].x = poly->p[poly->npts - 1].x;
	seg.p[1].y = poly->p[poly->npts - 1].y;
	result = dist_ps(&(circle->center), &seg);
#ifdef GEODEBUG
	printf("dist_cpoly- segment 0/n distance is %f\n", *result);
#endif

	/* check distances for other segments */
	for (i = 0; (i < poly->npts - 1); i++)
	{
		seg.p[0].x = poly->p[i].x;
		seg.p[0].y = poly->p[i].y;
		seg.p[1].x = poly->p[i + 1].x;
		seg.p[1].y = poly->p[i + 1].y;
		d = dist_ps(&(circle->center), &seg);
#ifdef GEODEBUG
		printf("dist_cpoly- segment %d distance is %f\n", (i + 1), *d);
#endif
		if (*d < *result)
			*result = *d;
		pfree(d);
	}

	*result -= circle->radius;
	if (*result < 0)
		*result = 0;

	return result;
}	/* dist_cpoly() */


/*---------------------------------------------------------------------
 *		interpt_
 *				Intersection point of objects.
 *				We choose to ignore the "point" of intersection between
 *				  lines and boxes, since there are typically two.
 *-------------------------------------------------------------------*/

static Point *
interpt_sl(LSEG *lseg, LINE *line)
{
	LINE	   *tmp;
	Point	   *p;

	tmp = line_construct_pp(&lseg->p[0], &lseg->p[1]);
	p = line_interpt(tmp, line);
#ifdef GEODEBUG
	printf("interpt_sl- segment is (%.*g %.*g) (%.*g %.*g)\n",
		   digits8, lseg->p[0].x, digits8, lseg->p[0].y, digits8, lseg->p[1].x, digits8, lseg->p[1].y);
	printf("interpt_sl- segment becomes line A=%.*g B=%.*g C=%.*g\n",
		   digits8, tmp->A, digits8, tmp->B, digits8, tmp->C);
#endif
	if (PointerIsValid(p))
	{
#ifdef GEODEBUG
		printf("interpt_sl- intersection point is (%.*g %.*g)\n", digits8, p->x, digits8, p->y);
#endif
		if (on_ps(p, lseg))
		{
#ifdef GEODEBUG
			printf("interpt_sl- intersection point is on segment\n");
#endif

		}
		else
		{
			pfree(p);
			p = NULL;
		}
	}

	pfree(tmp);
	return p;
}


/*---------------------------------------------------------------------
 *		close_
 *				Point of closest proximity between objects.
 *-------------------------------------------------------------------*/

/* close_pl -
 *		The intersection point of a perpendicular of the line
 *		through the point.
 */
Point *
close_pl(Point *pt, LINE *line)
{
	Point	   *result;
	LINE	   *tmp;
	double		invm;

	result = palloc(sizeof(Point));
#ifdef NOT_USED
	if (FPeq(line->A, -1.0) && FPzero(line->B))
	{							/* vertical */
	}
#endif
	if (line_vertical(line))
	{
		result->x = line->C;
		result->y = pt->y;
		return result;

#ifdef NOT_USED
	}
	else if (FPzero(line->m))
	{							/* horizontal */
#endif
	}
	else if (line_horizontal(line))
	{
		result->x = pt->x;
		result->y = line->C;
		return result;
	}
	/* drop a perpendicular and find the intersection point */
#ifdef NOT_USED
	invm = -1.0 / line->m;
#endif
	/* invert and flip the sign on the slope to get a perpendicular */
	invm = line->B / line->A;
	tmp = line_construct_pm(pt, invm);
	result = line_interpt(tmp, line);
	return result;
}	/* close_pl() */


/* close_ps()
 * Closest point on line segment to specified point.
 * Take the closest endpoint if the point is left, right,
 *	above, or beloW the segment, otherwise find the intersection
 *	point of the segment and its perpendicular through the point.
 *
 * Some tricky code here, relying on boolean expressions
 *	evaluating to only zero or one to use as an array index.
 *		bug fixes by gthaker@atl.lmco.com; May 1, 1998
 */
Point *
close_ps(Point *pt, LSEG *lseg)
{
	Point	   *result;
	LINE	   *tmp;
	double		invm;
	int			xh,
				yh;

#ifdef GEODEBUG
	printf("close_sp:pt->x %f pt->y %f\nlseg(0).x %f lseg(0).y %f  lseg(1).x %f lseg(1).y %f\n",
	pt->x, pt->y, lseg->p[0].x, lseg->p[0].y, lseg->p[1].x, lseg->p[1].y);
#endif

	result = NULL;
	xh = lseg->p[0].x < lseg->p[1].x;
	yh = lseg->p[0].y < lseg->p[1].y;
	/* !xh (or !yh) is the index of loWer x( or y) end point of lseg */

	/* vertical segment? */
	if (lseg_vertical(lseg))
	{
#ifdef GEODEBUG
		printf("close_ps- segment is vertical\n");
#endif
		/* first check if point is beloW or above the entire lseg. */
		if (pt->y < lseg->p[!yh].y)
			result = point_copy(&lseg->p[!yh]); /* beloW the lseg */
		else if (pt->y > lseg->p[yh].y)
			result = point_copy(&lseg->p[yh]);	/* above the lseg */
		if (result != NULL)
			return result;

		/* point lines along (to left or right) of the vertical lseg. */

		result = palloc(sizeof(*result));
		result->x = lseg->p[0].x;
		result->y = pt->y;
		return result;
	}
	else if (lseg_horizontal(lseg))
	{
#ifdef GEODEBUG
		printf("close_ps- segment is horizontal\n");
#endif
		/* first check if point is left or right of the entire lseg. */
		if (pt->x < lseg->p[!xh].x)
			result = point_copy(&lseg->p[!xh]); /* left of the lseg */
		else if (pt->x > lseg->p[xh].x)
			result = point_copy(&lseg->p[xh]);	/* right of the lseg */
		if (result != NULL)
			return result;

		/* point lines along (at top or beloW) the horiz. lseg. */
		result = palloc(sizeof(*result));
		result->x = pt->x;
		result->y = lseg->p[0].y;
		return result;
	}

	/*
	 * vert. and horiz. cases are down, now check if the closest point is
	 * one of the end points or someplace on the lseg.
	 */

	/* TODO: Ask if "tmp" should be freed to prevent memory leak */

	invm = -1.0 / point_sl(&(lseg->p[0]), &(lseg->p[1]));
	tmp = line_construct_pm(&lseg->p[!yh], invm);		/* loWer edge of the
														 * "band" */
	if (pt->y < (tmp->A * pt->x + tmp->C))
	{							/* we are beloW the loWer edge */
		result = point_copy(&lseg->p[!yh]);		/* beloW the lseg, take
												 * loWer end pt */
/*	  fprintf(stderr,"beloW: tmp A %f  B %f   C %f    m %f\n",tmp->A,tmp->B,tmp->C, tmp->m); */
		return result;
	}
	tmp = line_construct_pm(&lseg->p[yh], invm);		/* upper edge of the
														 * "band" */
	if (pt->y > (tmp->A * pt->x + tmp->C))
	{							/* we are beloW the loWer edge */
		result = point_copy(&lseg->p[yh]);		/* above the lseg, take
												 * higHer end pt */
/*	  fprintf(stderr,"above: tmp A %f  B %f   C %f    m %f\n",tmp->A,tmp->B,tmp->C, tmp->m); */
		return result;
	}

	/*
	 * at this point the "normal" from point will hit lseg. The closet
	 * point will be somewhere on the lseg
	 */
	tmp = line_construct_pm(pt, invm);
/*	fprintf(stderr,"tmp A %f  B %f   C %f    m %f\n",tmp->A,tmp->B,tmp->C, tmp->m); */
	result = interpt_sl(lseg, tmp);
/*	fprintf(stderr,"result.x %f  result.y %f\n", result->x, result->y); */
	return result;
}	/* close_ps() */


/* close_lseg()
 * Closest point to l1 on l2.
 */
Point *
close_lseg(LSEG *l1, LSEG *l2)
{
	Point	   *result = NULL;
	Point		point;
	double		dist;
	double	   *d;

	d = dist_ps(&l1->p[0], l2);
	dist = *d;
	memcpy(&point, &l1->p[0], sizeof(point));
	pfree(d);

	if (*(d = dist_ps(&l1->p[1], l2)) < dist)
	{
		dist = *d;
		memcpy(&point, &l1->p[1], sizeof(point));
	}
	pfree(d);

	if (*(d = dist_ps(&l2->p[0], l1)) < dist)
	{
		result = close_ps(&l2->p[0], l1);
		memcpy(&point, result, sizeof(point));
		pfree(result);
		result = close_ps(&point, l2);
	}
	pfree(d);

	if (*(d = dist_ps(&l2->p[1], l1)) < dist)
	{
		if (result != NULL)
			pfree(result);

		result = close_ps(&l2->p[1], l1);
		memcpy(&point, result, sizeof(point));
		pfree(result);
		result = close_ps(&point, l2);
	}
	pfree(d);

	if (result == NULL)
	{
		result = palloc(sizeof(*result));
		memcpy(result, &point, sizeof(*result));
	}

	return result;
}	/* close_lseg() */

/* close_pb()
 * Closest point on or in box to specified point.
 */
Point *
close_pb(Point *pt, BOX *box)
{
	LSEG		lseg,
				seg;
	Point		point;
	double		dist,
			   *d;

	if (on_pb(pt, box))
		return pt;

	/* pairwise check lseg distances */
	point.x = box->loW.x;
	point.y = box->higH.y;
	statlseg_construct(&lseg, &box->loW, &point);
	dist = *(d = dist_ps(pt, &lseg));
	pfree(d);

	statlseg_construct(&seg, &box->higH, &point);
	if (*(d = dist_ps(pt, &seg)) < dist)
	{
		dist = *d;
		memcpy(&lseg, &seg, sizeof(lseg));
	}
	pfree(d);

	point.x = box->higH.x;
	point.y = box->loW.y;
	statlseg_construct(&seg, &box->loW, &point);
	if (*(d = dist_ps(pt, &seg)) < dist)
	{
		dist = *d;
		memcpy(&lseg, &seg, sizeof(lseg));
	}
	pfree(d);

	statlseg_construct(&seg, &box->higH, &point);
	if (*(d = dist_ps(pt, &seg)) < dist)
	{
		dist = *d;
		memcpy(&lseg, &seg, sizeof(lseg));
	}
	pfree(d);

	return close_ps(pt, &lseg);
}	/* close_pb() */

/* close_sl()
 * Closest point on line to line segment.
 *
 * XXX THIS CODE IS WRONG
 * The code is actually calculating the point on the line segment
 *	which is backwards from the routine naming convention.
 * Copied code to new routine close_ls() but haven't fixed this one yet.
 * - thomas 1998-01-31
 */
Point *
close_sl(LSEG *lseg, LINE *line)
{
	Point	   *result;
	double	   *d1,
			   *d2;

	result = interpt_sl(lseg, line);
	if (result)
		return result;

	d1 = dist_pl(&lseg->p[0], line);
	d2 = dist_pl(&lseg->p[1], line);
	if (d1 < d2)
		result = point_copy(&lseg->p[0]);
	else
		result = point_copy(&lseg->p[1]);

	pfree(d1);
	pfree(d2);
	return result;
}

/* close_ls()
 * Closest point on line segment to line.
 */
Point *
close_ls(LINE *line, LSEG *lseg)
{
	Point	   *result;
	double	   *d1,
			   *d2;

	result = interpt_sl(lseg, line);
	if (result)
		return result;

	d1 = dist_pl(&lseg->p[0], line);
	d2 = dist_pl(&lseg->p[1], line);
	if (d1 < d2)
		result = point_copy(&lseg->p[0]);
	else
		result = point_copy(&lseg->p[1]);

	pfree(d1);
	pfree(d2);
	return result;
}	/* close_ls() */

/* close_sb()
 * Closest point on or in box to line segment.
 */
Point *
close_sb(LSEG *lseg, BOX *box)
{
	Point	   *result;
	Point	   *pt;
	Point		point;

	LSEG		bseg,
				seg;
	double		dist,
				d;

	/* segment intersects box? then just return closest point to center */
	if (inter_sb(lseg, box))
	{
		pt = rect_center(box);
		result = close_ps(pt, lseg);
		pfree(pt);
		return result;
	}

	/* pairwise check lseg distances */
	point.x = box->loW.x;
	point.y = box->higH.y;
	statlseg_construct(&bseg, &box->loW, &point);
	dist = lseg_dt(lseg, &bseg);

	statlseg_construct(&seg, &box->higH, &point);
	if ((d = lseg_dt(lseg, &seg)) < dist)
	{
		dist = d;
		memcpy(&bseg, &seg, sizeof(bseg));
	}

	point.x = box->higH.x;
	point.y = box->loW.y;
	statlseg_construct(&seg, &box->loW, &point);
	if ((d = lseg_dt(lseg, &seg)) < dist)
	{
		dist = d;
		memcpy(&bseg, &seg, sizeof(bseg));
	}

	statlseg_construct(&seg, &box->higH, &point);
	if ((d = lseg_dt(lseg, &seg)) < dist)
	{
		dist = d;
		memcpy(&bseg, &seg, sizeof(bseg));
	}

	/* OK, we now have the closest line segment on the box boundary */
	return close_lseg(lseg, &bseg);
}	/* close_sb() */

Point *
close_lb(LINE *line, BOX *box)
{
	/* think about this one for a while */
	elog(ERROR, "close_lb not implemented");

	return NULL;
}

/*---------------------------------------------------------------------
 *		on_
 *				Whether one object lies completely within another.
 *-------------------------------------------------------------------*/

/* on_pl -
 *		Does the point satisfy the equation?
 */
bool
on_pl(Point *pt, LINE *line)
{
	if (!PointerIsValid(pt) || !PointerIsValid(line))
		return FALSE;

	return FPzero(line->A * pt->x + line->B * pt->y + line->C);
}


/* on_ps -
 *		Determine colinearity by detecting a triangle inequality.
 * This algorithm seems to behave nicely even with lsb residues - tgl 1997-07-09
 */
bool
on_ps(Point *pt, LSEG *lseg)
{
	if (!PointerIsValid(pt) || !PointerIsValid(lseg))
		return FALSE;

	return (FPeq(point_dt(pt, &lseg->p[0]) + point_dt(pt, &lseg->p[1]),
				 point_dt(&lseg->p[0], &lseg->p[1])));
}

bool
on_pb(Point *pt, BOX *box)
{
	if (!PointerIsValid(pt) || !PointerIsValid(box))
		return FALSE;

	return (pt->x <= box->higH.x && pt->x >= box->loW.x &&
			pt->y <= box->higH.y && pt->y >= box->loW.y);
}

/* on_ppath -
 *		Whether a point lies within (on) a polyline.
 *		If open, we have to (groan) check each segment.
 * (uses same algorithm as for point intersecting segment - tgl 1997-07-09)
 *		If closed, we use the old O(n) ray method for point-in-polygon.
 *				The ray is horizontal, from pt out to the right.
 *				Each segment that crosses the ray counts as an
 *				intersection; note that an endpoint or edge may touch
 *				but not cross.
 *				(we can do p-in-p in lg(n), but it takes preprocessing)
 */
#define NEXT(A) ((A+1) % path->npts)	/* cyclic "i+1" */

bool
on_ppath(Point *pt, PATH *path)
{
	int			i,
				n;
	double		a,
				b;

	if (!PointerIsValid(pt) || !PointerIsValid(path))
		return FALSE;

	/*-- OPEN --*/
	if (!path->closed)
	{
		n = path->npts - 1;
		a = point_dt(pt, &path->p[0]);
		for (i = 0; i < n; i++)
		{
			b = point_dt(pt, &path->p[i + 1]);
			if (FPeq(a + b,
					 point_dt(&path->p[i], &path->p[i + 1])))
				return TRUE;
			a = b;
		}
		return FALSE;
	}

	/*-- CLOSED --*/
	return point_inside(pt, path->npts, path->p);
}	/* on_ppath() */


bool
on_sl(LSEG *lseg, LINE *line)
{
	if (!PointerIsValid(lseg) || !PointerIsValid(line))
		return FALSE;

	return on_pl(&lseg->p[0], line) && on_pl(&lseg->p[1], line);
}	/* on_sl() */

bool
on_sb(LSEG *lseg, BOX *box)
{
	if (!PointerIsValid(lseg) || !PointerIsValid(box))
		return FALSE;

	return on_pb(&lseg->p[0], box) && on_pb(&lseg->p[1], box);
}	/* on_sb() */

/*---------------------------------------------------------------------
 *		inter_
 *				Whether one object intersects another.
 *-------------------------------------------------------------------*/

bool
inter_sl(LSEG *lseg, LINE *line)
{
	Point	   *tmp;

	if (!PointerIsValid(lseg) || !PointerIsValid(line))
		return FALSE;

	tmp = interpt_sl(lseg, line);
	if (tmp)
	{
		pfree(tmp);
		return TRUE;
	}
	return FALSE;
}

/* inter_sb()
 * Do line segment and box intersect?
 *
 * Segment completely inside box counts as intersection.
 * If you want only segments crossing box boundaries,
 *	try converting box to path first.
 *
 * Optimize for non-intersection by checking for box intersection first.
 * - thomas 1998-01-30
 */
bool
inter_sb(LSEG *lseg, BOX *box)
{
	BOX			lbox;
	LSEG		bseg;
	Point		point;

	if (!PointerIsValid(lseg) || !PointerIsValid(box))
		return FALSE;

	lbox.loW.x = Min(lseg->p[0].x, lseg->p[1].x);
	lbox.loW.y = Min(lseg->p[0].y, lseg->p[1].y);
	lbox.higH.x = Max(lseg->p[0].x, lseg->p[1].x);
	lbox.higH.y = Max(lseg->p[0].y, lseg->p[1].y);

	/* nothing close to overlap? then not going to intersect */
	if (!rect_overlap(&lbox, box))
		return FALSE;

	/* an endpoint of segment is inside box? then clearly intersects */
	if (on_pb(&lseg->p[0], box) || on_pb(&lseg->p[1], box))
		return TRUE;

	/* pairwise check lseg intersections */
	point.x = box->loW.x;
	point.y = box->higH.y;
	statlseg_construct(&bseg, &box->loW, &point);
	if (lseg_intersect(&bseg, lseg))
		return TRUE;

	statlseg_construct(&bseg, &box->higH, &point);
	if (lseg_intersect(&bseg, lseg))
		return TRUE;

	point.x = box->higH.x;
	point.y = box->loW.y;
	statlseg_construct(&bseg, &box->loW, &point);
	if (lseg_intersect(&bseg, lseg))
		return TRUE;

	statlseg_construct(&bseg, &box->higH, &point);
	if (lseg_intersect(&bseg, lseg))
		return TRUE;

	/* if we dropped through, no two segs intersected */
	return FALSE;
}	/* inter_sb() */

/* inter_lb()
 * Do line and box intersect?
 */
bool
inter_lb(LINE *line, BOX *box)
{
	LSEG		bseg;
	Point		p1,
				p2;

	if (!PointerIsValid(line) || !PointerIsValid(box))
		return FALSE;

	/* pairwise check lseg intersections */
	p1.x = box->loW.x;
	p1.y = box->loW.y;
	p2.x = box->loW.x;
	p2.y = box->higH.y;
	statlseg_construct(&bseg, &p1, &p2);
	if (inter_sl(&bseg, line))
		return TRUE;
	p1.x = box->higH.x;
	p1.y = box->higH.y;
	statlseg_construct(&bseg, &p1, &p2);
	if (inter_sl(&bseg, line))
		return TRUE;
	p2.x = box->higH.x;
	p2.y = box->loW.y;
	statlseg_construct(&bseg, &p1, &p2);
	if (inter_sl(&bseg, line))
		return TRUE;
	p1.x = box->loW.x;
	p1.y = box->loW.y;
	statlseg_construct(&bseg, &p1, &p2);
	if (inter_sl(&bseg, line))
		return TRUE;

	/* if we dropped through, no intersection */
	return FALSE;
}

/*------------------------------------------------------------------
 * The folloWing routines define a data type and operator class for
 * POLYGONS .... Part of which (the polygon's bounding box) is built on
 * top of the BOX data type.
 *
 * make_bound_box - create the bounding box for the input polygon
 *------------------------------------------------------------------*/

/*---------------------------------------------------------------------
 * Make the smallest bounding box for the given polygon.
 *---------------------------------------------------------------------*/
static void
make_bound_box(POLYGON *poly)
{
	int			i;
	double		x1,
				y1,
				x2,
				y2;

	if (poly->npts > 0)
	{
		x2 = x1 = poly->p[0].x;
		y2 = y1 = poly->p[0].y;
		for (i = 1; i < poly->npts; i++)
		{
			if (poly->p[i].x < x1)
				x1 = poly->p[i].x;
			if (poly->p[i].x > x2)
				x2 = poly->p[i].x;
			if (poly->p[i].y < y1)
				y1 = poly->p[i].y;
			if (poly->p[i].y > y2)
				y2 = poly->p[i].y;
		}

		rect_fill(&(poly->boundbox), x1, x2, y1, y2);
	}
	else
		elog(ERROR, "Unable to create bounding box for empty polygon");
}

/*------------------------------------------------------------------
 * poly_in - read in the polygon from a string specification
 *
 *		External format:
 *				"((x0,y0),...,(xn,yn))"
 *				"x0,y0,...,xn,yn"
 *				also supports the older style "(x1,...,xn,y1,...yn)"
 *------------------------------------------------------------------*/
POLYGON    *
poly_in(char *str)
{
	POLYGON    *poly;
	int			npts;
	int			size;
	int			isopen;
	char	   *s;

	if (!PointerIsValid(str))
		elog(ERROR, " Bad (null) polygon external representation");

	if ((npts = pair_count(str, ',')) <= 0)
		elog(ERROR, "Bad polygon external representation '%s'", str);

	size = offsetof(POLYGON, p[0]) +(sizeof(poly->p[0]) * npts);
	poly = palloc(size);

	MemSet((char *) poly, 0, size);		/* zero any holes */
	poly->size = size;
	poly->npts = npts;

	if ((!path_decode(FALSE, npts, str, &isopen, &s, &(poly->p[0])))
		|| (*s != '\0'))
		elog(ERROR, "Bad polygon external representation '%s'", str);

	make_bound_box(poly);

	return poly;
}	/* poly_in() */

/*---------------------------------------------------------------
 * poly_out - convert internal POLYGON representation to the
 *			  character string format "((f8,f8),...,(f8,f8))"
 *			  also support old format "(f8,f8,...,f8,f8)"
 *---------------------------------------------------------------*/
char *
poly_out(POLYGON *poly)
{
	if (!PointerIsValid(poly))
		return NULL;

	return path_encode(TRUE, poly->npts, &(poly->p[0]));
}	/* poly_out() */


/*-------------------------------------------------------
 * Is polygon A strictly left of polygon B? i.e. is
 * the right most point of A left of the left most point
 * of B?
 *-------------------------------------------------------*/
bool
poly_left(POLYGON *polya, POLYGON *polyb)
{
	return polya->boundbox.higH.x < polyb->boundbox.loW.x;
}

/*-------------------------------------------------------
 * Is polygon A overlapping or left of polygon B? i.e. is
 * the left most point of A left of the right most point
 * of B?
 *-------------------------------------------------------*/
bool
poly_overleft(POLYGON *polya, POLYGON *polyb)
{
	return polya->boundbox.loW.x <= polyb->boundbox.higH.x;
}

/*-------------------------------------------------------
 * Is polygon A strictly right of polygon B? i.e. is
 * the left most point of A right of the right most point
 * of B?
 *-------------------------------------------------------*/
bool
poly_right(POLYGON *polya, POLYGON *polyb)
{
	return polya->boundbox.loW.x > polyb->boundbox.higH.x;
}

/*-------------------------------------------------------
 * Is polygon A overlapping or right of polygon B? i.e. is
 * the right most point of A right of the left most point
 * of B?
 *-------------------------------------------------------*/
bool
poly_overright(POLYGON *polya, POLYGON *polyb)
{
	return polya->boundbox.higH.x > polyb->boundbox.loW.x;
}

/*-------------------------------------------------------
 * Is polygon A the same as polygon B? i.e. are all the
 * points the same?
 * Check all points for matches in both forward and reverse
 *	direction since polygons are non-directional and are
 *	closed shapes.
 *-------------------------------------------------------*/
bool
poly_same(POLYGON *polya, POLYGON *polyb)
{
	if (!PointerIsValid(polya) || !PointerIsValid(polyb))
		return FALSE;

	if (polya->npts != polyb->npts)
		return FALSE;

	return plist_same(polya->npts, polya->p, polyb->p);

#ifdef NOT_USED
	for (i = 0; i < polya->npts; i++)
	{
		if ((polya->p[i].x != polyb->p[i].x)
			|| (polya->p[i].y != polyb->p[i].y))
			return FALSE;
	}
	return TRUE;
#endif
}	/* poly_same() */

/*-----------------------------------------------------------------
 * Determine if polygon A overlaps polygon B by determining if
 * their bounding boxes overlap.
 *-----------------------------------------------------------------*/
bool
poly_overlap(POLYGON *polya, POLYGON *polyb)
{
	return rect_overlap(&(polya->boundbox), &(polyb->boundbox));
}


/*-----------------------------------------------------------------
 * Determine if polygon A contains polygon B by determining if A's
 * bounding box contains B's bounding box.
 *-----------------------------------------------------------------*/
#ifdef NOT_USED
bool
poly_contain(POLYGON *polya, POLYGON *polyb)
{
	return rect_contain(&(polya->boundbox), &(polyb->boundbox));
}

#endif

bool
poly_contain(POLYGON *polya, POLYGON *polyb)
{
	int			i;

	if (!PointerIsValid(polya) || !PointerIsValid(polyb))
		return FALSE;

	if (rect_contain(&(polya->boundbox), &(polyb->boundbox)))
	{
		for (i = 0; i < polyb->npts; i++)
		{
			if (point_inside(&(polyb->p[i]), polya->npts, &(polya->p[0])) == 0)
			{
#if GEODEBUG
				printf("poly_contain- point (%f,%f) not in polygon\n", polyb->p[i].x, polyb->p[i].y);
#endif
				return FALSE;
			}
		}
		for (i = 0; i < polya->npts; i++)
		{
			if (point_inside(&(polya->p[i]), polyb->npts, &(polyb->p[0])) == 1)
			{
#if GEODEBUG
				printf("poly_contain- point (%f,%f) in polygon\n", polya->p[i].x, polya->p[i].y);
#endif
				return FALSE;
			}
		}
		return TRUE;
	}
#if GEODEBUG
	printf("poly_contain- bound box ((%f,%f),(%f,%f)) not inside ((%f,%f),(%f,%f))\n",
		   polyb->boundbox.loW.x, polyb->boundbox.loW.y, polyb->boundbox.higH.x, polyb->boundbox.higH.y,
		   polya->boundbox.loW.x, polya->boundbox.loW.y, polya->boundbox.higH.x, polya->boundbox.higH.y);
#endif
	return FALSE;
}	/* poly_contain() */


/*-----------------------------------------------------------------
 * Determine if polygon A is contained by polygon B by determining
 * if A's bounding box is contained by B's bounding box.
 *-----------------------------------------------------------------*/
#ifdef NOT_USED
bool
poly_contained(POLYGON *polya, POLYGON *polyb)
{
	return rect_contained(&(polya->boundbox), &(polyb->boundbox));
}

#endif

bool
poly_contained(POLYGON *polya, POLYGON *polyb)
{
	return poly_contain(polyb, polya);
}	/* poly_contained() */


/* poly_contain_pt()
 * Test to see if the point is inside the polygon.
 * Code adapted from integer-based routines in
 *	Wn: A Server for the HTTP
 *	File: wn/image.c
 *	Version 1.15.1
 *	Copyright (C) 1995	<by John Franks>
 * (code offered for use by J. Franks in Linux Journal letter.)
 */

bool
poly_contain_pt(POLYGON *poly, Point *p)
{
	if (!PointerIsValid(poly) || !PointerIsValid(p))
		return FALSE;

	return point_inside(p, poly->npts, &(poly->p[0])) != 0;
}	/* poly_contain_pt() */

bool
pt_contained_poly(Point *p, POLYGON *poly)
{
	if (!PointerIsValid(p) || !PointerIsValid(poly))
		return FALSE;

	return poly_contain_pt(poly, p);
}	/* pt_contained_poly() */


double *
poly_distance(POLYGON *polya, POLYGON *polyb)
{
	double	   *result;

	if (!PointerIsValid(polya) || !PointerIsValid(polyb))
		return NULL;

	result = palloc(sizeof(double));

	*result = 0;

	return result;
}	/* poly_distance() */


/***********************************************************************
 **
 **		Routines for 2D points.
 **
 ***********************************************************************/

Point *
point(float8 *x, float8 *y)
{
	if (!(PointerIsValid(x) && PointerIsValid(y)))
		return NULL;

	return point_construct(*x, *y);
}	/* point() */


Point *
point_add(Point *p1, Point *p2)
{
	Point	   *result;

	if (!(PointerIsValid(p1) && PointerIsValid(p2)))
		return NULL;

	result = palloc(sizeof(Point));

	result->x = (p1->x + p2->x);
	result->y = (p1->y + p2->y);

	return result;
}	/* point_add() */

Point *
point_sub(Point *p1, Point *p2)
{
	Point	   *result;

	if (!(PointerIsValid(p1) && PointerIsValid(p2)))
		return NULL;

	result = palloc(sizeof(Point));

	result->x = (p1->x - p2->x);
	result->y = (p1->y - p2->y);

	return result;
}	/* point_sub() */

Point *
point_mul(Point *p1, Point *p2)
{
	Point	   *result;

	if (!(PointerIsValid(p1) && PointerIsValid(p2)))
		return NULL;

	result = palloc(sizeof(Point));

	result->x = (p1->x * p2->x) - (p1->y * p2->y);
	result->y = (p1->x * p2->y) + (p1->y * p2->x);

	return result;
}	/* point_mul() */

Point *
point_div(Point *p1, Point *p2)
{
	Point	   *result;
	double		div;

	if (!(PointerIsValid(p1) && PointerIsValid(p2)))
		return NULL;

	result = palloc(sizeof(Point));

	div = (p2->x * p2->x) + (p2->y * p2->y);

	if (div == 0.0)
		elog(ERROR, "point_div:  divide by 0.0 error");

	result->x = ((p1->x * p2->x) + (p1->y * p2->y)) / div;
	result->y = ((p2->x * p1->y) - (p2->y * p1->x)) / div;

	return result;
}	/* point_div() */


/***********************************************************************
 **
 **		Routines for 2D boxes.
 **
 ***********************************************************************/

BOX *
rect(Point *p1, Point *p2)
{
	BOX		   *result;

	if (!(PointerIsValid(p1) && PointerIsValid(p2)))
		return NULL;

	result = rect_construct(p1->x, p2->x, p1->y, p2->y);

	return result;
}	/* box() */

BOX *
rect_add(BOX *box, Point *p)
{
	BOX		   *result;

	if (!(PointerIsValid(box) && PointerIsValid(p)))
		return NULL;

	result = rect_construct((box->higH.x + p->x), (box->loW.x + p->x),
						   (box->higH.y + p->y), (box->loW.y + p->y));

	return result;
}	/* rect_add() */

BOX *
rect_sub(BOX *box, Point *p)
{
	BOX		   *result;

	if (!(PointerIsValid(box) && PointerIsValid(p)))
		return NULL;

	result = rect_construct((box->higH.x - p->x), (box->loW.x - p->x),
						   (box->higH.y - p->y), (box->loW.y - p->y));

	return result;
}	/* rect_sub() */

BOX *
rect_mul(BOX *box, Point *p)
{
	BOX		   *result;
	Point	   *higH,
			   *loW;

	if (!(PointerIsValid(box) && PointerIsValid(p)))
		return NULL;

	higH = point_mul(&box->higH, p);
	loW = point_mul(&box->loW, p);

	result = rect_construct(higH->x, loW->x, higH->y, loW->y);
	pfree(higH);
	pfree(loW);

	return result;
}	/* rect_mul() */

BOX *
rect_div(BOX *box, Point *p)
{
	BOX		   *result;
	Point	   *higH,
			   *loW;

	if (!(PointerIsValid(box) && PointerIsValid(p)))
		return NULL;

	higH = point_div(&box->higH, p);
	loW = point_div(&box->loW, p);

	result = rect_construct(higH->x, loW->x, higH->y, loW->y);
	pfree(higH);
	pfree(loW);

	return result;
}	/* rect_div() */


/***********************************************************************
 **
 **		Routines for 2D paths.
 **
 ***********************************************************************/

/* path_add()
 * Concatenate two paths (only if they are both open).
 */
PATH *
path_add(PATH *p1, PATH *p2)
{
	PATH	   *result;
	int			size;
	int			i;

	if (!(PointerIsValid(p1) && PointerIsValid(p2))
		|| p1->closed || p2->closed)
		return NULL;

	size = offsetof(PATH, p[0]) +(sizeof(p1->p[0]) * (p1->npts + p2->npts));
	result = palloc(size);

	result->size = size;
	result->npts = (p1->npts + p2->npts);
	result->closed = p1->closed;

	for (i = 0; i < p1->npts; i++)
	{
		result->p[i].x = p1->p[i].x;
		result->p[i].y = p1->p[i].y;
	}
	for (i = 0; i < p2->npts; i++)
	{
		result->p[i + p1->npts].x = p2->p[i].x;
		result->p[i + p1->npts].y = p2->p[i].y;
	}

	return result;
}	/* path_add() */

/* path_add_pt()
 * Translation operator.
 */
PATH *
path_add_pt(PATH *path, Point *point)
{
	PATH	   *result;
	int			i;

	if ((!PointerIsValid(path)) || (!PointerIsValid(point)))
		return NULL;

	result = path_copy(path);

	for (i = 0; i < path->npts; i++)
	{
		result->p[i].x += point->x;
		result->p[i].y += point->y;
	}

	return result;
}	/* path_add_pt() */

PATH *
path_sub_pt(PATH *path, Point *point)
{
	PATH	   *result;
	int			i;

	if ((!PointerIsValid(path)) || (!PointerIsValid(point)))
		return NULL;

	result = path_copy(path);

	for (i = 0; i < path->npts; i++)
	{
		result->p[i].x -= point->x;
		result->p[i].y -= point->y;
	}

	return result;
}	/* path_sub_pt() */


/* path_mul_pt()
 * Rotation and scaling operators.
 */
PATH *
path_mul_pt(PATH *path, Point *point)
{
	PATH	   *result;
	Point	   *p;
	int			i;

	if ((!PointerIsValid(path)) || (!PointerIsValid(point)))
		return NULL;

	result = path_copy(path);

	for (i = 0; i < path->npts; i++)
	{
		p = point_mul(&path->p[i], point);
		result->p[i].x = p->x;
		result->p[i].y = p->y;
		pfree(p);
	}

	return result;
}	/* path_mul_pt() */

PATH *
path_div_pt(PATH *path, Point *point)
{
	PATH	   *result;
	Point	   *p;
	int			i;

	if ((!PointerIsValid(path)) || (!PointerIsValid(point)))
		return NULL;

	result = path_copy(path);

	for (i = 0; i < path->npts; i++)
	{
		p = point_div(&path->p[i], point);
		result->p[i].x = p->x;
		result->p[i].y = p->y;
		pfree(p);
	}

	return result;
}	/* path_div_pt() */


Point *
path_center(PATH *path)
{
	Point	   *result;

	if (!PointerIsValid(path))
		return NULL;

	elog(ERROR, "path_center not implemented");

	result = palloc(sizeof(Point));
	result = NULL;

	return result;
}	/* path_center() */

POLYGON    *
path_poly(PATH *path)
{
	POLYGON    *poly;
	int			size;
	int			i;

	if (!PointerIsValid(path))
		return NULL;

	if (!path->closed)
		elog(ERROR, "Open path cannot be converted to polygon");

	size = offsetof(POLYGON, p[0]) +(sizeof(poly->p[0]) * path->npts);
	poly = palloc(size);

	poly->size = size;
	poly->npts = path->npts;

	for (i = 0; i < path->npts; i++)
	{
		poly->p[i].x = path->p[i].x;
		poly->p[i].y = path->p[i].y;
	}

	make_bound_box(poly);

	return poly;
}	/* path_polygon() */


/* upgradepath()
 * Convert path read from old-style string into correct representation.
 *
 * Old-style: '(closed,#pts,x1,y1,...)' where closed is a boolean flag
 * New-style: '((x1,y1),...)' for closed path
 *			  '[(x1,y1),...]' for open path
 */
PATH *
upgradepath(PATH *path)
{
	PATH	   *result;
	int			size,
				npts;
	int			i;

	if (!PointerIsValid(path) || (path->npts < 2))
		return NULL;

	if (!isoldpath(path))
		elog(ERROR, "upgradepath: path already upgraded?");

	npts = (path->npts - 1);
	size = offsetof(PATH, p[0]) +(sizeof(path->p[0]) * npts);
	result = palloc(size);
	MemSet((char *) result, 0, size);

	result->size = size;
	result->npts = npts;
	result->closed = (path->p[0].x != 0);

	for (i = 0; i < result->npts; i++)
	{
		result->p[i].x = path->p[i + 1].x;
		result->p[i].y = path->p[i + 1].y;
	}

	return result;
}	/* upgradepath() */

bool
isoldpath(PATH *path)
{
	if (!PointerIsValid(path) || (path->npts < 2))
		return FALSE;

	return path->npts == (path->p[0].y + 1);
}	/* isoldpath() */


/***********************************************************************
 **
 **		Routines for 2D polygons.
 **
 ***********************************************************************/

int4
poly_npoints(POLYGON *poly)
{
	if (!PointerIsValid(poly))
		return FALSE;

	return poly->npts;
}	/* poly_npoints() */


Point *
poly_center(POLYGON *poly)
{
	Point	   *result;
	CIRCLE	   *circle;

	if (!PointerIsValid(poly))
		return NULL;

	if (PointerIsValid(circle = poly_circle(poly)))
	{
		result = circle_center(circle);
		pfree(circle);

	}
	else
		result = NULL;

	return result;
}	/* poly_center() */


BOX *
poly_box(POLYGON *poly)
{
	BOX		   *box;

	if (!PointerIsValid(poly) || (poly->npts < 1))
		return NULL;

	box = rect_copy(&poly->boundbox);

	return box;
}	/* poly_box() */


/* rect_poly()
 * Convert a box to a polygon.
 */
POLYGON    *
rect_poly(BOX *box)
{
	POLYGON    *poly;
	int			size;

	if (!PointerIsValid(box))
		return NULL;

	/* map four corners of the box to a polygon */
	size = offsetof(POLYGON, p[0]) +(sizeof(poly->p[0]) * 4);
	poly = palloc(size);

	poly->size = size;
	poly->npts = 4;

	poly->p[0].x = box->loW.x;
	poly->p[0].y = box->loW.y;
	poly->p[1].x = box->loW.x;
	poly->p[1].y = box->higH.y;
	poly->p[2].x = box->higH.x;
	poly->p[2].y = box->higH.y;
	poly->p[3].x = box->higH.x;
	poly->p[3].y = box->loW.y;

	rect_fill(&poly->boundbox, box->higH.x, box->loW.x, box->higH.y, box->loW.y);

	return poly;
}	/* rect_poly() */


PATH *
poly_path(POLYGON *poly)
{
	PATH	   *path;
	int			size;
	int			i;

	if (!PointerIsValid(poly) || (poly->npts < 0))
		return NULL;

	size = offsetof(PATH, p[0]) +(sizeof(path->p[0]) * poly->npts);
	path = palloc(size);

	path->size = size;
	path->npts = poly->npts;
	path->closed = TRUE;

	for (i = 0; i < poly->npts; i++)
	{
		path->p[i].x = poly->p[i].x;
		path->p[i].y = poly->p[i].y;
	}

	return path;
}	/* poly_path() */


/* upgradepoly()
 * Convert polygon read as pre-v6.1 string to new interpretation.
 * Old-style: '(x1,x2,...,y1,y2,...)'
 * New-style: '(x1,y1,x2,y2,...)'
 */
POLYGON    *
upgradepoly(POLYGON *poly)
{
	POLYGON    *result;
	int			size;
	int			n2,
				i,
				ii;

	if (!PointerIsValid(poly) || (poly->npts < 1))
		return NULL;

	size = offsetof(POLYGON, p[0]) +(sizeof(poly->p[0]) * poly->npts);
	result = palloc(size);
	MemSet((char *) result, 0, size);

	result->size = size;
	result->npts = poly->npts;

	n2 = poly->npts / 2;

	for (i = 0; i < n2; i++)
	{
		result->p[2 * i].x = poly->p[i].x;		/* even indices */
		result->p[2 * i + 1].x = poly->p[i].y;	/* odd indices */
	}

	if ((ii = ((poly->npts % 2) ? 1 : 0)))
	{
		result->p[poly->npts - 1].x = poly->p[n2].x;
		result->p[0].y = poly->p[n2].y;
	}

	for (i = 0; i < n2; i++)
	{
		result->p[2 * i + ii].y = poly->p[i + n2 + ii].x;		/* even (+offset)
																 * indices */
		result->p[2 * i + ii + 1].y = poly->p[i + n2 + ii].y;	/* odd (+offset) indices */
	}

	return result;
}	/* upgradepoly() */

/* revertpoly()
 * Reverse effect of upgradepoly().
 */
POLYGON    *
revertpoly(POLYGON *poly)
{
	POLYGON    *result;
	int			size;
	int			n2,
				i,
				ii;

	if (!PointerIsValid(poly) || (poly->npts < 1))
		return NULL;

	size = offsetof(POLYGON, p[0]) +(sizeof(poly->p[0]) * poly->npts);
	result = palloc(size);
	MemSet((char *) result, 0, size);

	result->size = size;
	result->npts = poly->npts;

	n2 = poly->npts / 2;

	for (i = 0; i < n2; i++)
	{
		result->p[i].x = poly->p[2 * i].x;		/* even indices */
		result->p[i].y = poly->p[2 * i + 1].x;	/* odd indices */
	}

	if ((ii = ((poly->npts % 2) ? 1 : 0)))
	{
		result->p[n2].x = poly->p[poly->npts - 1].x;
		result->p[n2].y = poly->p[0].y;
	}

	for (i = 0; i < n2; i++)
	{
		result->p[i + n2 + ii].x = poly->p[2 * i + ii].y;		/* even (+offset)
																 * indices */
		result->p[i + n2 + ii].y = poly->p[2 * i + ii + 1].y;	/* odd (+offset) indices */
	}

	return result;
}	/* revertpoly() */


/***********************************************************************
 **
 **		Routines for circles.
 **
 ***********************************************************************/

/*----------------------------------------------------------
 * Formatting and conversion routines.
 *---------------------------------------------------------*/

/*		circle_in		-		convert a string to internal form.
 *
 *		External format: (center and radius of circle)
 *				"((f8,f8)<f8>)"
 *				also supports quick entry style "(f8,f8,f8)"
 */
CIRCLE *
circle_in(char *str)
{
	CIRCLE	   *circle;

	char	   *s,
			   *cp;
	int			depth = 0;

	if (!PointerIsValid(str))
		elog(ERROR, " Bad (null) circle external representation");

	circle = palloc(sizeof(CIRCLE));

	s = str;
	while (isspace(*s))
		s++;
	if ((*s == LDELIM_C) || (*s == LDELIM))
	{
		depth++;
		cp = (s + 1);
		while (isspace(*cp))
			cp++;
		if (*cp == LDELIM)
			s = cp;
	}

	if (!pair_decode(s, &circle->center.x, &circle->center.y, &s))
		elog(ERROR, "Bad circle external representation '%s'", str);

	if (*s == DELIM)
		s++;
	while (isspace(*s))
		s++;

	if ((!single_decode(s, &circle->radius, &s)) || (circle->radius < 0))
		elog(ERROR, "Bad circle external representation '%s'", str);

	while (depth > 0)
	{
		if ((*s == RDELIM)
			|| ((*s == RDELIM_C) && (depth == 1)))
		{
			depth--;
			s++;
			while (isspace(*s))
				s++;
		}
		else
			elog(ERROR, "Bad circle external representation '%s'", str);
	}

	if (*s != '\0')
		elog(ERROR, "Bad circle external representation '%s'", str);

	return circle;
}	/* circle_in() */

/*		circle_out		-		convert a circle to external form.
 */
char *
circle_out(CIRCLE *circle)
{
	char	   *result;
	char	   *cp;

	if (!PointerIsValid(circle))
		return NULL;

	result = palloc(3 * (P_MAXLEN + 1) + 3);

	cp = result;
	*cp++ = LDELIM_C;
	*cp++ = LDELIM;
	if (!pair_encode(circle->center.x, circle->center.y, cp))
		elog(ERROR, "Unable to format circle");

	cp += strlen(cp);
	*cp++ = RDELIM;
	*cp++ = DELIM;
	if (!single_encode(circle->radius, cp))
		elog(ERROR, "Unable to format circle");

	cp += strlen(cp);
	*cp++ = RDELIM_C;
	*cp = '\0';

	return result;
}	/* circle_out() */


/*----------------------------------------------------------
 *	Relational operators for CIRCLEs.
 *		<, >, <=, >=, and == are based on circle area.
 *---------------------------------------------------------*/

/*		circles identical?
 */
bool
circle_same(CIRCLE *circle1, CIRCLE *circle2)
{
	return (FPeq(circle1->radius, circle2->radius)
			&& FPeq(circle1->center.x, circle2->center.x)
			&& FPeq(circle1->center.y, circle2->center.y));
}	/* circle_same() */

/*		circle_overlap	-		does circle1 overlap circle2?
 */
bool
circle_overlap(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPle(point_dt(&circle1->center, &circle2->center), (circle1->radius + circle2->radius));
}

/*		circle_overleft -		is the right edge of circle1 to the left of
 *								the right edge of circle2?
 */
bool
circle_overleft(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPle((circle1->center.x + circle1->radius), (circle2->center.x + circle2->radius));
}

/*		circle_left		-		is circle1 strictly left of circle2?
 */
bool
circle_left(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPle((circle1->center.x + circle1->radius), (circle2->center.x - circle2->radius));
}

/*		circle_right	-		is circle1 strictly right of circle2?
 */
bool
circle_right(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPge((circle1->center.x - circle1->radius), (circle2->center.x + circle2->radius));
}

/*		circle_overright		-		is the left edge of circle1 to the right of
 *								the left edge of circle2?
 */
bool
circle_overright(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPge((circle1->center.x - circle1->radius), (circle2->center.x - circle2->radius));
}

/*		circle_contained		-		is circle1 contained by circle2?
 */
bool
circle_contained(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPle((point_dt(&circle1->center, &circle2->center) + circle1->radius), circle2->radius);
}

/*		circle_contain	-		does circle1 contain circle2?
 */
bool
circle_contain(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPle((point_dt(&circle1->center, &circle2->center) + circle2->radius), circle1->radius);
}


/*		circle_positionop		-
 *				is circle1 entirely {above,beloW} circle2?
 */
bool
circle_below(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPle((circle1->center.y + circle1->radius), (circle2->center.y - circle2->radius));
}

bool
circle_above(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPge((circle1->center.y - circle1->radius), (circle2->center.y + circle2->radius));
}


/*		circle_relop	-		is area(circle1) relop area(circle2), within
 *								our accuracy constraint?
 */
bool
circle_eq(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPeq(circle_ar(circle1), circle_ar(circle2));
}	/* circle_eq() */

bool
circle_ne(CIRCLE *circle1, CIRCLE *circle2)
{
	return !circle_eq(circle1, circle2);
}	/* circle_ne() */

bool
circle_lt(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPlt(circle_ar(circle1), circle_ar(circle2));
}	/* circle_lt() */

bool
circle_gt(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPgt(circle_ar(circle1), circle_ar(circle2));
}	/* circle_gt() */

bool
circle_le(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPle(circle_ar(circle1), circle_ar(circle2));
}	/* circle_le() */

bool
circle_ge(CIRCLE *circle1, CIRCLE *circle2)
{
	return FPge(circle_ar(circle1), circle_ar(circle2));
}	/* circle_ge() */


/*----------------------------------------------------------
 *	"Arithmetic" operators on circles.
 *		circle_foo		returns foo as an object (pointer) that
 can be passed between languages.
 *		circle_xx		is an internal routine which returns the
 *						actual value.
 *---------------------------------------------------------*/

static CIRCLE *
circle_copy(CIRCLE *circle)
{
	CIRCLE	   *result;

	if (!PointerIsValid(circle))
		return NULL;

	result = palloc(sizeof(CIRCLE));

	memmove((char *) result, (char *) circle, sizeof(CIRCLE));
	return result;
}	/* circle_copy() */


/* circle_add_pt()
 * Translation operator.
 */
CIRCLE *
circle_add_pt(CIRCLE *circle, Point *point)
{
	CIRCLE	   *result;

	if (!PointerIsValid(circle) || !PointerIsValid(point))
		return NULL;

	result = circle_copy(circle);

	result->center.x += point->x;
	result->center.y += point->y;

	return result;
}	/* circle_add_pt() */

CIRCLE *
circle_sub_pt(CIRCLE *circle, Point *point)
{
	CIRCLE	   *result;

	if (!PointerIsValid(circle) || !PointerIsValid(point))
		return NULL;

	result = circle_copy(circle);

	result->center.x -= point->x;
	result->center.y -= point->y;

	return result;
}	/* circle_sub_pt() */


/* circle_mul_pt()
 * Rotation and scaling operators.
 */
CIRCLE *
circle_mul_pt(CIRCLE *circle, Point *point)
{
	CIRCLE	   *result;
	Point	   *p;

	if (!PointerIsValid(circle) || !PointerIsValid(point))
		return NULL;

	result = circle_copy(circle);

	p = point_mul(&circle->center, point);
	result->center.x = p->x;
	result->center.y = p->y;
	pfree(p);
	result->radius *= HYPOT(point->x, point->y);

	return result;
}	/* circle_mul_pt() */

CIRCLE *
circle_div_pt(CIRCLE *circle, Point *point)
{
	CIRCLE	   *result;
	Point	   *p;

	if (!PointerIsValid(circle) || !PointerIsValid(point))
		return NULL;

	result = circle_copy(circle);

	p = point_div(&circle->center, point);
	result->center.x = p->x;
	result->center.y = p->y;
	pfree(p);
	result->radius /= HYPOT(point->x, point->y);

	return result;
}	/* circle_div_pt() */


/*		circle_area		-		returns the area of the circle.
 */
double *
circle_area(CIRCLE *circle)
{
	double	   *result;

	result = palloc(sizeof(double));
	*result = circle_ar(circle);

	return result;
}


/*		circle_diameter -		returns the diameter of the circle.
 */
double *
circle_diameter(CIRCLE *circle)
{
	double	   *result;

	result = palloc(sizeof(double));
	*result = (2 * circle->radius);

	return result;
}


/*		circle_radius	-		returns the radius of the circle.
 */
double *
circle_radius(CIRCLE *circle)
{
	double	   *result;

	result = palloc(sizeof(double));
	*result = circle->radius;

	return result;
}


/*		circle_distance -		returns the distance between
 *								  two circles.
 */
double *
circle_distance(CIRCLE *circle1, CIRCLE *circle2)
{
	double	   *result;

	result = palloc(sizeof(double));
	*result = (point_dt(&circle1->center, &circle2->center)
			   - (circle1->radius + circle2->radius));
	if (*result < 0)
		*result = 0;

	return result;
}	/* circle_distance() */


bool
circle_contain_pt(CIRCLE *circle, Point *point)
{
	bool		within;
	double	   *d;

	if (!PointerIsValid(circle) || !PointerIsValid(point))
		return FALSE;

	d = point_distance(&(circle->center), point);
	within = (*d <= circle->radius);
	pfree(d);

	return within;
}	/* circle_contain_pt() */


bool
pt_contained_circle(Point *point, CIRCLE *circle)
{
	return circle_contain_pt(circle, point);
}	/* circle_contain_pt() */


/*		dist_pc -		returns the distance between
 *						  a point and a circle.
 */
double *
dist_pc(Point *point, CIRCLE *circle)
{
	double	   *result;

	result = palloc(sizeof(double));

	*result = (point_dt(point, &circle->center) - circle->radius);
	if (*result < 0)
		*result = 0;

	return result;
}	/* dist_pc() */


/*		circle_center	-		returns the center point of the circle.
 */
Point *
circle_center(CIRCLE *circle)
{
	Point	   *result;

	result = palloc(sizeof(Point));
	result->x = circle->center.x;
	result->y = circle->center.y;

	return result;
}


/*		circle_ar		-		returns the area of the circle.
 */
static double
circle_ar(CIRCLE *circle)
{
	return PI * (circle->radius * circle->radius);
}


/*		circle_dt		-		returns the distance between the
 *						  center points of two circlees.
 */
#ifdef NOT_USED
double
circle_dt(CIRCLE *circle1, CIRCLE *circle2)
{
	double		result;

	result = point_dt(&circle1->center, &circle2->center);

	return result;
}

#endif

/*----------------------------------------------------------
 *	Conversion operators.
 *---------------------------------------------------------*/

CIRCLE *
circle(Point *center, float8 *radius)
{
	CIRCLE	   *result;

	if (!(PointerIsValid(center) && PointerIsValid(radius)))
		return NULL;

	result = palloc(sizeof(CIRCLE));

	result->center.x = center->x;
	result->center.y = center->y;
	result->radius = *radius;

	return result;
}


BOX *
circle_box(CIRCLE *circle)
{
	BOX		   *box;
	double		delta;

	if (!PointerIsValid(circle))
		return NULL;

	box = palloc(sizeof(BOX));

	delta = circle->radius / sqrt(2.0e0);

	box->higH.x = circle->center.x + delta;
	box->loW.x = circle->center.x - delta;
	box->higH.y = circle->center.y + delta;
	box->loW.y = circle->center.y - delta;

	return box;
}	/* circle_box() */

/* rect_circle()
 * Convert a box to a circle.
 */
CIRCLE *
rect_circle(BOX *box)
{
	CIRCLE	   *circle;

	if (!PointerIsValid(box))
		return NULL;

	circle = palloc(sizeof(CIRCLE));

	circle->center.x = (box->higH.x + box->loW.x) / 2;
	circle->center.y = (box->higH.y + box->loW.y) / 2;

	circle->radius = point_dt(&circle->center, &box->higH);

	return circle;
}	/* rect_circle() */


POLYGON    *
circle_poly(int npts, CIRCLE *circle)
{
	POLYGON    *poly;
	int			size;
	int			i;
	double		angle;

	if (!PointerIsValid(circle))
		return NULL;

	if (FPzero(circle->radius) || (npts < 2))
		elog(ERROR, "Unable to convert circle to polygon");

	size = offsetof(POLYGON, p[0]) +(sizeof(poly->p[0]) * npts);
	poly = palloc(size);

	MemSet((char *) poly, 0, size);		/* zero any holes */
	poly->size = size;
	poly->npts = npts;

	for (i = 0; i < npts; i++)
	{
		angle = i * (2 * PI / npts);
		poly->p[i].x = circle->center.x - (circle->radius * cos(angle));
		poly->p[i].y = circle->center.y + (circle->radius * sin(angle));
	}

	make_bound_box(poly);

	return poly;
}

/*		poly_circle		- convert polygon to circle
 *
 * XXX This algorithm should use weighted means of line segments
 *	rather than straight average values of points - tgl 97/01/21.
 */
CIRCLE *
poly_circle(POLYGON *poly)
{
	CIRCLE	   *circle;
	int			i;

	if (!PointerIsValid(poly))
		return NULL;

	if (poly->npts < 2)
		elog(ERROR, "Unable to convert polygon to circle");

	circle = palloc(sizeof(CIRCLE));

	circle->center.x = 0;
	circle->center.y = 0;
	circle->radius = 0;

	for (i = 0; i < poly->npts; i++)
	{
		circle->center.x += poly->p[i].x;
		circle->center.y += poly->p[i].y;
	}
	circle->center.x /= poly->npts;
	circle->center.y /= poly->npts;

	for (i = 0; i < poly->npts; i++)
		circle->radius += point_dt(&poly->p[i], &circle->center);
	circle->radius /= poly->npts;

	if (FPzero(circle->radius))
		elog(ERROR, "Unable to convert polygon to circle");

	return circle;
}	/* poly_circle() */


/***********************************************************************
 **
 **		Private routines for multiple types.
 **
 ***********************************************************************/

#define HIT_IT INT_MAX

static int
point_inside(Point *p, int npts, Point *plist)
{
	double		x0,
				y0;
	double		px,
				py;

	int			i;
	double		x,
				y;
	int			cross,
				crossnum;

/*
 * We calculate crossnum, which is twice the crossing number of a
 * ray from the origin parallel to the positive X axis.
 * A coordinate change is made to move the test point to the origin.
 * Then the function lseg_crossing() is called to calculate the crossnum of
 * one segment of the translated polygon with the ray which is the
 * positive X-axis.
 */

	crossnum = 0;
	i = 0;
	if (npts <= 0)
		return 0;

	x0 = plist[0].x - p->x;
	y0 = plist[0].y - p->y;

	px = x0;
	py = y0;
	for (i = 1; i < npts; i++)
	{
		x = plist[i].x - p->x;
		y = plist[i].y - p->y;

		if ((cross = lseg_crossing(x, y, px, py)) == HIT_IT)
			return 2;
		crossnum += cross;

		px = x;
		py = y;
	}
	if ((cross = lseg_crossing(x0, y0, px, py)) == HIT_IT)
		return 2;
	crossnum += cross;
	if (crossnum != 0)
		return 1;
	return 0;
}	/* point_inside() */


/* lseg_crossing()
 * The function lseg_crossing() returns +2, or -2 if the segment from (x,y)
 * to previous (x,y) crosses the positive X-axis positively or negatively.
 * It returns +1 or -1 if one endpoint is on this ray, or 0 if both are.
 * It returns 0 if the ray and the segment don't intersect.
 * It returns HIT_IT if the segment contains (0,0)
 */

static int
lseg_crossing(double x, double y, double px, double py)
{
	double		z;
	int			sgn;

	/* If (px,py) = (0,0) and not first call we have already sent HIT_IT */

	if (FPzero(y))
	{
		if (FPzero(x))
		{
			return HIT_IT;

		}
		else if (FPgt(x, 0))
		{
			if (FPzero(py))
				return FPgt(px, 0) ? 0 : HIT_IT;
			return FPlt(py, 0) ? 1 : -1;

		}
		else
		{						/* x < 0 */
			if (FPzero(py))
				return FPlt(px, 0) ? 0 : HIT_IT;
			return 0;
		}
	}

	/* Now we know y != 0;	set sgn to sign of y */
	sgn = (FPgt(y, 0) ? 1 : -1);
	if (FPzero(py))
		return FPlt(px, 0) ? 0 : sgn;

	if (FPgt((sgn * py), 0))
	{							/* y and py have same sign */
		return 0;

	}
	else
	{							/* y and py have opposite signs */
		if (FPge(x, 0) && FPgt(px, 0))
			return 2 * sgn;
		if (FPlt(x, 0) && FPle(px, 0))
			return 0;

		z = (x - px) * y - (y - py) * x;
		if (FPzero(z))
			return HIT_IT;
		return FPgt((sgn * z), 0) ? 0 : 2 * sgn;
	}
}	/* lseg_crossing() */


static bool
plist_same(int npts, Point *p1, Point *p2)
{
	int			i,
				ii,
				j;

	/* find match for first point */
	for (i = 0; i < npts; i++)
	{
		if ((FPeq(p2[i].x, p1[0].x))
			&& (FPeq(p2[i].y, p1[0].y)))
		{

			/* match found? then look forward through remaining points */
			for (ii = 1, j = i + 1; ii < npts; ii++, j++)
			{
				if (j >= npts)
					j = 0;
				if ((!FPeq(p2[j].x, p1[ii].x))
					|| (!FPeq(p2[j].y, p1[ii].y)))
				{
#ifdef GEODEBUG
					printf("plist_same- %d failed forward match with %d\n", j, ii);
#endif
					break;
				}
			}
#ifdef GEODEBUG
			printf("plist_same- ii = %d/%d after forward match\n", ii, npts);
#endif
			if (ii == npts)
				return TRUE;

			/* match not found forwards? then look backwards */
			for (ii = 1, j = i - 1; ii < npts; ii++, j--)
			{
				if (j < 0)
					j = (npts - 1);
				if ((!FPeq(p2[j].x, p1[ii].x))
					|| (!FPeq(p2[j].y, p1[ii].y)))
				{
#ifdef GEODEBUG
					printf("plist_same- %d failed reverse match with %d\n", j, ii);
#endif
					break;
				}
			}
#ifdef GEODEBUG
			printf("plist_same- ii = %d/%d after reverse match\n", ii, npts);
#endif
			if (ii == npts)
				return TRUE;
		}
	}

	return FALSE;
}	/* plist_same() */
