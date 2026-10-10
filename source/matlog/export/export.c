#include <matlog/export/export.h>

#include <matlog/export/math.h>
#include <matlog/parse/parser.h>
#include <matlog/ui/markup.h>

#include <string.h>

/* ---- formats and options ------------------------------------------------ */

static const char *const format_names [ML_EXPORT_COUNT] = {
	[ML_EXPORT_LATEX]    = "latex",
	[ML_EXPORT_MATHML]   = "mathml",
	[ML_EXPORT_MARKDOWN] = "markdown"
};

static const char *const extensions [ML_EXPORT_COUNT] = {
	[ML_EXPORT_LATEX]    = "tex",
	[ML_EXPORT_MATHML]   = "html",
	[ML_EXPORT_MARKDOWN] = "md"
};

static int32_t is (MlSlice a, const char *b)
{
	return ml_slice_equal (a, ml_slice_cstr (b));
}

MlExportFormat ml_export_format_from_name (MlSlice name)
{
	MlExportFormat f = ML_EXPORT_COUNT;

	if (is (name, "latex") || is (name, "tex"))
	{
		f = ML_EXPORT_LATEX;
	}

	else if (is (name, "mathml") || is (name, "html"))
	{
		f = ML_EXPORT_MATHML;
	}

	else if (is (name, "markdown") || is (name, "md"))
	{
		f = ML_EXPORT_MARKDOWN;
	}

	return f;
}

const char *ml_export_format_name (MlExportFormat format)
{
	return ml_choose (format < ML_EXPORT_COUNT, format_names [format], "?");
}

const char *ml_export_extension (MlExportFormat format)
{
	return ml_choose (format < ML_EXPORT_COUNT, extensions [format], "");
}

MlExportOptions ml_export_options (MlExportFormat format)
{
	MlExportOptions o = {
		.format = format,
		.body   = 0,
		.errors = 0,
		.ranged = 0,
		.first  = 0,
		.last   = 0,
		.title  = {
			.data = "",
			.len  = 0
		}
	};

	return o;
}

/* A number of digits from pos on; digits is 0 when there is none. */
static uint64_t read_number (MlSlice word, uint64_t *pos, int32_t *digits)
{
	uint64_t n = 0;

	*digits = 0;

	while (*pos < word.len && word.data [*pos] >= '0' && word.data [*pos] <= '9'
	)
	{
		n       = n * 10 + (uint64_t)(word.data [*pos] - '0');
		*digits = 1;
		(*pos)++;
	}

	return n;
}

/* A range word: 5 or 3-12 (1 based). 0 when it is not one, 2 when it is bad. */
static int32_t read_range (MlSlice word, uint64_t *first, uint64_t *last)
{
	uint64_t pos  = 0;
	int32_t  d1   = 0;
	int32_t  d2   = 1;
	uint64_t a    = read_number (word, &pos, &d1);
	uint64_t b    = a;
	int32_t  kind = 0;

	if (d1 && pos == word.len)
	{
		kind = 1;
	}

	else if (d1 && pos < word.len && word.data [pos] == '-')
	{
		pos++;
		b    = read_number (word, &pos, &d2);
		kind = ml_choose (d2 && pos == word.len, 1, 0);
	}

	if (kind == 1)
	{
		kind = ml_choose (a >= 1 && b >= a, 1, 2);
	}

	*first = a;
	*last  = b;
	return kind;
}

MlResult ml_export_read_words (
	MlExportOptions *o,
	MlSlice         words,
	MlSlice         *path
)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	*path = ml_slice ("", 0);

	while (pos < words.len && r == ML_OK)
	{
		uint64_t start;
		MlSlice  word;
		uint64_t a    = 0;
		uint64_t b    = 0;
		int32_t  kind;

		while (pos < words.len && words.data [pos] == ' ')
		{
			pos++;
		}

		start = pos;

		while (pos < words.len && words.data [pos] != ' ')
		{
			pos++;
		}

		word = ml_slice_sub (words, start, pos - start);
		kind = ml_choose (word.len > 0, read_range (word, &a, &b), 0);

		if (word.len == 0)
		{
			r = ML_OK;
		}

		else if (is (word, "body"))
		{
			o->body = 1;
		}

		else if (is (word, "errors"))
		{
			o->errors = 1;
		}

		else if (kind == 1)
		{
			o->ranged = 1;
			o->first  = a - 1;
			o->last   = b - 1;
		}

		else if (kind == 2)
		{
			r = ML_ERR_INVALID;
		}

		else if (path->len == 0)
		{
			*path = word;
		}

		else
		{
			r = ML_ERR_INVALID;
		}
	}

	return r;
}

