#ifndef MATLOG_UI_WATCHPANEL_H
#define MATLOG_UI_WATCHPANEL_H

#include <matlog/ui/screen.h>

/*
 * The watch window (plan section 4.21) as drawn: a panel at the right of the
 * editor with the names defined at the line of the cursor, their type and
 * their value (see session/watch.h). F4 or :watch shows and hides it. The
 * panel takes columns from the editor; a terminal that is too narrow for it
 * (fewer than 70 columns) shows no panel.
 *
 *	 watch  line 5  (4)
 *	*a     exact    5
 *	 A     matrix 2x2  [[1, 2], [3, 4]]
 *	 f     f (x)    f(x) = x^2
 *
 * A star marks what the line of the cursor defined or changed. Rows that do
 * not fit end in "+N more".
 */

/* The width of the panel on a terminal of cols columns, 0 for none. */
uint32_t ml_watch_width (uint32_t cols);

/*
 * Appends `rows` rows of exactly `width` columns each (styled text) to out:
 * the header, then one row for each name.
 */
MlResult ml_watch_panel (
	MlRowVec       *out,
	const MlEditor *ed,
	uint32_t       width,
	uint32_t       rows
);

/* The columns a row takes on the screen, escape sequences left out. */
uint64_t ml_row_width (MlSlice row);

#endif /* MATLOG_UI_WATCHPANEL_H */
