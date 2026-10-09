#include "test.h"
#include "test_keys.h"

#include <matlog/ui/hint.h>
#include <matlog/ui/screen.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static void press (MlEditor *ed, MlKeyCode code)
{
	MlKey k = { .code = code, .cp = 0, .mods = 0 };

	CHECK (ml_editor_handle_key (ed, k) == ML_OK);
}

static void type (MlEditor *ed, const char *text)
{
	for (uint64_t i = 0; text [i] != '\0'; i++)
	{
		MlKey k =
		{
			.code = ML_KEY_CHAR,
			.cp   = (uint8_t)text [i],
			.mods = 0
		};

		CHECK (ml_editor_handle_key (ed, k) == ML_OK);
	}
}

/* The row without its escape sequences. */
static MlResult plain (MlStr *out, MlSlice row)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	ml_str_clear (out);

	while (r == ML_OK && pos < row.len)
	{
		if (row.data [pos] == 0x1b)
		{
			pos += 2;

			while (
				   pos < row.len
				&& !(row.data [pos] >= '@' && row.data [pos] <= '~')
			)
			{
				pos++;
			}

			pos++;
		}

		else
		{
			r = ml_str_append (out, ml_slice_sub (row, pos, 1));
			pos++;
		}
	}

	return r;
}

static int32_t row_is (
	const MlLayout *layout,
	uint64_t       idx,
	const char     *expected
)
{
	MlStr   p;
	int32_t same;

	ml_str_init (&p);
	same = idx < layout->rows.len
		&& plain (&p, ml_str_view (layout->rows.data [idx])) == ML_OK
		&& ml_slice_equal (ml_str_view (p), lit (expected));

	if (!same)
	{
		printf (
			"  row %u is \"%s\", expected \"%s\"\n",
			(uint32_t)idx,
			ml_str_cstr (p),
			expected
		);
	}

	ml_str_free (&p);
	return same;
}

/* Like row_is, for a row that may go on with a result after the text. */
static int32_t row_has (
	const MlLayout *layout,
	uint64_t       idx,
	const char     *prefix
)
{
	MlStr   p;
	int32_t same;

	ml_str_init (&p);
	same = idx < layout->rows.len
		&& plain (&p, ml_str_view (layout->rows.data [idx])) == ML_OK
		&& p.len >= strlen (prefix)
		&& memcmp (p.data, prefix, strlen (prefix)) == 0;

	if (!same)
	{
		printf (
			"  row %u is \"%s\", expected it to start with \"%s\"\n",
			(uint32_t)idx,
			ml_str_cstr (p),
			prefix
		);
	}

	ml_str_free (&p);
	return same;
}

static void fill (MlEditor *ed, uint32_t count)
{
	CHECK (ml_editor_begin (ed) == ML_OK);

	for (uint32_t i = 0; i < count; i++)
	{
		MlStr text;

		ml_str_init (&text);
		CHECK (ml_str_appendf (&text, "line %u", i) == ML_OK);
		type (ed, ml_str_cstr (text));
		press (ed, ML_KEY_ENTER);
		ml_str_free (&text);
	}

	test_leave (ed);
}