/* ---- blocks ------------------------------------------------------------- */

typedef enum BlockKind
{
	B_HEADING = 0,
	B_PARAGRAPH,
	B_RULE,
	B_MATH,
	B_CODE,
	B_ERROR
} BlockKind;

typedef struct Block
{
	BlockKind kind;
	int32_t   level;      /* B_HEADING: 1 to 6 */
	MlStr     text;       /* the note, the source of the math, the code */
	MlStr     result;     /* B_MATH: the value, without a leading ≈ */
	int32_t   has_result;
	int32_t   approx;
	int32_t   logic;      /* B_MATH: 0 algebra, 1 boolean, 2 lambda */
} Block;

ML_VEC_DEFINE (BlockVec, Block)

static void blocks_free (BlockVec *blocks)
{
	for (uint64_t i = 0; i < blocks->len; i++)
	{
		ml_str_free (&blocks->data [i].text);
		ml_str_free (&blocks->data [i].result);
	}

	BlockVec_free (blocks);
}

static Block block_new (BlockKind kind)
{
	Block b = {
		.kind       = kind,
		.level      = 0,
		.has_result = 0,
		.approx     = 0,
		.logic      = 0
	};

	ml_str_init (&b.text);
	ml_str_init (&b.result);
	return b;
}

/* Adds a block with the text; the block owns the text afterwards. */
static MlResult add_block (BlockVec *blocks, Block b, MlSlice text)
{
	MlResult r = ml_str_append (&b.text, text);

	if (r == ML_OK)
	{
		r = BlockVec_push (blocks, b);
	}

	if (r != ML_OK)
	{
		ml_str_free (&b.text);
		ml_str_free (&b.result);
	}

	return r;
}

/* A note of three or more -, * or _ only (spaces around are allowed). */
static int32_t is_rule (MlSlice text)
{
	uint64_t first = 0;
	uint64_t end   = text.len;
	char     mark;
	int32_t  rule;

	while (first < end && text.data [first] == ' ')
	{
		first++;
	}

	while (end > first && text.data [end - 1] == ' ')
	{
		end--;
	}

	mark = ml_choose (end > first, text.data [first], 0);
	rule = (
		   end - first >= 3
		&& (mark == '-' || mark == '*' || mark == '_')
	);

	for (uint64_t i = first; i < end && rule; i++)
	{
		rule = text.data [i] == mark;
	}

	return rule;
}

/* The level of a heading note (# Title), 0 when it is not one. */
static int32_t heading_level (MlSlice text)
{
	int32_t level = 0;

	while ((uint64_t)level < text.len && text.data [level] == '#')
	{
		level++;
	}

	return ml_choose (
		level >= 1 && level <= 6
		&& (uint64_t)level + 1 < text.len
		&& text.data [level] == ' ',
		level,
		0
	);
}

/* Adds the block of a math line of the algebra domain with its result. */
static MlResult add_math (
	BlockVec     *blocks,
	MlSlice      source,
	MlResultView res,
	int32_t      errors,
	int32_t      units
)
{
	MlResult      r = ML_OK;
	MlParseResult p = ml_parse_units (source, units);
	Block         b = block_new (B_MATH);
	int32_t       ok = p.error.code == ML_PARSE_OK && p.ast.root != 0;
	MlNodeKind    root = ml_ast_node (&p.ast, p.ast.root).kind;
	MlSlice       value = res.text;

	/* a definition shows no result, it is the value it was given */
	b.has_result = (
		   ok
		&& res.kind == ML_RESULT_VALUE
		&& root != ML_NODE_ASSIGN
		&& root != ML_NODE_FUNC_DEF
	);

	if (b.has_result && value.len >= 3
		&& memcmp (value.data, "\xE2\x89\x88", 3) == 0)
	{
		b.approx = 1;
		value    = ml_slice_sub (value, 3, value.len - 3);

		while (value.len > 0 && value.data [0] == ' ')
		{
			value = ml_slice_sub (value, 1, value.len - 1);
		}
	}

	if (!ok)
	{
		b.kind = B_CODE;
	}

	else if (b.has_result)
	{
		r = ml_str_append (&b.result, value);
	}

	if (r == ML_OK)
	{
		r = add_block (blocks, b, source);
	}

	else
	{
		ml_str_free (&b.text);
		ml_str_free (&b.result);
	}

	if (r == ML_OK && ok && res.kind == ML_RESULT_ERROR && errors)
	{
		Block e = block_new (B_ERROR);

		r = add_block (blocks, e, res.text);
	}

	ml_ast_free (&p.ast);
	return r;
}

