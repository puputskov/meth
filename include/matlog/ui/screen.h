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
 * The result of a line (ed->results) is shown dim after its text, "= 7/2", or
 * in red for an error, "! division by zero". The text of the line has
 * priority: the result gets the columns that are left, two spaces after the
 * text, and is cut to them (nothing is drawn when fewer than 4 are left).
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

/* What a screen row shows, for finding the text under the mouse. */
typedef struct MlRowInfo
{
	int32_t  kind;      /* 0: no text of a line, 1: a line, 2: a folded block */
	uint64_t src;       /* the session line */
	uint64_t text_col;  /* 0 based column where the text starts */
	uint64_t first_col; /* the display column of the text shown there */
} MlRowInfo;

ML_VEC_DEFINE (MlRowInfoVec, MlRowInfo)

typedef struct MlLayout
{
	MlResult    result;
	MlRowVec    rows; /* exactly screen.rows strings, owned by the caller */
	MlRowInfoVec info; /* one per row */
	MlCursorPos cursor;
} MlLayout;

void ml_screen_init (MlScreen *s, uint32_t cols, uint32_t rows);
void ml_screen_free (MlScreen *s);

/* A new terminal size: everything is redrawn next time. */
void ml_screen_resize (MlScreen *s, uint32_t cols, uint32_t rows);

/* Updates s.top so the focused line is visible and builds all rows. */
MlLayout ml_screen_layout (MlScreen *s, const MlEditor *ed);

void ml_layout_free (MlLayout *layout);

/* The text a click at a terminal cell (1 based) hits. */
MlHit ml_screen_hit (
	MlScreen       *s,
	const MlEditor *ed,
	uint32_t        col,
	uint32_t        row
);

/* Appends the VT output that brings the terminal up to date. */
MlResult ml_screen_render (MlScreen *s, MlStr *out, const MlEditor *ed);

#endif /* MATLOG_UI_SCREEN_H */
