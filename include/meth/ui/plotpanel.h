#ifndef METH_UI_PLOTPANEL_H
#define METH_UI_PLOTPANEL_H

#include <meth/ui/watchpanel.h>

/*
 * The plot panel (plan section 4.6): the plots of the lines that are switched
 * on (F7 or :plot on the line), drawn one above the other in the panel at the
 * right of the editor, where the watch window is too (the plots take the
 * upper two thirds then). The editor itself only shows the one line summary
 * of a plot.
 *
 *	*plot  line 5  histogram of 40 values in 7 bins
 *	12 │        ▄▄▄
 *	   │   ▄▄▄  ███  ▄▄▄
 *	 0 └──────────────────
 *
 * A star marks the plot of the line of the cursor. Plots that do not fit
 * (each needs ML_PLOT_PANEL_MIN rows) end in "+N more".
 */

#define ML_PLOT_PANEL_MIN 7

/* How many lines have their plot switched on and a plot to show. */
uint64_t ml_plot_panel_count (const MlEditor *ed);

/* Appends `rows` rows of exactly `width` columns each (styled text) to out. */
MlResult ml_plot_panel (
	MlRowVec       *out,
	const MlEditor *ed,
	uint32_t       width,
	uint32_t       rows
);

#endif /* METH_UI_PLOTPANEL_H */