/*
 * Adds the block of a line of the boolean or the lambda domain: the line
 * and its value typeset, and the rows under it (a truth table, the steps of
 * a reduction) as code.
 */
static MlResult add_logic (
	BlockVec     *blocks,
	MlSlice      source,
	MlResultView res,
	int32_t      lambda,
	int32_t      errors
)
{
	MlResult r = ML_OK;
	Block    b = block_new (
		ml_choose (res.kind == ML_RESULT_ERROR, B_CODE, B_MATH)
	);

	/* a line with an error is code, as an algebra line that does not parse */
	b.logic      = ml_choose (lambda, 2, 1);
	b.has_result = res.kind == ML_RESULT_VALUE;

	if (b.has_result)
	{
		r = ml_str_append (&b.result, res.text);
	}

	if (r == ML_OK)
	{
		r = add_block (blocks, b, source);
	}

	else
	{
		ml_str_free (&b.text);
		ml_str_free (&b.result);
	}

	if (r == ML_OK && res.kind == ML_RESULT_VALUE && res.block.len > 0)
	{
		r = add_block (blocks, block_new (B_CODE), res.block);
	}

	if (r == ML_OK && res.kind == ML_RESULT_ERROR && errors)
	{
		r = add_block (blocks, block_new (B_ERROR), res.text);
	}

	return r;
}

/* The blocks of the lines first..last. */
static MlResult build_blocks (
	BlockVec        *blocks,
	const MlSession *s,
	const MlResults *results,
	MlExportOptions o
)
{
	MlResult r         = ML_OK;
	uint64_t last      = ml_choose (
		o.ranged && o.last + 1 < s->lines.len,
		o.last,
		ml_choose (s->lines.len > 0, s->lines.len - 1, 0)
	);
	uint64_t first     = ml_choose (o.ranged, o.first, 0);
	int32_t  can_join  = 0; /* the line before was a note of a paragraph */

	for (uint64_t i = first; i <= last && i < s->lines.len && r == ML_OK; i++)
	{
		MlLineView v = ml_session_get (s, i);

		if (v.kind == ML_LINE_NOTE && heading_level (v.text) > 0)
		{
			Block b = block_new (B_HEADING);
			int32_t level = heading_level (v.text);

			b.level = level;
			r       = add_block (
				blocks,
				b,
				ml_slice_sub (
					v.text,
					(uint64_t)level + 1,
					v.text.len - (uint64_t)level - 1
				)
			);
			can_join = 0;
		}

		else if (v.kind == ML_LINE_NOTE && is_rule (v.text))
		{
			r        = add_block (blocks, block_new (B_RULE), ml_slice ("", 0));
			can_join = 0;
		}

		else if (v.kind == ML_LINE_NOTE && can_join && blocks->len > 0)
		{
			Block *p = &blocks->data [blocks->len - 1];

			r = ml_str_append (&p->text, ml_slice_cstr ("\n"));

			if (r == ML_OK)
			{
				r = ml_str_append (&p->text, v.text);
			}
		}

		else if (v.kind == ML_LINE_NOTE)
		{
			r        = add_block (blocks, block_new (B_PARAGRAPH), v.text);
			can_join = 1;
		}

		else if (
			   v.kind == ML_LINE_MATH
			&& (
				   v.domain == ML_DOMAIN_ALGEBRA
				|| v.domain == ML_DOMAIN_PHYSICS
			)
		)
		{
			r = add_math (
				blocks,
				v.text,
				ml_results_get (results, i),
				o.errors,
				v.domain == ML_DOMAIN_PHYSICS
			);
			can_join = 0;
		}

		else if (
			   v.kind == ML_LINE_MATH
			&& (v.domain == ML_DOMAIN_BOOLEAN || v.domain == ML_DOMAIN_LAMBDA)
		)
		{
			r = add_logic (
				blocks,
				v.text,
				ml_results_get (results, i),
				v.domain == ML_DOMAIN_LAMBDA,
				o.errors
			);
			can_join = 0;
		}

		else if (v.kind == ML_LINE_MATH || v.kind == ML_LINE_UNKNOWN)
		{
			r        = add_block (blocks, block_new (B_CODE), v.text);
			can_join = 0;
		}

		else if (v.kind == ML_LINE_INCLUDE)
		{
			MlStr text;

			ml_str_init (&text);
			r = ml_str_appendf (
				&text,
				"include \"%.*s\"",
				(int)v.text.len,
				v.text.data
			);

			if (r == ML_OK)
			{
				r = add_block (blocks, block_new (B_CODE), ml_str_view (text));
			}

			ml_str_free (&text);
			can_join = 0;
		}

		else
		{
			can_join = 0;
		}
	}

	return r;
}

