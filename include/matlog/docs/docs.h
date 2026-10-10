#ifndef MATLOG_DOCS_DOCS_H
#define MATLOG_DOCS_DOCS_H

#include <matlog/docs/page.h>
#include <matlog/ui/keymap.h>

/*
 * The documentation (plan section 4.14): the pages of docs/ compiled into the
 * program (source/matlog/docs/pages_data.c, made by tools/gen_docs.py), parsed
 * into pages, searched and checked.
 *
 * Search takes words. A page matches when every word is found in it: in its
 * title or id, its keywords, its summary, a heading or its text; the last word
 * may be the beginning of a word. Case is ignored. Matches in the title
 * count most, then the keywords, the headings and the summary, then the text.
 */

typedef struct MlDocSource
{
	const char *name; /* the file name without its extension: the id */
	const char *text;
} MlDocSource;

/* The built in pages, made by tools/gen_docs.py from docs/*.md. */
extern const MlDocSource ml_doc_sources [];
extern const uint64_t    ml_doc_source_count;

ML_VEC_DEFINE (MlDocPageVec, MlDocPage)

typedef struct MlDocs
{
	MlDocPageVec pages;
} MlDocs;

void ml_docs_init (MlDocs *d);
void ml_docs_free (MlDocs *d);

/*
 * Parses the built in pages. The keys in the pages are written as the action
 * they stand for, {ACTION_WATCH}, and become the first key of the action in
 * the key table keys (NULL: the default table), so the pages show the keys of
 * the user. ML_ERR_PARSE when one is broken: page is its index and line the
 * line of the problem.
 */
MlResult ml_docs_load (
	MlDocs         *d,
	const MlKeymap *keys,
	uint64_t       *page,
	uint64_t       *line
);

/* The index of the page with the id; pages.len when there is none. */
uint64_t ml_docs_find (const MlDocs *d, MlSlice id);

typedef struct MlDocHit
{
	uint64_t page;
	uint64_t score;
	MlStr    where; /* the text around the first match, or the summary */
} MlDocHit;

ML_VEC_DEFINE (MlDocHitVec, MlDocHit)

void ml_doc_hits_free (MlDocHitVec *hits);

/* The pages that match the words, the best first. No words: no hits. */
MlResult ml_docs_search (const MlDocs *d, MlSlice query, MlDocHitVec *hits);

/*
 * Checks the pages as a whole and appends a line to report for every problem:
 * an id that is not the name of the file or is used twice, a `see` entry or a
 * link to a page that does not exist, a page that cannot be reached from the
 * page `index` by links and see entries, and an example whose results are
 * not what the evaluator gives. problems is the number of lines written.
 */
MlResult ml_docs_check (const MlDocs *d, MlStr *report, uint64_t *problems);

/*
 * Evaluates the lines of an example against the written results; mode is the
 * domain of the example: 0 algebra, 1 physics (units), 2 pga2, 3 pga3.
 */
MlResult ml_docs_check_example (
	MlSlice  example,
	int32_t  mode,
	MlStr    *report,
	uint64_t *problems
);

#endif /* MATLOG_DOCS_DOCS_H */
