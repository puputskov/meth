#ifndef MATLOG_UI_COMPLETE_H
#define MATLOG_UI_COMPLETE_H

#include <matlog/core/slice.h>
#include <matlog/math/env.h>

/*
 * Completion of the command line. Given the text typed after ':' it finds the
 * word being typed (a command name, or the first argument of a command that
 * has known values) and the candidates that start with what has been typed.
 *
 *	:mo       one candidate "mode": ghost text "de", accepting types it
 *	:mode     (accepting adds a space, as the command needs an argument)
 *	:mode     the values of the argument are listed: algebra lambda ...
 *	:mode l   only "lambda" is left: ghost text "ambda", no list any more
 *	:mode lambda   nothing to complete
 *
 * The candidate list is meant to be shown while it has two or more entries;
 * the ghost text is the rest of the first candidate that extends the typed
 * word, and only exists once the word has at least one character.
 */

#define ML_MAX_CANDIDATES 64

typedef struct MlCompletion
{
	uint64_t token_start;  /* byte offset of the word being completed */
	uint64_t count;        /* number of candidates */
	MlSlice  items [ML_MAX_CANDIDATES];
	uint64_t ghost;        /* index of the candidate behind the ghost text */
	MlSlice  ghost_text;   /* the rest of that candidate, empty when none */
	int32_t  append_space; /* accepting also types a space */
} MlCompletion;

/* No candidates at all. */
MlCompletion ml_completion_none (void);

/* line is the text after ':' up to the cursor. */
MlCompletion ml_complete_command (MlSlice line);

/*
 * The same, with the names of the theme files of the user (the file names of
 * name.theme, one per line) offered after :theme. The text must stay valid
 * as long as the completion is used.
 */
MlCompletion ml_complete_command_with (MlSlice line, MlSlice theme_files);

/*
 * Completion of a math line. text is the line up to the cursor; the word being
 * typed is the name at its end (digits in front of it, as in 2x, are not part
 * of it). The candidates are the names that start with the word and are longer
 * than it: variables and functions of the environment as of the snapshot
 * (newest first, each name once), then the built in and symbolic functions,
 * the constants pi and e, and, when units is 1 (the physics domain), the unit
 * names. The ghost text is the rest of the
 * first one. Nothing is offered for an empty word.
 */
MlCompletion ml_complete_math (
	const MlEnv *env,
	uint64_t    snapshot,
	MlSlice     text,
	int32_t     units
);

#endif /* MATLOG_UI_COMPLETE_H */