/* ---- text of notes ------------------------------------------------------ */

static MlResult put (MlStr *out, const char *text)
{
	return ml_str_append (out, ml_slice_cstr (text));
}

/* The tags a style mask opens and closes in each format. */
static const char *const latex_open [] = {
	"\\textbf{", "\\textit{", "\\underline{", "\\sout{", "\\texttt{"
};

static const char *const html_open [] = {
	"<b>", "<i>", "<u>", "<s>", "<code>"
};

static const char *const html_close [] = {
	"</b>", "</i>", "</u>", "</s>", "</code>"
};

/* A note in the notation of the format, its Markdown marks as formatting. */
static MlResult put_note (MlStr *out, MlMathNotation n, MlSlice text)
{
	MlResult r = ML_OK;
	MlMarkup m;
	MlStr    flat;

	ml_markup_init (&m);
	ml_str_init (&flat);

	/* the lines of a paragraph are one line of text */
	for (uint64_t i = 0; i < text.len && r == ML_OK; i++)
	{
		r = ml_str_append (
			&flat,
			ml_choose (
				text.data [i] == '\n',
				ml_slice_cstr (" "),
				ml_slice_sub (text, i, 1)
			)
		);
	}

	if (r == ML_OK)
	{
		r = ml_markup_parse (&m, ml_str_view (flat));
	}

	for (uint64_t i = 0; i < m.plain.len && r == ML_OK; )
	{
		uint32_t mask = m.styles.data [i];
		uint64_t end  = i + 1;

		while (end < m.plain.len && m.styles.data [end] == mask)
		{
			end++;
		}

		for (uint32_t bit = 0; bit < 5 && r == ML_OK; bit++)
		{
			if (mask & (1u << bit))
			{
				r = put (
					out,
					ml_choose (
						n == ML_MATH_LATEX,
						latex_open [bit],
						html_open [bit]
					)
				);
			}
		}

		if (r == ML_OK)
		{
			r = ml_math_escape (
				out,
				n,
				ml_slice_sub (ml_str_view (m.plain), i, end - i)
			);
		}

		for (uint32_t bit = 5; bit > 0 && r == ML_OK; bit--)
		{
			if (mask & (1u << (bit - 1)))
			{
				r = put (
					out,
					ml_choose (n == ML_MATH_LATEX, "}", html_close [bit - 1])
				);
			}
		}

		i = end;
	}

	ml_str_free (&flat);
	ml_markup_free (&m);
	return r;
}

/* A formula, or the text when it does not parse. */
static MlResult put_formula (
	MlStr          *out,
	MlMathNotation n,
	int32_t        logic,
	MlSlice        text
)
{
	MlResult r = ml_choose (
		logic > 0,
		ml_logic_typeset (out, n, logic == 2, text),
		ml_math_typeset (out, n, text)
	);

	if (r == ML_ERR_PARSE)
	{
		r = put (out, ml_choose (n == ML_MATH_LATEX, "\\text{", "<mtext>"));

		if (r == ML_OK)
		{
			r = ml_math_escape (out, n, text);
		}

		if (r == ML_OK)
		{
			r = put (out, ml_choose (n == ML_MATH_LATEX, "}", "</mtext>"));
		}
	}

	return r;
}

/*
 * The source of a formula, then the relation and the result. A result that
 * looks the same as the formula (1/2 for 1/2) is not repeated.
 */