static void test_basic_layout (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 40, 6);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "1+2");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "x");
	press (&ed, ML_KEY_ENTER);
	test_normal (&ed, "gm");
	test_normal (&ed, "o");
	type (&ed, "A = 1");
	press (&ed, ML_KEY_ENTER);
	test_key (&ed, ML_KEY_CHAR, 'n', ML_MOD_CTRL);
	type (&ed, "a note");

	l = ml_screen_layout (&s, &ed);
	CHECK (l.result == ML_OK);
	CHECK (l.rows.len == 6);
	CHECK (row_has (&l, 0, " 1 alg> 1+2"));
	CHECK (row_has (&l, 1, " 2    \xE2\x94\x94 x"));
	CHECK (row_is (&l, 2, " 3 mat> A = 1"));
	CHECK (row_is (&l, 3, " 4 a note"));
	CHECK (row_is (&l, 4, ""));
	CHECK (row_is (&l, 5, "-- NOTE -- matrix>         [no name] [+]"));

	/* the cursor sits at the end of the line being edited */
	CHECK (l.cursor.row == 4 && l.cursor.col == 3 + 6 + 1);
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_typing_row (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 24, 5);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "ab");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "x^2 + y");

	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 alg> ab"));
	CHECK (row_is (&l, 1, " 2    \xE2\x94\x94 x\xC2\xB2 + y"));
	CHECK (row_is (&l, 4, "-- MATH -- algebra>     "));
	/*
	 * number 3 + gutter 5, "xÂ² + y" is 6 columns: the cursor is in column 15
	 */
	CHECK (l.cursor.row == 2 && l.cursor.col == 15);
	ml_layout_free (&l);

	/* a new line typed in the middle of the session pushes the rest down */
	test_leave (&ed);
	test_normal (&ed, "kk");
	test_normal (&ed, "N");
	type (&ed, "between");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 alg> ab"));
	CHECK (row_is (&l, 1, " 2 between"));
	CHECK (row_has (&l, 2, " 3 alg> x\xC2\xB2 + y"));
	CHECK (l.cursor.row == 2 && l.cursor.col == 3 + 7 + 1);
	ml_layout_free (&l);

	/* the command line takes the last row */
	test_leave (&ed);
	test_normal (&ed, ":mode vec");
	l = ml_screen_layout (&s, &ed);
	/* the ghost text "tor" completes "vec" to "vector" */
	CHECK (row_is (&l, 4, ":mode vector"));
	CHECK (l.cursor.row == 5 && l.cursor.col == 2 + 8);
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_scrolling (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 20, 6);
	fill (&ed, 20);
	CHECK (ed.session.lines.len == 20);

	/* the cursor is on the last line, which is at the bottom of the area */
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 15);
	CHECK (row_has (&l, 0, "16    \xE2\x94\x82 line 15"));
	CHECK (row_has (&l, 4, "20    \xE2\x94\x94 line 19"));
	CHECK (l.cursor.row == 5);
	ml_layout_free (&l);

	/* moving up inside the window does not scroll */
	test_normal (&ed, "kk");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 15);
	CHECK (l.cursor.row == 3);
	ml_layout_free (&l);

	/* moving above the window scrolls just enough */
	test_normal (&ed, "kkk");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 14);
	CHECK (row_has (&l, 0, "15    \xE2\x94\x82 line 14"));
	CHECK (l.cursor.row == 1);
	ml_layout_free (&l);

	test_normal (&ed, "gg");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 0);
	CHECK (row_has (&l, 0, " 1 alg> line 0"));
	ml_layout_free (&l);

	test_normal (&ed, "G");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 15);
	ml_layout_free (&l);

	/* typing a new line at the end scrolls it into view */
	test_normal (&ed, "o");
	type (&ed, "new");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 16);
	CHECK (row_is (&l, 4, "21    \xE2\x94\x94 new"));
	CHECK (l.cursor.row == 5);
	ml_layout_free (&l);

	/* the session shrinking pulls the window back */
	press (&ed, ML_KEY_ESCAPE);
	test_normal (&ed, "gg");
	test_normal (&ed, "dd");
	for (uint32_t i = 0; i < 17; i++)
	{
		test_normal (&ed, "dd");
	}

	l = ml_screen_layout (&s, &ed);
	CHECK (ed.session.lines.len == 3);
	CHECK (s.top == 0);
	CHECK (row_has (&l, 0, " 1 alg> line 18"));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_clipping (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 14, 4);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "0123456789abcdef");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "short");
	press (&ed, ML_KEY_ENTER);

	/* a long line is clipped to the width */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> 012345"));
	ml_layout_free (&l);

	/* the line being typed scrolls with its cursor */
	test_normal (&ed, "kk");
	press (&ed, ML_KEY_END);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> cdef"));
	CHECK (l.cursor.row == 1 && l.cursor.col == 13);
	ml_layout_free (&l);

	/* wide characters are not cut in half */
	ml_screen_free (&s);
	ml_editor_free (&ed);
	ml_editor_init (&ed);
	ml_screen_init (&s, 12, 3);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "ab");

	{
		MlKey wide = { .code = ML_KEY_CHAR, .cp = 0x4E2D, .mods = 0 };

		CHECK (ml_editor_handle_key (&ed, wide) == ML_OK);
		CHECK (ml_editor_handle_key (&ed, wide) == ML_OK);
	}

	press (&ed, ML_KEY_HOME);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> ab"));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_small_sizes (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 3, 1);
	fill (&ed, 3);

	/* one row: only the status line; narrower than the gutter: no crash */
	l = ml_screen_layout (&s, &ed);
	CHECK (l.result == ML_OK && l.rows.len == 1);
	ml_layout_free (&l);

	ml_screen_resize (&s, 4, 3);
	l = ml_screen_layout (&s, &ed);
	CHECK (l.result == ML_OK && l.rows.len == 3);
	ml_layout_free (&l);

	ml_screen_resize (&s, 1, 2);
	test_normal (&ed, "o");
	type (&ed, "x");
	l = ml_screen_layout (&s, &ed);
	CHECK (l.result == ML_OK && l.rows.len == 2);
	ml_layout_free (&l);

	ml_screen_resize (&s, 0, 0);
	l = ml_screen_layout (&s, &ed);
	CHECK (l.result == ML_OK && l.rows.len == 0);
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_render (void)
{
	MlEditor ed;
	MlScreen s;
	MlStr    out;

	ml_editor_init (&ed);
	ml_str_init (&out);
	ml_screen_init (&s, 20, 4);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "a");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "b");
	press (&ed, ML_KEY_ENTER);

	/* the first frame clears and draws every row */
	CHECK (ml_screen_render (&s, &out, &ed) == ML_OK);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[2J") != NULL);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[1;1H") != NULL);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[4;1H") != NULL);
	CHECK (!s.force);

	/* nothing changed: no row is redrawn, only the cursor is placed */
	ml_str_clear (&out);
	CHECK (ml_screen_render (&s, &out, &ed) == ML_OK);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[K") == NULL);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[2J") == NULL);

	/* moving to the line above redraws the rows involved, not the first */
	test_normal (&ed, "k");
	ml_str_clear (&out);
	CHECK (ml_screen_render (&s, &out, &ed) == ML_OK);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[1;1H") == NULL);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[3;1H") != NULL);

	/* typing a character changes the typed row and the status line only */
	test_normal (&ed, "j");
	test_normal (&ed, "o");
	ml_str_clear (&out);
	CHECK (ml_screen_render (&s, &out, &ed) == ML_OK);
	type (&ed, "c");
	ml_str_clear (&out);
	CHECK (ml_screen_render (&s, &out, &ed) == ML_OK);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[3;1H") != NULL);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[1;1H") == NULL);

	/* a resize redraws everything */
	ml_screen_resize (&s, 30, 5);
	ml_str_clear (&out);
	CHECK (ml_screen_render (&s, &out, &ed) == ML_OK);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[2J") != NULL);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[5;1H") != NULL);
	CHECK (s.drawn.len == 5);

	/* the cursor is hidden while drawing and shown again at the end */
	CHECK (strstr (ml_str_cstr (out), "\x1b[?25l") != NULL);
	CHECK (strstr (ml_str_cstr (out), "\x1b[?25h") != NULL);

	ml_str_free (&out);
	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_line_numbers (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 30, 5);
	fill (&ed, 120);

	/* three digits once the session has 100 lines or more */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, "117    \xE2\x94\x82 line 116"));
	CHECK (row_has (&l, 3, "120    \xE2\x94\x94 line 119"));
	ml_layout_free (&l);

	/* the line being typed has the next number */
	test_normal (&ed, "o");
	type (&ed, "x");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 3, "121    \xE2\x94\x94 x"));
	CHECK (l.cursor.row == 4 && l.cursor.col == 4 + 5 + 1 + 1);
	ml_layout_free (&l);

	/* the numbers follow the lines: after deleting one the rest move up */
	test_leave (&ed);
	test_normal (&ed, "gg");
	test_normal (&ed, "dd");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, "  1 alg> line 1"));
	CHECK (row_has (&l, 1, "  2    \xE2\x94\x82 line 2"));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_lambda_gutter (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 20, 4);
	test_normal (&ed, "gl");
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "x");
	press (&ed, ML_KEY_ENTER);

	/* the lambda domain is marked with the lambda sign, then the > */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 \xCE\xBB  > x"));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_results (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;
	MlStr    raw;

	ml_editor_init (&ed);
	ml_screen_init (&s, 40, 8);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "a = 3");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "1/3 + 1/6");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "1/0");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "f(x) = x");
	press (&ed, ML_KEY_ENTER);
	test_normal (&ed, "N");
	type (&ed, "1 + 1");
	press (&ed, ML_KEY_ENTER);

	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> a = 3  = 3"));
	CHECK (row_is (&l, 1, " 2    \xE2\x94\x82 1/3 + 1/6  = 1/2"));
	CHECK (row_is (&l, 2, " 3    \xE2\x94\x82 1/0  ! division by zero"));
	CHECK (row_is (&l, 3, " 4    \xE2\x94\x94 f(x) = x"));
	CHECK (row_is (&l, 4, " 5 1 + 1"));

	/* results are dim, errors are red */
	ml_str_init (&raw);
	CHECK (ml_str_append (&raw, ml_str_view (l.rows.data [0])) == ML_OK);
	CHECK (strstr (ml_str_cstr (raw), "\x1b[2m= 3\x1b[0m") != NULL);
	ml_str_free (&raw);
	CHECK (strstr (ml_str_cstr (l.rows.data [2]), "\x1b[31m! ") != NULL);
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_results_clipping (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 20, 4);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "10!");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "1000000 + 1");
	press (&ed, ML_KEY_ENTER);

	/* the result is cut to the columns the text leaves */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> 10!  = 36288"));
	CHECK (row_is (&l, 1, " 2    \xE2\x94\x94 1000000 + 1"));
	ml_layout_free (&l);

	/* a width where the text fills the row leaves no result */
	ml_screen_resize (&s, 16, 4);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> 10!  = 3"));
	CHECK (row_is (&l, 1, " 2    \xE2\x94\x94 1000000 "));
	ml_layout_free (&l);

	ml_screen_resize (&s, 10, 4);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> 10"));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_results_follow_edits (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 40, 6);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "a = 2");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "a * 21");
	press (&ed, ML_KEY_ENTER);

	/* a committed line has its result without any extra call */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> a = 2  = 2"));
	CHECK (row_is (&l, 1, " 2    \xE2\x94\x94 a * 21  = 42"));
	ml_layout_free (&l);

	/* editing the first line changes the one below it */
	test_normal (&ed, "gg");
	press (&ed, ML_KEY_END);
	type (&ed, "0");
	test_normal (&ed, "G");
	press (&ed, ML_KEY_END);
	press (&ed, ML_KEY_ENTER);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> a = 20  = 20"));
	CHECK (row_is (&l, 1, " 2    \xE2\x94\x94 a * 21  = 420"));
	ml_layout_free (&l);

	/* deleting a line */
	test_normal (&ed, "gg");
	test_normal (&ed, "dd");
	test_normal (&ed, "G");
	press (&ed, ML_KEY_END);
	press (&ed, ML_KEY_ENTER);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> a * 21  ! undefined name 'a'"));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_matrix_rows (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 40, 12);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "A = [[1,2],[3,4]]");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "1 + 1");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "[1,2,3]");
	press (&ed, ML_KEY_ENTER);

	/* the matrix is drawn under its line, the number stays on its row */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 alg> A = [[1,2],[3,4]]"));
	CHECK (row_is (&l, 1, "          \xE2\x8E\xA1 1 2 \xE2\x8E\xA4"));
	CHECK (row_is (&l, 2, "          \xE2\x8E\xA3 3 4 \xE2\x8E\xA6"));
	CHECK (row_is (&l, 3, " 2    \xE2\x94\x82 1 + 1  = 2"));
	CHECK (row_has (&l, 4, " 3    \xE2\x94\x94 [1,2,3]"));
	CHECK (row_is (&l, 5, "          \xE2\x8E\xA1 1 \xE2\x8E\xA4"));
	ml_layout_free (&l);

	/* the cursor line is still found: line 3 is on row 5 */
	test_normal (&ed, "gg");
	l = ml_screen_layout (&s, &ed);
	CHECK (l.cursor.row == 1);
	ml_layout_free (&l);
	test_normal (&ed, "jj");
	l = ml_screen_layout (&s, &ed);
	CHECK (l.cursor.row == 5);
	ml_layout_free (&l);

	/* scrolling counts rows: with 3 rows of area line 3 needs the top moved */
	ml_screen_resize (&s, 40, 4);
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top >= 1);
	CHECK (l.cursor.row >= 1 && l.cursor.row <= 3);
	CHECK (row_has (&l, l.cursor.row - 1, " 3    \xE2\x94\x94 [1,2,3]"));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_steps_rows (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 50, 14);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "A = [[1,2],[3,4]]");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "det(A)");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "1 + 1");
	press (&ed, ML_KEY_ENTER);

	/* a line without steps says so and opens nothing */
	test_normal (&ed, "s");
	CHECK (!ed.steps_open);
	CHECK (strcmp (ml_str_cstr (ed.message), "no steps for this line") == 0);

	/* s on det (A) opens its steps under it, s again closes them */
	test_normal (&ed, "kk");
	test_normal (&ed, "s");
	CHECK (ed.steps_open && ed.steps_line == 1);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 3, " 2    \xE2\x94\x82 det(A)"));
	CHECK (row_has (&l, 4, "          Eliminate below the diagonal:"));
	CHECK (row_has (&l, 5, "          \xE2\x8E\xA1 1 2 \xE2\x8E\xA4"));
	CHECK (row_has (&l, 7, "          R2 \xE2\x86\x90 R2 \xE2\x88\x92 3"));
	CHECK (row_has (&l, 11, " 3    \xE2\x94\x94 1 + 1"));
	ml_layout_free (&l);
	test_normal (&ed, "s");
	CHECK (!ed.steps_open);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 4, " 3    \xE2\x94\x94 1 + 1"));
	ml_layout_free (&l);

	/* :steps does the same, and an edit of the session closes the steps */
	test_normal (&ed, ":steps");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.steps_open);
	test_normal (&ed, "G");
	test_normal (&ed, "dd");
	CHECK (!ed.steps_open);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_note_styles (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 30, 5);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	test_leave (&ed);
	test_normal (&ed, "N");
	type (&ed, "a **bold** *it* <u>un</u> done");
	press (&ed, ML_KEY_ENTER);

	/* the marks are not shown, the text is styled */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 a bold it un done"));
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "\x1b[1mbold") != NULL);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "\x1b[3mit") != NULL);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "\x1b[4mun") != NULL);
	ml_layout_free (&l);

	/* the width counts the visible text: 18 columns hold "a bold it un" */
	ml_screen_resize (&s, 18, 5);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 a bold it un d"));
	ml_layout_free (&l);

	/* while the note is edited its text is shown as typed */
	ml_screen_resize (&s, 60, 5);
	press (&ed, ML_KEY_UP);
	press (&ed, ML_KEY_END);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 a **bold** *it* <u>un</u> done"));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_bar (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 30, 5);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "1+1");
	press (&ed, ML_KEY_ENTER);

	/* the line number and the gutter are one dim bar, then the text */
	l = ml_screen_layout (&s, &ed);
	CHECK (
		strstr (
			ml_str_cstr (l.rows.data [0]),
			"\x1b[2m 1 alg> \x1b[0m1+1"
		) != NULL
	);
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_brackets (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 30, 5);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "(1+2)*3");

	/* away from a bracket nothing is marked */
	l = ml_screen_layout (&s, &ed);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "\x1b[1;4m") == NULL);
	ml_layout_free (&l);

	/* the cursor right after the closing bracket marks both of the pair */
	for (uint32_t i = 0; i < 2; i++)
	{
		press (&ed, ML_KEY_LEFT);
	}

	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 alg> (1+2)*3"));
	CHECK (
		strstr (
			ml_str_cstr (l.rows.data [0]),
			"\x1b[1;4m(\x1b[0m1+2\x1b[1;4m)\x1b[0m*3"
		) != NULL
	);
	ml_layout_free (&l);

	/* a bracket without a partner is red */
	press (&ed, ML_KEY_END);
	type (&ed, "+(4");
	press (&ed, ML_KEY_LEFT);
	l = ml_screen_layout (&s, &ed);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "\x1b[1;31m(") != NULL);
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_selected_rows (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 20, 6);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "aa");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "bb");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "cc");
	press (&ed, ML_KEY_HOME);
	test_key (&ed, ML_KEY_UP, 0, ML_MOD_SHIFT);
	test_key (&ed, ML_KEY_RIGHT, 0, ML_MOD_SHIFT);

	/* the text from the middle of a line to the middle of another */
	l = ml_screen_layout (&s, &ed);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "\x1b[7m") == NULL);
	CHECK (strstr (ml_str_cstr (l.rows.data [1]), "b\x1b[7mb\x1b[0m") != NULL);
	CHECK (strstr (ml_str_cstr (l.rows.data [1]), "\x1b[7m \x1b[0m") != NULL);
	CHECK (strstr (ml_str_cstr (l.rows.data [2]), "\x1b[7m") == NULL);
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_rule (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 12, 5);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	test_leave (&ed);
	test_normal (&ed, "N");
	type (&ed, "---");

	/* typed as it is */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 ---"));
	ml_layout_free (&l);

	/* a rule across the rest of the row once the note is done */
	press (&ed, ML_KEY_ENTER);
	l = ml_screen_layout (&s, &ed);
	CHECK (
		strstr (
			ml_str_cstr (l.rows.data [0]),
			"\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
			"\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
			"\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
		) != NULL
	);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "---") == NULL);
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_blocks_layout (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 40, 12);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	test_leave (&ed);
	test_normal (&ed, "N");
	type (&ed, "A **note**");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "second line");
	press (&ed, ML_KEY_ENTER);
	test_normal (&ed, "o");
	type (&ed, "a = 1");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "b = 2");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "a + b");
	press (&ed, ML_KEY_ENTER);
	test_normal (&ed, "gm");
	test_normal (&ed, "o");
	type (&ed, "[1,2]");
	press (&ed, ML_KEY_ENTER);

	/* the label is on the first row of a block, a rule runs down, text has
	   no gutter, a new domain is a new block */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 A note"));
	CHECK (row_is (&l, 1, " 2 second line"));
	CHECK (row_is (&l, 2, " 3 alg> a = 1  = 1"));
	CHECK (row_is (&l, 3, " 4    \xE2\x94\x82 b = 2  = 2"));
	CHECK (row_is (&l, 4, " 5    \xE2\x94\x94 a + b  = 3"));
	CHECK (row_has (&l, 5, " 6 mat> [1,2]"));
	ml_layout_free (&l);

	/* a folded block is one row with the number of lines behind it */
	test_normal (&ed, "gg");
	test_normal (&ed, "2j");
	test_normal (&ed, "za");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 2, " 3 alg> \xE2\x96\xB8 a = 1  +2 lines"));
	CHECK (row_has (&l, 3, " 6 mat> [1,2]"));
	CHECK (l.cursor.row == 3);
	ml_layout_free (&l);

	/* so is a folded text block, its text still formatted */
	test_normal (&ed, "gg");
	test_normal (&ed, "za");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 \xE2\x96\xB8 A note  +1 line"));
	CHECK (row_has (&l, 1, " 3 alg> \xE2\x96\xB8 a = 1"));
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "\x1b[1mnote") != NULL);
	ml_layout_free (&l);

	/* the line being typed joins the block above it and opens the block */
	test_normal (&ed, "zR");
	test_normal (&ed, "2j");
	test_normal (&ed, "G");
	test_normal (&ed, "k");
	test_normal (&ed, "ga");
	test_normal (&ed, "o");
	type (&ed, "x");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 4, " 5    \xE2\x94\x82 a + b"));
	CHECK (row_has (&l, 5, " 6    \xE2\x94\x94 x"));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_hit (void)
{
	MlEditor ed;
	MlScreen s;
	MlHit    h;

	ml_editor_init (&ed);
	ml_screen_init (&s, 40, 8);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "1+2");
	press (&ed, ML_KEY_ENTER);
	test_key (&ed, ML_KEY_CHAR, 'n', ML_MOD_CTRL);
	type (&ed, "a note");

	/* " 1 alg> 1+2": the text starts at column 9 (1 based) */
	h = ml_screen_hit (&s, &ed, 11, 1);
	CHECK (h.valid && !h.folded && h.line == 0 && h.byte == 2);
	h = ml_screen_hit (&s, &ed, 2, 1);
	CHECK (h.valid && h.line == 0 && h.byte == 0);
	h = ml_screen_hit (&s, &ed, 30, 1);
	CHECK (h.valid && h.line == 0 && h.byte == 3);

	/* a note has no gutter text; the line being typed is the second */
	h = ml_screen_hit (&s, &ed, 7, 2);
	CHECK (h.valid && h.line == 1 && h.byte == 3);

	/* an empty row and the status line are not text */
	h = ml_screen_hit (&s, &ed, 5, 5);
	CHECK (!h.valid);
	h = ml_screen_hit (&s, &ed, 5, 8);
	CHECK (!h.valid);
	h = ml_screen_hit (&s, &ed, 0, 0);
	CHECK (!h.valid);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

