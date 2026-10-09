#ifndef MATLOG_UI_LINEEDIT_H
#define MATLOG_UI_LINEEDIT_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>
#include <matlog/platform/key.h>

/*
 * Single line UTF-8 text editor. The cursor is a byte offset that always sits
 * on a code point boundary, and every movement or deletion works on whole
 * code points. Column widths come from ml_cp_width, so wide characters and
 * combining marks are handled when drawing.
 */

typedef struct MlLineEdit
{
	MlStr    text;
	uint64_t cursor; /* byte offset into text */
} MlLineEdit;

typedef struct MlEditResult
{
	MlResult result;
	/* 1 when the key was an editing key and has been applied */
	int32_t handled;
} MlEditResult;

void ml_lineedit_init (MlLineEdit *e);
void ml_lineedit_free (MlLineEdit *e);
void ml_lineedit_clear (MlLineEdit *e);

/* Replaces the text and puts the cursor at the end. */
MlResult ml_lineedit_set (MlLineEdit *e, MlSlice text);

/* Inserts at the cursor and moves the cursor after the inserted text. */
MlResult ml_lineedit_insert (MlLineEdit *e, MlSlice text);
MlResult ml_lineedit_insert_cp (MlLineEdit *e, uint32_t cp);

/* Deletion; all of these do nothing when there is nothing to delete. */
void ml_lineedit_backspace (MlLineEdit *e);
void ml_lineedit_delete (MlLineEdit *e);
void ml_lineedit_delete_word_left (MlLineEdit *e);
void ml_lineedit_delete_word_right (MlLineEdit *e);
void ml_lineedit_kill_to_start (MlLineEdit *e);
void ml_lineedit_kill_to_end (MlLineEdit *e);

/* Movement, clamped to the ends of the text. */
void ml_lineedit_left (MlLineEdit *e);
void ml_lineedit_right (MlLineEdit *e);
void ml_lineedit_home (MlLineEdit *e);
void ml_lineedit_end (MlLineEdit *e);
void ml_lineedit_word_left (MlLineEdit *e);
void ml_lineedit_word_right (MlLineEdit *e);

/* Terminal columns of the whole text, and of the text before the cursor. */
uint64_t ml_lineedit_width (const MlLineEdit *e);
uint64_t ml_lineedit_cursor_col (const MlLineEdit *e);

/*
 * Applies an editing key: printable characters, Left/Right/Home/End,
 * Backspace/Delete (with Ctrl for words), Ctrl+A/E/K/U/W. Other keys
 * (Enter, Esc, Up, Down, ...) are reported as not handled.
 */
MlEditResult ml_lineedit_handle_key (MlLineEdit *e, MlKey key);

/*
 * Appends the VT sequence that redraws the line: carriage return, prompt,
 * the visible part of the text, erase to the end of the line, and cursor
 * placement. prompt_width is the number of columns the prompt takes (the
 * prompt itself may contain escape sequences). When the text is wider than
 * the terminal it scrolls horizontally so the cursor stays visible.
 */
MlResult ml_lineedit_render (
	MlStr            *out,
	const MlLineEdit *e,
	MlSlice          prompt,
	uint32_t         prompt_width,
	uint32_t         cols
);

typedef struct MlTextWindow
{
	MlResult result;
	uint64_t first_col; /* column of the first character that was shown */
} MlTextWindow;

/*
 * Appends the part of text that fits into avail columns, starting at
 * column start_col. Wide characters are never cut in half and combining
 * marks stay with their character.
 */
MlTextWindow ml_text_window (
	MlStr    *out,
	MlSlice  text,
	uint64_t start_col,
	uint64_t avail
);

/* First column to show so that the cursor stays inside avail columns. */
uint64_t ml_lineedit_start_col (const MlLineEdit *e, uint64_t avail);

#endif /* MATLOG_UI_LINEEDIT_H */
