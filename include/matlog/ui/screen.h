#ifndef MATLOG_UI_SCREEN_H
#define MATLOG_UI_SCREEN_H

#include <matlog/core/vec.h>
#include <matlog/ui/editor.h>

/*
 * Drawing of the editor on a terminal of cols x rows cells. The last row is
 * the status line (or the command line), the rows above it show the lines of
 * the session starting at `top`, scrolled just far enough to keep the focused
 * line visible. Each line has a gutter with its domain (or # for notes); the
 * cursor line of the Normal state is highlighted. Text that does not fit is
 * clipped, the line being typed scrolls horizontally with its cursor.
 *
 * ml_screen_layout builds the rows as strings. ml_screen_render turns the
 * rows that differ from the last drawn frame into VT output, so a keystroke
 * redraws a row or two instead of the whole screen.
 */

ML_VEC_DEFINE (MlRowVec, MlStr)

#define ML_GUTTER_WIDTH 5

typedef struct MlScreen
{
	uint32_t cols;
	uint32_t rows;
	uint64_t top;   /* index of the first visible line */
	MlRowVec drawn; /* what the terminal shows now, one string per row */
	int32_t  force; /* redraw every row the next time */
} MlScreen;

/* 1 based terminal position. */
typedef struct MlCursorPos
{
	uint32_t row;
	uint32_t col;
} MlCursorPos;

typedef struct MlLayout
{
	MlResult    result;
	MlRowVec    rows; /* exactly screen.rows strings, owned by the caller */
	MlCursorPos cursor;
} MlLayout;

void ml_screen_init (MlScreen *s, uint32_t cols, uint32_t rows);
void ml_screen_free (MlScreen *s);

/* A new terminal size: everything is redrawn next time. */
void ml_screen_resize (MlScreen *s, uint32_t cols, uint32_t rows);

/* Updates s.top so the focused line is visible and builds all rows. */
MlLayout ml_screen_layout (MlScreen *s, const MlEditor *ed);

void ml_layout_free (MlLayout *layout);

/* Appends the VT output that brings the terminal up to date. */
MlResult ml_screen_render (MlScreen *s, MlStr *out, const MlEditor *ed);

#endif /* MATLOG_UI_SCREEN_H */
