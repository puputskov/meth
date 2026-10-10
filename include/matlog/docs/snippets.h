#ifndef MATLOG_DOCS_SNIPPETS_H
#define MATLOG_DOCS_SNIPPETS_H

#include <matlog/docs/docs.h>
#include <matlog/session/session.h>

/*
 * Snippets (plan section 4.16): small named pieces of a session that are put
 * into the one being edited, as a start to change. A snippet is a session
 * file whose first note is its one line summary; the lines after it are what
 * is inserted.
 *
 *	# Roots of a x^2 + b x + c with the discriminant
 *	m:algebra a = 1
 *	m:algebra d = b^2 - 4*a*c
 *
 * The built in snippets are snippets/*.mlog, compiled into the program
 * (source/matlog/docs/snippets_data.c, made by tools/gen_docs.py). The user's
 * own are name.mlog files in the folder `snippets` of the settings folder; a
 * file there hides the built in snippet of the same name. A name is made of
 * lower case letters, digits, `-` and `_`.
 */

extern const MlDocSource ml_snippet_sources [];
extern const uint64_t    ml_snippet_source_count;

uint64_t ml_snippet_count (void);
MlSlice ml_snippet_name (uint64_t i);
MlSlice ml_snippet_text (uint64_t i);

/* 1 when the text is a name a snippet may have. */
int32_t ml_snippet_name_ok (MlSlice name);

typedef struct MlSnippetFound
{
	MlResult result; /* ML_ERR_NOMEM is the only failure */
	int32_t  found;
} MlSnippetFound;

/* Appends the text of the snippet to out: the user's file, else built in. */
MlSnippetFound ml_snippet_find (MlStr *out, MlSlice name);

/* Appends the summary (the first note) of a snippet text to out. */
MlResult ml_snippet_summary (MlStr *out, MlSlice text);

/* Puts the lines of a snippet text after its summary into the session. */
MlResult ml_snippet_lines (MlSession *out, MlSlice text);

/* The text of the page `snippets`: the built in ones and the user's. */
MlResult ml_snippets_page (MlStr *out);

/* Adds the page `snippets` (the built in ones) to d. */
MlResult ml_snippets_add_page (MlDocs *d);

#endif /* MATLOG_DOCS_SNIPPETS_H */