static MlResult put_math_line (MlStr *out, MlMathNotation n, const Block *b)
{
	MlResult r = ML_OK;
	MlStr    source;
	MlStr    result;

	ml_str_init (&source);
	ml_str_init (&result);
	r = put_formula (&source, n, b->logic, ml_str_view (b->text));

	if (r == ML_OK && b->has_result)
	{
		r = put_formula (&result, n, b->logic, ml_str_view (b->result));
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_str_view (source));
	}

	if (r == ML_OK && b->has_result
		&& (b->approx
			|| !ml_slice_equal (ml_str_view (source), ml_str_view (result))))
	{
		r = ml_math_relation (out, n, b->approx);

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_str_view (result));
		}
	}

	ml_str_free (&result);
	ml_str_free (&source);
	return r;
}

/* ---- the writers -------------------------------------------------------- */

static const char *const latex_sections [] = {
	"\\section*{", "\\subsection*{", "\\subsubsection*{",
	"\\paragraph{", "\\subparagraph{", "\\subparagraph{"
};

static MlResult latex_block (MlStr *out, const Block *b)
{
	MlResult r = ML_OK;

	if (b->kind == B_HEADING)
	{
		r = put (out, latex_sections [b->level - 1]);

		if (r == ML_OK)
		{
			r = put_note (out, ML_MATH_LATEX, ml_str_view (b->text));
		}

		if (r == ML_OK)
		{
			r = put (out, "}\n\n");
		}
	}

	else if (b->kind == B_PARAGRAPH)
	{
		r = put_note (out, ML_MATH_LATEX, ml_str_view (b->text));
		r = ml_choose (r == ML_OK, put (out, "\n\n"), r);
	}

	else if (b->kind == B_RULE)
	{
		r = put (out, "\\noindent\\rule{\\linewidth}{0.4pt}\n\n");
	}

	else if (b->kind == B_MATH)
	{
		r = put (out, "\\[\n");

		if (r == ML_OK)
		{
			r = put_math_line (out, ML_MATH_LATEX, b);
		}

		if (r == ML_OK)
		{
			r = put (out, "\n\\]\n\n");
		}
	}

	else if (b->kind == B_CODE)
	{
		r = put (out, "\\begin{verbatim}\n");

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_str_view (b->text));
		}

		if (r == ML_OK)
		{
			r = put (out, "\n\\end{verbatim}\n\n");
		}
	}

	else
	{
		r = put (out, "\\texttt{! ");

		if (r == ML_OK)
		{
			r = ml_math_escape (out, ML_MATH_LATEX, ml_str_view (b->text));
		}

		if (r == ML_OK)
		{
			r = put (out, "}\n\n");
		}
	}

	return r;
}

static MlResult html_block (MlStr *out, const Block *b, MlSlice source_text)
{
	MlResult r = ML_OK;

	(void)source_text;

	if (b->kind == B_HEADING)
	{
		r = ml_str_appendf (out, "<h%d>", b->level);

		if (r == ML_OK)
		{
			r = put_note (out, ML_MATH_MATHML, ml_str_view (b->text));
		}

		if (r == ML_OK)
		{
			r = ml_str_appendf (out, "</h%d>\n", b->level);
		}
	}

	else if (b->kind == B_PARAGRAPH)
	{
		r = put (out, "<p>");
		r = ml_choose (
			r == ML_OK,
			put_note (out, ML_MATH_MATHML, ml_str_view (b->text)),
			r
		);
		r = ml_choose (r == ML_OK, put (out, "</p>\n"), r);
	}

	else if (b->kind == B_RULE)
	{
		r = put (out, "<hr>\n");
	}

	else if (b->kind == B_MATH)
	{
		r = put (out, "<math display=\"block\"><mrow>");

		if (r == ML_OK)
		{
			r = put_math_line (out, ML_MATH_MATHML, b);
		}

		if (r == ML_OK)
		{
			r = put (out, "</mrow></math>\n");
		}
	}

	else if (b->kind == B_CODE)
	{
		r = put (out, "<pre><code>");

		if (r == ML_OK)
		{
			r = ml_math_escape (out, ML_MATH_MATHML, ml_str_view (b->text));
		}

		if (r == ML_OK)
		{
			r = put (out, "</code></pre>\n");
		}
	}

	else
	{
		r = put (out, "<p class=\"error\"><code>! ");

		if (r == ML_OK)
		{
			r = ml_math_escape (out, ML_MATH_MATHML, ml_str_view (b->text));
		}

		if (r == ML_OK)
		{
			r = put (out, "</code></p>\n");
		}
	}

	return r;
}

