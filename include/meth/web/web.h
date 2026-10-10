#ifndef METH_WEB_WEB_H
#define METH_WEB_WEB_H

#include <meth/core/err.h>
#include <meth/core/slice.h>
#include <meth/core/str.h>
#include <meth/docs/docs.h>
#include <meth/session/results.h>
#include <meth/session/session.h>

/*
 * What a page needs from the library (plan section 9 of TODO.md, the web
 * editor): a session with its results, and the services of the editor, each as
 * a function that writes its answer as JSON (or, where noted, as plain text)
 * into one string of the object. The page reads no structure of the library:
 * it sends text and indices and reads the answer, so the same functions work
 * from a WebAssembly module (source/web/api.c) and from a test.
 *
 * Indices are 0 based lines of the session. A function returns ML_OK or the
 * failure of the library (ML_ERR_NOMEM, ML_ERR_INVALID for an index or a name
 * that is not valid, ML_ERR_PARSE for a file that cannot be read); the answer
 * of a function that failed is not meaningful.
 *
 * A line as the page sees it (the "lines" of ml_web_lines):
 *
 *	{ "i": 3, "kind": "math", "domain": "algebra", "text": "A * B",
 *	  "first": 2, "last": 4,           the block (cell) the line is in
 *	  "wants_steps": false,
 *	  "result": { "kind": "value", "text": "7/2", "type": "exact",
 *	              "block": [rows of a typeset matrix], "steps": [rows],
 *	              "plot": { "kind": "histogram", "count": 6, "lo": 1,
 *	                        "hi": 3, "xs": [], "ys": [1, 2, 3] } } }
 *
 * and a result of kind "error" has "text", "code", "start" and "len" (the byte
 * range in the line), one of kind "none" has nothing else. kind is one of
 * math, note, include, blank, unknown; domain is for math lines only.
 */

typedef struct MlWeb
{
	MlSession session;
	MlResults results;
	MlDocs    docs;        /* the documentation, loaded on first use */
	int32_t   docs_loaded;
	MlStr     out;         /* the answer of the last call */
} MlWeb;

MlResult ml_web_init (MlWeb *w);
void     ml_web_free (MlWeb *w);

/* The answer of the last call, valid until the next one. */
MlSlice ml_web_answer (const MlWeb *w);

/*
 * Replaces the session by the lines of a .meth file (session/file.h) and
 * evaluates it. Answer: the lines, as for ml_web_lines from line 0.
 */
MlResult ml_web_load (MlWeb *w, MlSlice file_text);

/* The session as a .meth file; the answer is the file text. */
MlResult ml_web_save (MlWeb *w);

/*
 * { "from": F, "count": N, "lines": [...] }: the N lines of the session and
 * the lines from F on, as above.
 */
MlResult ml_web_lines (MlWeb *w, uint64_t from);

typedef enum MlWebEdit
{
	ML_WEB_REPLACE = 0, /* the line idx gets kind, domain and text */
	ML_WEB_INSERT,      /* a new line before idx (idx == count appends) */
	ML_WEB_DELETE,      /* the line idx goes; kind, domain, text are unused */
	ML_WEB_UNDO,        /* take back the last group of changes */
	ML_WEB_REDO
} MlWebEdit;

/*
 * Changes the session, evaluates again from the changed line and answers with
 * the lines from there on (ml_web_lines). kind is "math", "note", "blank" or
 * "include" and domain is the name of a domain (see ml_web_domains), used for
 * math lines. Every call is one step of the undo history.
 */
MlResult ml_web_edit (
	MlWeb    *w,
	MlWebEdit op,
	uint64_t idx,
	MlSlice  kind,
	MlSlice  domain,
	MlSlice  text
);

/* Names a name with no value as a symbol (1) or as an error (0). */
MlResult ml_web_set_symbols (MlWeb *w, int32_t on);

