#ifndef METH_UI_PLOT_H
#define METH_UI_PLOT_H

#include <meth/core/str.h>
#include <meth/math/plotdata.h>

/*
 * Plots drawn with characters (plan section 4.6). The functions here know
 * nothing of the editor: they take numbers and a size in columns and rows and
 * append the picture to a string, rows separated by "\n" (no newline after the
 * last one), so a panel or a test can cut it into lines.
 *
 *	   12 │        ▄▄▄
 *	      │   ▄▄▄  ███  ▄▄▄
 *	      │   ███  ███  ███
 *	    0 └──────────────────
 *	        1             9
 *
 * A histogram uses block characters (eight levels per row), a scatter plot and
 * a line plot braille dots (2 by 4 per cell). The size is the whole picture,
 * labels included; it grows to the smallest size that is still readable.
 * Values that are not finite are left out.
 */

/*
 * Bars for the counts, left to right; lo and hi are the data range written
 * under the axis.
 */
MlResult ml_plot_histogram (
	MlStr              *out,
	const MlDoubleVec *counts,
	double             lo,
	double             hi,
	uint64_t           width,
	uint64_t           height
);

/*
 * The points (xs [i], ys [i]) for the shorter of the two lists. With connect
 * set a line joins each point to the next one, else only the points show.
 */
MlResult ml_plot_xy (
	MlStr              *out,
	const MlDoubleVec *xs,
	const MlDoubleVec *ys,
	int32_t            connect,
	uint64_t           width,
	uint64_t           height
);

/* The picture of a plot value, by its kind (ML_PLOT_NONE draws nothing). */
MlResult ml_plot_draw (
	MlStr             *out,
	const MlPlotOwned *plot,
	uint64_t          width,
	uint64_t          height
);

#endif /* METH_UI_PLOT_H */
