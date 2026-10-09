#ifndef MATLOG_TEST_KEYS_H
#define MATLOG_TEST_KEYS_H

#include <matlog/ui/editor.h>

/* Presses one key with modifiers. */
void test_key (MlEditor *ed, MlKeyCode code, uint32_t cp, uint32_t mods);

/* Types text as characters. */
void test_text (MlEditor *ed, const char *text);

/*
 * Esc as a vim like editor had it: the line typed is kept; an empty new
 * line is dropped and the cursor goes to the line before.
 */
void test_leave (MlEditor *ed);

/* 1 when a math line or a note is being edited (not select, not command). */
int32_t test_editing (const MlEditor *ed);

/*
 * Line commands written in the short form of a vim like editor, turned into
 * the keys of the free form editor, so tests can say what they mean:
 *	j k  Up Down      gg G  first / last line (5G, 5gg: line 5)
 *	dd yy p  cut, copy and paste lines (3dd: three lines)
 *	o N  a new math line / note below      i n  above
 *	u  undo     za  fold the block     zR zM  unfold / fold all
 *	]] [[  next / previous block       s  steps
 *	v  select the line (then j k extend, d y p act on the selection)
 *	gm ga ...  the domain of new lines    :cmd  a command line command
 */
void test_normal (MlEditor *ed, const char *keys);

#endif /* MATLOG_TEST_KEYS_H */
