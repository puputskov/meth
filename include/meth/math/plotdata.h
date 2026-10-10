#ifndef METH_MATH_PLOTDATA_H
#define METH_MATH_PLOTDATA_H

#include <meth/core/mem.h>
#include <meth/core/str.h>
#include <meth/core/vec.h>

/*
 * The numbers behind a plot (plan section 4.6), what the statistics functions
 * `hist`, `scatter` and `plot` give. Drawing them is ui/plot.h.
 */

typedef enum MlPlotKind
{
	ML_PLOT_NONE = 0,
	ML_PLOT_HISTOGRAM, /* ys: the count of each bin, lo..hi: the data range */
	ML_PLOT_SCATTER,   /* the points (xs [i], ys [i]) */
	ML_PLOT_LINE       /* the same points, each joined to the next */
} MlPlotKind;

ML_VEC_DEFINE (MlDoubleVec, double)

/* In the memory of an arena, as a value carries it. */
typedef struct MlPlotData
{
	MlPlotKind    kind;
	uint64_t      count; /* values of a histogram, points of the others */
	uint64_t      n;     /* length of ys (and of xs, when there is one) */
	const double *xs;
	const double *ys;
	double        lo;
	double        hi;
} MlPlotData;

/* The same, owned: the results keep it for the panel. */
typedef struct MlPlotOwned
{
	MlPlotKind  kind;
	uint64_t    count;
	double      lo;
	double      hi;
	MlDoubleVec xs;
	MlDoubleVec ys;
} MlPlotOwned;

void ml_plot_owned_init (MlPlotOwned *p);
void ml_plot_owned_free (MlPlotOwned *p);

/* Replaces the contents of p with a copy of the data. */
MlResult ml_plot_owned_set (MlPlotOwned *p, const MlPlotData *data);

/* "histogram of 40 values in 7 bins", "scatter plot of 12 points" ... */
MlResult ml_plot_summary (MlStr *out, const MlPlotData *data);

#endif /* METH_MATH_PLOTDATA_H */
