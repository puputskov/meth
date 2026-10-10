#ifndef METH_UI_TUTORIALPANEL_H
#define METH_UI_TUTORIALPANEL_H

#include <meth/ui/watchpanel.h>

/*
 * The lesson panel of the tutorial (plan section 4.9), drawn above the lines
 * of the editor: the title with the place in the tutorial, the task of the
 * step, a hint after a few keys that did not do it, and how to leave. Its
 * height is fixed, ML_TUTORIAL_PANEL_ROWS, and a terminal with fewer than
 * ML_TUTORIAL_MIN_ROWS rows shows no panel.
 */

#define ML_TUTORIAL_PANEL_ROWS 6
#define ML_TUTORIAL_MIN_ROWS   12

/* The rows the panel takes on a terminal of this many rows, 0 for none. */
uint32_t ml_tutorial_panel_height (const MlEditor *ed, uint32_t rows);

/* Appends `height` rows of exactly `cols` columns to out. */
MlResult ml_tutorial_panel (
	MlRowVec       *out,
	const MlEditor *ed,
	uint32_t       cols,
	uint32_t       height
);

#endif /* METH_UI_TUTORIALPANEL_H */
