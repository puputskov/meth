#include "test.h"

#include <matlog/ui/markup.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/*
 * The text of a note after its marks are taken away, and which styles its
 * bytes have written as a string of the same length: '.' none, b bold,
 * i italic, u underline, s strike, c code, and a letter in capitals when
 * more than one (B = bold and italic ...) is replaced by '*'.
 */
static int32_t parses_to (
	const char *source,
	const char *plain,
	const char *styles
)
{
	MlMarkup m;
	MlStr    marks;
	int32_t  same;

	ml_markup_init (&m);
	ml_str_init (&marks);
	CHECK (ml_markup_parse (&m, lit (source)) == ML_OK);

	for (uint64_t i = 0; i < m.styles.len; i++)
	{
		uint8_t s = m.styles.data [i];
		char    c = '.';

		c = ml_choose (s == ML_STYLE_BOLD, 'b', c);
		c = ml_choose (s == ML_STYLE_ITALIC, 'i', c);
		c = ml_choose (s == ML_STYLE_UNDERLINE, 'u', c);
		c = ml_choose (s == ML_STYLE_STRIKE, 's', c);
		c = ml_choose (s == ML_STYLE_CODE, 'c', c);
		c = ml_choose (
			s != 0 && (s & (s - 1)) != 0,
			'*',
			c
		);
		ml_str_append (&marks, ml_slice (&c, 1));
	}

	same = (
		   ml_slice_equal (ml_str_view (m.plain), lit (plain))
		&& ml_slice_equal (ml_str_view (marks), lit (styles))
		&& m.plain.len == m.styles.len
	);

	if (!same)
	{
		printf (
			"  \"%s\" gives \"%s\" / \"%s\", expected \"%s\" / \"%s\"\n",
			source,
			ml_str_cstr (m.plain),
			ml_str_cstr (marks),
			plain,
			styles
		);
	}

	ml_str_free (&marks);
	ml_markup_free (&m);
	return same;
}

static void test_marks (void)
{
	/* nothing to format */
	CHECK (parses_to ("", "", ""));
	CHECK (parses_to ("plain text", "plain text", ".........."));

	/* each mark, alone and in a sentence */
	CHECK (parses_to ("**bold**", "bold", "bbbb"));
	CHECK (parses_to ("*italic*", "italic", "iiiiii"));
	CHECK (parses_to ("_italic_", "italic", "iiiiii"));
	CHECK (parses_to ("<u>under</u>", "under", "uuuuu"));
	CHECK (parses_to ("<U>under</U>", "under", "uuuuu"));
	CHECK (parses_to ("~~gone~~", "gone", "ssss"));
	CHECK (parses_to ("`code`", "code", "cccc"));
	CHECK (parses_to ("a **b** c", "a b c", "..b.."));
	CHECK (parses_to ("**a** and **b**", "a and b", "b.....b"));

	/* nesting, also of three stars */
	CHECK (
		parses_to ("**bold *and italic***", "bold and italic", "bbbbb**********")
	);
	CHECK (parses_to ("***both***", "both", "****"));
	CHECK (parses_to ("<u>**x**</u>", "x", "*"));
	CHECK (parses_to ("*a **b** c*", "a b c", "ii*ii"));
	CHECK (parses_to ("~~**x**~~", "x", "*"));
}

static void test_rules (void)
{
	/* spaces inside the marks: it is not formatting */
	CHECK (parses_to ("2 * 3 * 4", "2 * 3 * 4", "........."));
	CHECK (parses_to ("a ** b ** c", "a ** b ** c", "..........."));
	CHECK (parses_to ("* item", "* item", "......"));

	/* a mark without partner stays */
	CHECK (parses_to ("**open", "**open", "......"));
	CHECK (parses_to ("close**", "close**", "......."));
	CHECK (parses_to ("a *b", "a *b", "...."));
	CHECK (parses_to ("<u>x", "<u>x", "...."));
	CHECK (parses_to ("`code", "`code", "....."));

	/* underscores inside words are text, stars are not special there */
	CHECK (parses_to ("snake_case_name", "snake_case_name", "..............."));
	CHECK (parses_to ("x_1 + y_2", "x_1 + y_2", "........."));
	CHECK (parses_to ("in*side*word", "insideword", "..iiii...."));
	CHECK (parses_to ("_it_ is", "it is", "ii..."));

	/* escapes drop the backslash and keep the mark as text */
	CHECK (parses_to ("\\*not\\*", "*not*", "....."));
	CHECK (parses_to ("\\**x*", "*x", ".i"));
	CHECK (parses_to ("a\\\\b", "a\\b", "..."));
	CHECK (parses_to ("\\pi stays", "\\pi stays", "........."));

	/* inside a code span nothing is a mark */
	CHECK (parses_to ("`a **b** c`", "a **b** c", "ccccccccc"));
	CHECK (parses_to ("**x** `*y*` **z**", "x *y* z", "b.ccc.b"));

	/* the mark ends before the other kinds cross */
	CHECK (parses_to ("**a *b** c*", "a *b c*", "bbbb..."));

	/* UTF-8 goes through untouched */
	CHECK (
		parses_to ("**\xCF\x80** r\xC2\xB2", "\xCF\x80 r\xC2\xB2", "bb....")
	);
}

