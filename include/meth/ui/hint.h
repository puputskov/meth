#ifndef METH_UI_HINT_H
#define METH_UI_HINT_H

#include <meth/core/slice.h>

/*
 * The hints shown in the middle of an empty editor, one at a time, the next
 * one after a while. A hint is a line of text in which the parts between
 * braces are the important ones (keys, commands, examples): the screen draws
 * the text as ghost text and those parts in the colour of the hint-key class.
 *
 *	"{Ctrl+P} opens the command line"
 *
 * The braces are not drawn. Hints are plain ASCII. A key is never written in
 * a hint but as the action it stands for, {ACTION_UNDO}, which the screen
 * expands with the key table of the user (ml_keymap_expand in keymap.h).
 */

/* Seconds a hint stays before the next one. */
#define ML_HINT_SECONDS 8

uint64_t ml_hint_count (void);

/* Hint n; n wraps around, so a counter that only grows can be passed. */
MlSlice ml_hint_at (uint64_t n);

/* The columns a hint takes: its length without the braces. */
uint64_t ml_hint_width (MlSlice hint);

#endif /* METH_UI_HINT_H */
