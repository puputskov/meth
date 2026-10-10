#ifndef METH_EXPORT_EXPORT_H
#define METH_EXPORT_EXPORT_H

#include <meth/session/results.h>
#include <meth/session/session.h>

/*
 * Export (plan section 4.11): the session as a document. The lines are first
 * turned into blocks (headings, paragraphs, typeset math with its result,
 * code, rules), and each format is a writer over the blocks, so the content
 * is the same in all of them.
 *
 *	latex     a complete .tex file with amsmath; `body`: only the body
 *	mathml    an HTML page with presentation MathML; `body`: only the body
 *	markdown  notes as they are, math as $$ ... $$ blocks (LaTeX code)
 *
 * Notes become paragraphs (the note lines of one block are one paragraph; a
 * blank line starts a new one), a note that starts with # and a space is a
 * heading (## is a smaller one), a note of three or more - * _ is a rule, and
 * the Markdown marks of a note become the formatting of the format. Math lines
 * of the algebra domain are typeset from their syntax tree with the result
 * the session has for them (shown as = or ≈); a definition (a = 3, f(x) = ...)
 * shows no result since it is the same. Math lines of other domains, includes
 * and unreadable lines are written as code. Errors are left out unless the
 * `errors` option is given.
 */

typedef enum MlExportFormat
{
	ML_EXPORT_LATEX = 0,
	ML_EXPORT_MATHML,
	ML_EXPORT_MARKDOWN,
	ML_EXPORT_COUNT
} MlExportFormat;

typedef struct MlExportOptions
{
	MlExportFormat format;
	int32_t        body;   /* no preamble, no page around it */
	int32_t        errors; /* the errors of lines are written too */
	int32_t        ranged; /* only the lines first..last (0 based) */
	uint64_t       first;
	uint64_t       last;
	MlSlice        title;  /* the title of an HTML page, "meth" if empty */
} MlExportOptions;

/* latex (tex), mathml (html), markdown (md); ML_EXPORT_COUNT if none. */
MlExportFormat ml_export_format_from_name (MlSlice name);
const char *ml_export_format_name (MlExportFormat format);

/* "tex", "html", "md". */
const char *ml_export_extension (MlExportFormat format);

MlExportOptions ml_export_options (MlExportFormat format);

/*
 * Reads the words after the format: `body`, `errors`, a line range (`3`,
 * `3-12`, 1 based) and a path (the first other word, given back in path, which
 * is empty when there is none). ML_ERR_INVALID for a range that makes no
 * sense.
 */
MlResult ml_export_read_words (
	MlExportOptions *o,
	MlSlice         words,
	MlSlice         *path
);

/*
 * Appends the document. The results are those of the session as computed by
 * ml_results_update. ML_ERR_NOMEM is the only failure.
 */
MlResult ml_export (
	MlStr           *out,
	const MlSession *s,
	const MlResults *results,
	MlExportOptions o
);

#endif /* METH_EXPORT_EXPORT_H */