/*
 * The syntax classes of text as the line idx of the session in a domain:
 * [[start, length, "class"], ...], in order, covering the whole text (the
 * classes are those of ui/theme.h: number, name, function, ...). err_start
 * and err_len mark the byte range of an error (length 0 for none).
 */
MlResult ml_web_highlight (
	MlWeb    *w,
	MlSlice  domain,
	uint64_t idx,
	MlSlice  text,
	uint64_t err_start,
	uint64_t err_len
);

/*
 * The preview of the text typed as line idx (the number of lines for a new
 * line at the end): { "shown": true, "text": "7/2", "block": [rows] }.
 */
MlResult ml_web_ghost (MlWeb *w, MlSlice domain, uint64_t idx, MlSlice text);

/*
 * Completion of the name at the end of text (the line up to the cursor):
 * { "start": byte offset of the word, "items": [...], "ghost": "rest of the
 * first one", "space": false }.
 */
MlResult ml_web_complete (MlWeb *w, MlSlice domain, uint64_t idx, MlSlice text);

/* { "found": true, "rows": [...] }: the steps of line idx on request. */
MlResult ml_web_steps (MlWeb *w, uint64_t idx);

/* { "reason": "simplify" }: a reason for the proof step at line idx. */
MlResult ml_web_hint (MlWeb *w, uint64_t idx);

/*
 * The session as a document; format is latex, mathml or markdown and words are
 * the options of :export (body, errors, a line range). The answer is the
 * document text.
 */
MlResult ml_web_export (MlWeb *w, MlSlice format, MlSlice words);

/*
 * ["algebra", "lambda", ...]: the names of the domains, in the order of the
 * domain menu.
 */
MlResult ml_web_domains (MlWeb *w);

/*
 * Help, examples, snippets, the tutorial and themes: the content of the
 * program, as data for the page.
 *
 *	ml_web_docs           [{ id, title, summary }] of every page
 *	ml_web_doc            one page with its blocks: { id, title, summary,
 *	                      keywords, see, blocks: [{ kind, text }] }, kind is
 *	                      heading, paragraph, item, example, keys, command or
 *	                      code; ML_ERR_INVALID for an id that is no page
 *	ml_web_docs_search    [{ score, id, title, where }], the best first
 *	ml_web_examples       [{ name, title, summary, area }]
 *	ml_web_example        the text of the .meth file of an example (plain
 *	                      text, ML_ERR_INVALID for an unknown name)
 *	ml_web_snippets       [{ name, summary }]
 *	ml_web_snippet        the text of the .meth file of a snippet (plain text)
 *	ml_web_lessons        [{ id, title, stage, steps: [{ text, check, arg,
 *	                      param, param2, hint, answer }] }], check is a
 *	                      number of MlCheck (tutorial/lessons.h)
 *	ml_web_themes         the built in themes, see ml_web_theme_text
 *	ml_web_theme_text     a theme file (the text of a name.theme) applied to
 *	                      the dark theme: { name, background, paint,
 *	                      classes: { number: { fg, bg, bold, dim, underline,
 *	                      reverse }, ... }, bad } where colours are "#rrggbb"
 *	                      or null for the colour of the page, and bad is the
 *	                      number of lines that did not parse
 */
MlResult ml_web_docs (MlWeb *w);
MlResult ml_web_doc (MlWeb *w, MlSlice id);
MlResult ml_web_docs_search (MlWeb *w, MlSlice query);
MlResult ml_web_examples (MlWeb *w);
MlResult ml_web_example (MlWeb *w, MlSlice name);
MlResult ml_web_snippets (MlWeb *w);
MlResult ml_web_snippet (MlWeb *w, MlSlice name);
MlResult ml_web_lessons (MlWeb *w);
MlResult ml_web_themes (MlWeb *w);
MlResult ml_web_theme_text (MlWeb *w, MlSlice text);

#endif /* METH_WEB_WEB_H */
