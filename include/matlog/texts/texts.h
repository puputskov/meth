#ifndef MATLOG_TEXTS_TEXTS_H
#define MATLOG_TEXTS_TEXTS_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>

/*
 * The texts a user reads (plan section 4.24) are data: the code asks for a
 * code such as `errors.div_zero` and gets the text of the language in use. A
 * catalog is a plain UTF-8 file, `.mltext`, in the form of the settings file:
 *
 *	# a comment
 *	[errors]
 *	div_zero = division by zero
 *	undefined_name = undefined name '{name}'
 *	[hints]
 *	1 = Just type: a line is a note, {ACTION_BLOCK_NEW} starts a math
 *	    block
 *
 * A heading `[section]` gives its name to the entries after it, the code of
 * an entry is `section.name`. A line that starts with white space continues
 * the entry above it (joined with one space), `\n` in a text is a line break
 * and `\\` a backslash. `{name}` is a place for a value (ml_text_fill) or for
 * the key of an action (ml_keymap_expand).
 *
 * The English catalog is texts/en.mltext, compiled into the program (source/
 * matlog/texts/catalog_data.c, made by tools/gen_docs.py), so the tool works
 * with nothing next to it. ml_texts_reload also reads the file
 * texts/<language>.mltext of the settings folder (the setting `language`,
 * default en) over it: an entry there replaces the one of the same code, a
 * new one is added, so a typo is mended and a hint added without a rebuild.
 * Until it is called only the built in English texts are there.
 */

typedef struct MlCatalogSource
{
	const char *name; /* the file name without its extension */
	const char *text;
} MlCatalogSource;

extern const MlCatalogSource ml_catalog_sources [];
extern const uint64_t        ml_catalog_source_count;

/*
 * The text of a code. A code that has no text gives the code itself, so a
 * missing text shows and never crashes. The result is valid until the next
 * ml_texts_reload.
 */
const char *ml_text (const char *code);

/* The text of a code, NULL when there is none. */
const char *ml_text_find (const char *code);

/*
 * A number that changes whenever the texts change (a reload, or entries
 * added): whoever builds something from the texts rebuilds when it is not
 * the one it saw.
 */
uint64_t ml_texts_generation (void);

/* The same for the code section.name. */
const char *ml_text_in (const char *section, const char *name);

/* How many entries the section has, and the n-th (from 0) in file order. */
uint64_t ml_text_count (const char *section);
const char *ml_text_nth (const char *section, uint64_t n);

/* The name of the n-th entry of the section (the part after the dot). */
const char *ml_text_nth_name (const char *section, uint64_t n);

/* A value for a place {name} of a text. */
typedef struct MlTextArg
{
	const char *name;
	MlSlice    value;
} MlTextArg;

/* Appends the text with every {name} that has an argument filled in. */
MlResult ml_text_fill (
	MlStr           *out,
	const char      *text,
	const MlTextArg *args,
	uint64_t        count
);

/*
 * Starts again with the built in English texts, then the file
 * texts/<language>.mltext of the settings folder (when there is one; a
 * language that is NULL or empty is en). ML_ERR_NOMEM is the only failure; a
 * file with bad lines is used as far as it is good, bad says how many lines
 * it could not read (bad may be NULL).
 */
MlResult ml_texts_reload (const char *language, uint64_t *bad);

/* The language of the last ml_texts_reload, "en" before. */
const char *ml_texts_language (void);

/* Puts the entries of a catalog text over the ones there (bad: see above). */
MlResult ml_texts_add (MlSlice text, uint64_t *bad);

/* Forgets everything, including the built in texts (for the tests). */
void ml_texts_clear (void);

#endif /* MATLOG_TEXTS_TEXTS_H */