/* 1 when the row has text in it. */
static int32_t row_contains (
	const MlLayout *layout,
	uint64_t       idx,
	const char     *text
)
{
	MlSlice row  = ml_str_view (layout->rows.data [idx]);
	MlSlice want = lit (text);
	int32_t found = 0;

	for (uint64_t i = 0; i + want.len <= row.len && !found; i++)
	{
		found = ml_slice_equal (ml_slice_sub (row, i, want.len), want);
	}

	return found;
}

static void test_hint (void)
{
	MlEditor ed;
	MlScreen s;
	MlStr    p;
	MlLayout layout;
	uint64_t width = ml_hint_width (ml_hint_at (0));
	uint64_t pad   = (100 - width) / 2;

	ml_editor_init (&ed);
	ml_editor_begin_note (&ed);
	ml_screen_init (&s, 100, 10);
	ml_str_init (&p);

	/* an empty editor shows a hint in the middle, ghost text, keys bold */
	CHECK (ml_screen_hint_active (&ed));
	layout = ml_screen_layout (&s, &ed);
	CHECK (layout.result == ML_OK);
	plain (&p, ml_str_view (layout.rows.data [4]));
	CHECK (p.len == pad + width);
	CHECK (p.data [pad] == 'J');
	CHECK (row_contains (&layout, 4, "\x1b[2m"));
	CHECK (row_contains (&layout, 4, "\x1b[1m"));
	CHECK (layout.rows.data [3].len == 0 && layout.rows.data [5].len == 0);
	ml_layout_free (&layout);

	/* the next hint comes with the tick */
	s.hint_tick = 1;
	layout = ml_screen_layout (&s, &ed);
	plain (&p, ml_str_view (layout.rows.data [4]));
	CHECK (p.data [(100 - ml_hint_width (ml_hint_at (1))) / 2] == 'T');
	ml_layout_free (&layout);

	/* with a theme the keys have their own colour */
	ed.highlight = 1;
	layout = ml_screen_layout (&s, &ed);
	CHECK (row_contains (&layout, 4, "\x1b[1;38;2;79;193;255m"));
	ml_layout_free (&layout);

	/* typing makes it go at once */
	type (&ed, "a");
	CHECK (!ml_screen_hint_active (&ed));
	layout = ml_screen_layout (&s, &ed);
	CHECK (layout.rows.data [4].len == 0);
	ml_layout_free (&layout);

	/* a window too small to have room for it shows none */
	ml_editor_free (&ed);
	ml_editor_init (&ed);
	ml_editor_begin_note (&ed);
	ml_screen_resize (&s, 100, 5);
	layout = ml_screen_layout (&s, &ed);
	CHECK (layout.rows.data [2].len == 0);
	ml_layout_free (&layout);

	/* a window too narrow shows none either */
	ml_screen_resize (&s, 20, 10);
	layout = ml_screen_layout (&s, &ed);
	CHECK (layout.rows.data [4].len == 0);
	ml_layout_free (&layout);

	ml_str_free (&p);
	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_empty_gutter (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_editor_begin (&ed);
	ml_screen_init (&s, 40, 8);

	/* an empty line that starts a block shows the label of its domain */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 alg> "));
	ml_layout_free (&l);

	/* one that goes on a block of its own has no label */
	type (&ed, "1+2");
	press (&ed, ML_KEY_ENTER);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 1, " 2    "));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_block_start_gutter (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;
	MlKey    k = { .code = ML_KEY_CHAR, .cp = 'b', .mods = ML_MOD_CTRL };
	MlKey    ctrl_enter =
	{
		.code = ML_KEY_ENTER,
		.cp   = 0,
		.mods = ML_MOD_CTRL
	};

	ml_editor_init (&ed);
	ml_editor_begin_note (&ed);
	type (&ed, "intro");
	press (&ed, ML_KEY_ENTER);
	ml_screen_init (&s, 40, 8);

	/* while the domain of the block is chosen (Ctrl+B), and after it */
	CHECK (ml_editor_handle_key (&ed, k) == ML_OK);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 1, " 2 alg> "));
	ml_layout_free (&l);
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_MATH);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 1, " 2 alg> "));
	ml_layout_free (&l);

	/* Ctrl+Enter starts the block at once, with the label */
	ml_editor_free (&ed);
	ml_editor_init (&ed);
	ml_editor_begin_note (&ed);
	CHECK (ml_editor_handle_key (&ed, ctrl_enter) == ML_OK);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 alg> "));
	ml_layout_free (&l);

	/* also on the first line of a file that is already there */
	ml_editor_free (&ed);
	ml_editor_init (&ed);
	ml_editor_begin_note (&ed);
	type (&ed, "x");
	press (&ed, ML_KEY_ENTER);
	press (&ed, ML_KEY_UP);
	press (&ed, ML_KEY_END);
	press (&ed, ML_KEY_BACKSPACE);
	CHECK (ed.edit_existing && ed.edit_index == 0);
	CHECK (ml_editor_handle_key (&ed, ctrl_enter) == ML_OK);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 alg> "));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static void test_themed_rows (void)
{
	MlEditor ed;
	MlScreen s;
	MlStr    p;
	MlLayout layout;

	ml_editor_init (&ed);
	ml_editor_begin (&ed);
	ml_screen_init (&s, 40, 8);
	ml_str_init (&p);
	type (&ed, "2+x");

	/* without a theme the line is plain text */
	layout = ml_screen_layout (&s, &ed);
	CHECK (!row_contains (&layout, 0, "\x1b[38;"));
	ml_layout_free (&layout);

	/* with one the number, the operator and the name have their colours */
	ed.highlight = 1;
	layout = ml_screen_layout (&s, &ed);
	CHECK (row_contains (&layout, 0, "\x1b[38;2;181;206;168m" "2"));
	CHECK (row_contains (&layout, 0, "\x1b[38;2;212;212;212m" "+"));
	CHECK (row_contains (&layout, 0, "\x1b[38;2;156;220;254m" "x"));
	plain (&p, ml_str_view (layout.rows.data [0]));
	CHECK (p.len >= 11 && memcmp (p.data, " 1 alg> 2+x", 11) == 0);
	ml_layout_free (&layout);

	/* the colour level changes the escape sequences, not the text */
	ed.colors = ML_COLORS_16;
	layout = ml_screen_layout (&s, &ed);
	CHECK (!row_contains (&layout, 0, "\x1b[38;2;"));
	CHECK (row_contains (&layout, 0, "\x1b[3"));
	ml_layout_free (&layout);

	/* a committed line is drawn the same way, with its result */
	ed.colors = ML_COLORS_24BIT;
	press (&ed, ML_KEY_ESCAPE);
	layout = ml_screen_layout (&s, &ed);
	CHECK (row_contains (&layout, 0, "\x1b[38;2;181;206;168m" "2"));
	ml_layout_free (&layout);

	ml_str_free (&p);
	ml_screen_free (&s);
	ml_editor_free (&ed);
}

void run_screen_tests (void)
{
	test_hint ();
	test_empty_gutter ();
	test_block_start_gutter ();
	test_themed_rows ();
	test_basic_layout ();
	test_hit ();
	test_typing_row ();
	test_scrolling ();
	test_clipping ();
	test_small_sizes ();
	test_render ();
	test_line_numbers ();
	test_lambda_gutter ();
	test_results ();
	test_results_clipping ();
	test_results_follow_edits ();
	test_matrix_rows ();
	test_steps_rows ();
	test_note_styles ();
	test_bar ();
	test_brackets ();
	test_selected_rows ();
	test_rule ();
	test_blocks_layout ();
}
