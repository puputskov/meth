#ifndef MATLOG_DOCS_PAGE_H
#define MATLOG_DOCS_PAGE_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>
#include <matlog/core/vec.h>

/*
 * One page of the documentation (plan section 4.14), in a small strict markup
 * that is also readable as plain Markdown. A page is a header and a body:
 *
 *	id: syntax
 *	title: The algebra syntax
 *	summary: Numbers, names, operators and their precedence.
 *	keywords: precedence, operators, implicit multiplication
 *	see: functions, units
 *
 *	# A heading (## and ### are smaller ones)
 *	A paragraph. The lines of it are one line of text; a blank line ends
 *	it. A link to another page is [its words](page-id).
 *
 *	- a list item, which goes on
 *	  on the lines that are indented
 *
 *	```example
 *	1/3 + 1/6
 *	=> 1/2
 *	```
 *
 * The header ends at the first blank line; id and title are required, the
 * other entries may be left out. A fenced block has a kind after the opening
 * fence: `example` (input lines, each followed by `=> result`; checked against
 * the evaluator by ml_docs_check), `keys` (a key and what it does), `command`
 * (a : command or a command line) or nothing (code). The text of the block is
 * kept as it is, line breaks included.
 */

typedef enum MlDocBlockKind
{
	ML_DOC_HEADING = 0,
	ML_DOC_PARAGRAPH,
	ML_DOC_ITEM,
	ML_DOC_EXAMPLE,
	ML_DOC_KEYS,
	ML_DOC_COMMAND,
	ML_DOC_CODE,
	ML_DOC_BLOCK_COUNT
} MlDocBlockKind;

typedef struct MlDocBlock
{
	MlDocBlockKind kind;
	int32_t        level; /* ML_DOC_HEADING: 1 to 3 */
	MlStr          text;
} MlDocBlock;

ML_VEC_DEFINE (MlDocBlockVec, MlDocBlock)

typedef struct MlDocPage
{
	MlStr          id;
	MlStr          title;
	MlStr          summary;
	MlStr          keywords; /* as written: words separated by commas */
	MlStr          see;      /* as written: ids separated by commas */
	MlDocBlockVec  blocks;
} MlDocPage;

void ml_doc_page_init (MlDocPage *p);
void ml_doc_page_free (MlDocPage *p);

/*
 * Parses the text of a page. ML_ERR_PARSE when the id or the title is
 * missing, a header line is not `name: value`, a fence is not closed or the
 * kind of a fence is unknown; line is then the line (from 1) of the problem.
 */
MlResult ml_doc_parse (MlDocPage *p, MlSlice text, uint64_t *line);

/* The kind of block as a word: "heading", "paragraph", "example" ... */
const char *ml_doc_block_name (MlDocBlockKind kind);

/* A link in a text: the words that are marked and the id of the page. */
typedef struct MlDocLink
{
	uint64_t start;  /* byte range of the words in the plain text */
	uint64_t len;
	MlSlice  target; /* the id, a view into the text that was searched */
} MlDocLink;

ML_VEC_DEFINE (MlDocLinkVec, MlDocLink)

/*
 * Takes the link marks out of a text: out gets the text with [words](id)
 * replaced by words, and links the places of the words and their targets (the
 * targets are views into text, valid as long as text is).
 */
MlResult ml_doc_plain (MlStr *out, MlDocLinkVec *links, MlSlice text);

/* ---- laid out text ------------------------------------------------------ */

typedef enum MlDocStyle
{
	ML_DOC_STYLE_BLANK = 0,
	ML_DOC_STYLE_TITLE,
	ML_DOC_STYLE_HEADING,
	ML_DOC_STYLE_TEXT,
	ML_DOC_STYLE_ITEM,     /* a list item, with its bullet on the first line */
	ML_DOC_STYLE_INPUT,    /* a line of an example that is typed */
	ML_DOC_STYLE_RESULT,   /* a line of an example that is the result */
	ML_DOC_STYLE_KEYS,
	ML_DOC_STYLE_COMMAND,
	ML_DOC_STYLE_CODE,
	ML_DOC_STYLE_COUNT
} MlDocStyle;

typedef struct MlDocLine
{
	MlDocStyle   style;
	MlStr        text;  /* without link marks */
	MlDocLinkVec links; /* in this line; the targets are copied */
	MlStr        targets; /* the targets of links, one after the other */
	uint64_t     block; /* the block of the page it comes from */
} MlDocLine;

ML_VEC_DEFINE (MlDocLineVec, MlDocLine)

void ml_doc_lines_free (MlDocLineVec *lines);

/*
 * Lays a page out in lines of at most width columns: the title and the summary,
 * then the blocks, paragraphs and list items wrapped at word boundaries, the
 * other blocks line by line (cut at width). Links keep their place in the line
 * they land in. The target of the n-th link of a line is
 * ml_doc_line_target (line, n).
 */
MlResult ml_doc_layout (
	MlDocLineVec    *lines,
	const MlDocPage *page,
	uint64_t        width
);

MlSlice ml_doc_line_target (const MlDocLine *line, uint64_t n);

#endif /* MATLOG_DOCS_PAGE_H */
