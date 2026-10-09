#ifndef MATLOG_SESSION_OPTIONS_H
#define MATLOG_SESSION_OPTIONS_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>

/*
 * The settings the program knows (plan section 4.15): a name, the section of
 * the settings file it is kept in, the values it takes and its default. The
 * editor (`:set`) and the command line (`matlog config`) read the same table,
 * so a setting is added in one place. Names are unique over all sections.
 *
 *	theme      [display]  a theme name; the default follows the terminal
 *	colors     [display]  24bit, 256, 16 or off; the default follows the
 *	                      terminal
 *	highlight  [display]  on or off: syntax colours
 *	ghost      [display]  on or off: the preview of the result while typing
 *
 * Bool settings are written on or off in the file and also read as true,
 * false, yes, no, 1 and 0.
 */

typedef enum MlOptionKind
{
	ML_OPTION_TEXT = 0, /* any non empty word */
	ML_OPTION_BOOL,
	ML_OPTION_CHOICE    /* one of the words in values */
} MlOptionKind;

typedef struct MlOption
{
	const char   *name;
	const char   *section;
	MlOptionKind kind;
	const char   *values;  /* ML_OPTION_CHOICE: the words, separated by | */
	const char   *initial; /* the default, "auto" when the terminal decides */
	const char   *summary;
} MlOption;

uint64_t ml_option_count (void);
MlOption ml_option_at (uint64_t index);

/* The index of the setting with the name; ml_option_count () when none. */
uint64_t ml_option_find (MlSlice name);

/*
 * Checks a value for the setting and appends its canonical form to out (a
 * bool as on or off). ML_ERR_PARSE when the value is not one for it.
 */
MlResult ml_option_check (uint64_t index, MlSlice value, MlStr *out);

/* 1 for on, 0 for off, -1 when the text is no bool. */
int32_t ml_option_bool (MlSlice value);

#endif /* MATLOG_SESSION_OPTIONS_H */