static MlResult markdown_block (MlStr *out, const Block *b)
{
	MlResult r = ML_OK;

	if (b->kind == B_HEADING)
	{
		for (int32_t i = 0; i < b->level && r == ML_OK; i++)
		{
			r = put (out, "#");
		}

		r = ml_choose (r == ML_OK, put (out, " "), r);
		r = ml_choose (
			r == ML_OK,
			ml_str_append (out, ml_str_view (b->text)),
			r
		);
		r = ml_choose (r == ML_OK, put (out, "\n\n"), r);
	}

	else if (b->kind == B_PARAGRAPH)
	{
		r = ml_str_append (out, ml_str_view (b->text));
		r = ml_choose (r == ML_OK, put (out, "\n\n"), r);
	}

	else if (b->kind == B_RULE)
	{
		r = put (out, "---\n\n");
	}

	else if (b->kind == B_MATH)
	{
		r = put (out, "$$\n");
		r = ml_choose (r == ML_OK, put_math_line (out, ML_MATH_LATEX, b), r);
		r = ml_choose (r == ML_OK, put (out, "\n$$\n\n"), r);
	}

	else if (b->kind == B_CODE)
	{
		r = put (out, "```\n");
		r = ml_choose (
			r == ML_OK,
			ml_str_append (out, ml_str_view (b->text)),
			r
		);
		r = ml_choose (r == ML_OK, put (out, "\n```\n\n"), r);
	}

	else
	{
		r = put (out, "`! ");
		r = ml_choose (
			r == ML_OK,
			ml_str_append (out, ml_str_view (b->text)),
			r
		);
		r = ml_choose (r == ML_OK, put (out, "`\n\n"), r);
	}

	return r;
}

MlResult ml_export (
	MlStr           *out,
	const MlSession *s,
	const MlResults *results,
	MlExportOptions o
)
{
	MlResult r = ML_OK;
	BlockVec blocks;

	BlockVec_init (&blocks);
	r = build_blocks (&blocks, s, results, o);

	if (r == ML_OK && o.format == ML_EXPORT_LATEX && !o.body)
	{
		r = put (
			out,
			"\\documentclass{article}\n"
			"\\usepackage[utf8]{inputenc}\n"
			"\\usepackage{amsmath}\n"
			"\\usepackage[normalem]{ulem}\n"
			"\\begin{document}\n\n"
		);
	}

	if (r == ML_OK && o.format == ML_EXPORT_MATHML && !o.body)
	{
		r = put (
			out,
			"<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta "
			"charset=\"utf-8\">\n<title>"
		);

		if (r == ML_OK)
		{
			r = ml_math_escape (
				out,
				ML_MATH_MATHML,
				ml_choose (o.title.len > 0, o.title, ml_slice_cstr ("matlog"))
			);
		}

		if (r == ML_OK)
		{
			r = put (
				out,
				"</title>\n"
				"<style>\n"
				"body { font-family: serif; max-width: 48em; margin: 2em auto; "
				"padding: 0 1em; }\n"
				"math { font-size: 1.15em; }\n"
				"pre { background: #f4f4f4; padding: 0.5em; }\n"
				".error { color: #b00020; }\n"
				"</style>\n"
				"</head>\n<body>\n"
			);
		}
	}

	for (uint64_t i = 0; i < blocks.len && r == ML_OK; i++)
	{
		if (o.format == ML_EXPORT_LATEX)
		{
			r = latex_block (out, &blocks.data [i]);
		}

		else if (o.format == ML_EXPORT_MATHML)
		{
			r = html_block (out, &blocks.data [i], o.title);
		}

		else
		{
			r = markdown_block (out, &blocks.data [i]);
		}
	}

	if (r == ML_OK && o.format == ML_EXPORT_LATEX && !o.body)
	{
		r = put (out, "\\end{document}\n");
	}

	if (r == ML_OK && o.format == ML_EXPORT_MATHML && !o.body)
	{
		r = put (out, "</body>\n</html>\n");
	}

	blocks_free (&blocks);
	return r;
}