static void test_append (void)
{
	MlMarkup m;
	MlStr    out;

	ml_markup_init (&m);
	ml_str_init (&out);
	CHECK (ml_markup_parse (&m, lit ("a **b** *c*")) == ML_OK);
	CHECK (ml_markup_has_styles (&m));

	/* a style change resets and sets, and the end resets */
	CHECK (ml_markup_append (&out, &m, 0, m.plain.len) == ML_OK);
	CHECK (
		strcmp (
			ml_str_cstr (out),
			"a \x1b[0m\x1b[1mb\x1b[0m \x1b[0m\x1b[3mc\x1b[0m"
		) == 0
	);

	/* a part of the text, also cut in the middle of a styled run */
	ml_str_clear (&out);
	CHECK (ml_markup_append (&out, &m, 0, 3) == ML_OK);
	CHECK (strcmp (ml_str_cstr (out), "a \x1b[0m\x1b[1mb\x1b[0m") == 0);
	ml_str_clear (&out);
	CHECK (ml_markup_append (&out, &m, 2, 99) == ML_OK);
	CHECK (strstr (ml_str_cstr (out), "\x1b[1mb") != NULL);

	/* text without styles has no escape sequences at all */
	ml_str_clear (&out);
	CHECK (ml_markup_parse (&m, lit ("just text")) == ML_OK);
	CHECK (!ml_markup_has_styles (&m));
	CHECK (ml_markup_append (&out, &m, 0, m.plain.len) == ML_OK);
	CHECK (strcmp (ml_str_cstr (out), "just text") == 0);

	/* underline, strike and code have their own codes */
	ml_str_clear (&out);
	CHECK (ml_markup_parse (&m, lit ("<u>u</u>~~s~~`c`")) == ML_OK);
	CHECK (ml_markup_append (&out, &m, 0, m.plain.len) == ML_OK);
	CHECK (strstr (ml_str_cstr (out), "\x1b[4mu") != NULL);
	CHECK (strstr (ml_str_cstr (out), "\x1b[9ms") != NULL);
	CHECK (strstr (ml_str_cstr (out), "\x1b[36mc") != NULL);

	ml_str_free (&out);
	ml_markup_free (&m);
}

static void test_live (void)
{
	MlMarkup m;
	MlStr    out;

	ml_markup_init (&m);
	ml_str_init (&out);

	/* the text is kept as typed; marks dim, the words between them styled */
	CHECK (ml_markup_parse_live (&m, lit ("a **bold** *it*")) == ML_OK);
	CHECK (strcmp (ml_str_cstr (m.plain), "a **bold** *it*") == 0);
	CHECK (m.styles.len == m.plain.len);
	CHECK (m.styles.data [0] == 0);
	CHECK (m.styles.data [2] == ML_STYLE_MARK && m.styles.data [3] == ML_STYLE_MARK);
	CHECK (m.styles.data [4] == ML_STYLE_BOLD && m.styles.data [7] == ML_STYLE_BOLD);
	CHECK (m.styles.data [8] == ML_STYLE_MARK);
	CHECK (m.styles.data [12] == ML_STYLE_ITALIC);

	/* a mark that has no partner yet stays plain until it gets one */
	CHECK (ml_markup_parse_live (&m, lit ("a **bold")) == ML_OK);
	CHECK (!ml_markup_has_styles (&m));
	CHECK (ml_markup_parse_live (&m, lit ("a **bold*")) == ML_OK);
	CHECK (m.styles.data [3] == 0 || m.styles.data [3] != ML_STYLE_BOLD);

	/* the escape keeps both characters */
	CHECK (ml_markup_parse_live (&m, lit ("\\*x")) == ML_OK);
	CHECK (strcmp (ml_str_cstr (m.plain), "\\*x") == 0);
	CHECK (!ml_markup_has_styles (&m));

	/* drawn: dim marks, bold word */
	CHECK (ml_markup_parse_live (&m, lit ("**b**")) == ML_OK);
	CHECK (ml_markup_append (&out, &m, 0, m.plain.len) == ML_OK);
	CHECK (
		strcmp (
			ml_str_cstr (out),
			"\x1b[0m\x1b[2m**\x1b[0m\x1b[1mb\x1b[0m\x1b[2m**\x1b[0m"
		) == 0
	);

	ml_str_free (&out);
	ml_markup_free (&m);
}

void run_markup_tests (void);

void run_markup_tests (void)
{
	test_live ();
	test_marks ();
	test_rules ();
	test_append ();
}
