#include "test.h"

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

	press (ed, ML_KEY_ESCAPE);
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
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "gm");
	type (&ed, "o");
	type (&ed, "A = 1");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "");
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "N");
	type (&ed, "a note");
	press (&ed, ML_KEY_ESCAPE);

	l = ml_screen_layout (&s, &ed);
	CHECK (l.result == ML_OK);
	CHECK (l.rows.len == 6);
	CHECK (row_has (&l, 0, " 1 alg> 1+2"));
	CHECK (row_has (&l, 1, " 2    \xE2\x94\x94 x"));
	CHECK (row_is (&l, 2, " 3 mat> A = 1"));
	CHECK (row_is (&l, 3, " 4 a note"));
	CHECK (row_is (&l, 4, ""));
	CHECK (row_is (&l, 5, "-- NORMAL -- matrix>       [no name] [+]"));

	/* the cursor sits at the start of the highlighted cursor line */
	CHECK (l.cursor.row == 4 && l.cursor.col == 1);
	CHECK (ed.session.cursor == 3);
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
	CHECK (row_is (&l, 4, "-- INSERT -- algebra>   "));
	/*
	 * number 3 + gutter 5, "xÂ² + y" is 6 columns: the cursor is in column 15
	 */
	CHECK (l.cursor.row == 2 && l.cursor.col == 15);
	ml_layout_free (&l);

	/* a new line typed in the middle of the session pushes the rest down */
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "kk");
	type (&ed, "N");
	type (&ed, "between");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 alg> ab"));
	CHECK (row_is (&l, 1, " 2 between"));
	CHECK (row_has (&l, 2, " 3 alg> x\xC2\xB2 + y"));
	CHECK (l.cursor.row == 2 && l.cursor.col == 3 + 7 + 1);
	ml_layout_free (&l);

	/* the command line takes the last row */
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, ":mode vec");
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
	type (&ed, "kk");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 15);
	CHECK (l.cursor.row == 3);
	ml_layout_free (&l);

	/* moving above the window scrolls just enough */
	type (&ed, "kkk");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 14);
	CHECK (row_has (&l, 0, "15    \xE2\x94\x82 line 14"));
	CHECK (l.cursor.row == 1);
	ml_layout_free (&l);

	type (&ed, "gg");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 0);
	CHECK (row_has (&l, 0, " 1 alg> line 0"));
	ml_layout_free (&l);

	type (&ed, "G");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 15);
	ml_layout_free (&l);

	/* typing a new line at the end scrolls it into view */
	type (&ed, "o");
	type (&ed, "new");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 16);
	CHECK (row_is (&l, 4, "21    \xE2\x94\x94 new"));
	CHECK (l.cursor.row == 5);
	ml_layout_free (&l);

	/* the session shrinking pulls the window back */
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "gg");
	type (&ed, "dd");
	for (uint32_t i = 0; i < 17; i++)
	{
		type (&ed, "dd");
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
	press (&ed, ML_KEY_ESCAPE);

	/* a long line is clipped to the width */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> 012345"));
	ml_layout_free (&l);

	/* the line being typed scrolls with its cursor */
	type (&ed, "kk");
	press (&ed, ML_KEY_ENTER);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> cdef"));
	CHECK (l.cursor.row == 1 && l.cursor.col == 13);
	ml_layout_free (&l);

	/* wide characters are not cut in half */
	press (&ed, ML_KEY_ESCAPE);
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

	press (&ed, ML_KEY_ESCAPE);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> ab\xE4\xB8\xAD"));
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
	type (&ed, "o");
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
	press (&ed, ML_KEY_ESCAPE);

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

	/* moving the cursor line redraws the two rows involved and the status */
	type (&ed, "k");
	ml_str_clear (&out);
	CHECK (ml_screen_render (&s, &out, &ed) == ML_OK);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[1;1H") != NULL);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[2;1H") != NULL);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[3;1H") == NULL);

	/* typing a character changes the typed row and the status line only */
	type (&ed, "j");
	type (&ed, "o");
	ml_str_clear (&out);
	CHECK (ml_screen_render (&s, &out, &ed) == ML_OK);
	type (&ed, "c");
	ml_str_clear (&out);
	CHECK (ml_screen_render (&s, &out, &ed) == ML_OK);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[3;1H") != NULL);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[1;1H") == NULL);
	CHECK (strstr           (ml_str_cstr (out), "\x1b[2;1H") == NULL);

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
	type (&ed, "o");
	type (&ed, "x");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 3, "121    \xE2\x94\x94 x"));
	CHECK (l.cursor.row == 4 && l.cursor.col == 4 + 5 + 1 + 1);
	ml_layout_free (&l);

	/* the numbers follow the lines: after deleting one the rest move up */
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "gg");
	type (&ed, "dd");
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
	type (&ed, "gl");
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "x");
	press (&ed, ML_KEY_ESCAPE);

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
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "N");
	type (&ed, "1 + 1");
	press (&ed, ML_KEY_ESCAPE);

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
	press (&ed, ML_KEY_ESCAPE);

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
	press (&ed, ML_KEY_ESCAPE);

	/* a committed line has its result without any extra call */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> a = 2  = 2"));
	CHECK (row_is (&l, 1, " 2    \xE2\x94\x94 a * 21  = 42"));
	ml_layout_free (&l);

	/* editing the first line changes the one below it */
	type (&ed, "gg");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "0");
	press (&ed, ML_KEY_ENTER);
	press (&ed, ML_KEY_ESCAPE);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> a = 20  = 20"));
	CHECK (row_is (&l, 1, " 2    \xE2\x94\x94 a * 21  = 420"));
	ml_layout_free (&l);

	/* deleting a line */
	type (&ed, "gg");
	type (&ed, "dd");
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
	press (&ed, ML_KEY_ESCAPE);

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
	type (&ed, "gg");
	l = ml_screen_layout (&s, &ed);
	CHECK (l.cursor.row == 1);
	ml_layout_free (&l);
	type (&ed, "jj");
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
	press (&ed, ML_KEY_ESCAPE);

	/* a line without steps says so and opens nothing */
	type (&ed, "s");
	CHECK (!ed.steps_open);
	CHECK (strcmp (ml_str_cstr (ed.message), "no steps for this line") == 0);

	/* s on det (A) opens its steps under it, s again closes them */
	type (&ed, "k");
	type (&ed, "s");
	CHECK (ed.steps_open && ed.steps_line == 1);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 3, " 2    \xE2\x94\x82 det(A)"));
	CHECK (row_has (&l, 4, "          Eliminate below the diagonal:"));
	CHECK (row_has (&l, 5, "          \xE2\x8E\xA1 1 2 \xE2\x8E\xA4"));
	CHECK (row_has (&l, 7, "          R2 \xE2\x86\x90 R2 \xE2\x88\x92 3"));
	CHECK (row_has (&l, 11, " 3    \xE2\x94\x94 1 + 1"));
	ml_layout_free (&l);
	type (&ed, "s");
	CHECK (!ed.steps_open);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 4, " 3    \xE2\x94\x94 1 + 1"));
	ml_layout_free (&l);

	/* :steps does the same, and an edit of the session closes the steps */
	type (&ed, ":steps");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.steps_open);
	type (&ed, "G");
	type (&ed, "dd");
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
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "N");
	type (&ed, "a **bold** *it* <u>un</u> done");
	press (&ed, ML_KEY_ESCAPE);

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
	press (&ed, ML_KEY_ENTER);
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
	press (&ed, ML_KEY_ESCAPE);

	/* the line number and the gutter are one dim bar, then the text */
	l = ml_screen_layout (&s, &ed);
	CHECK (
		strstr (
			ml_str_cstr (l.rows.data [0]),
			"\x1b[7m 1 alg> \x1b[0m1+1"
		) != NULL
	);
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
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "N");
	type (&ed, "---");

	/* typed as it is */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 0, " 1 ---"));
	ml_layout_free (&l);

	/* a rule across the rest of the row once the note is done */
	press (&ed, ML_KEY_ESCAPE);
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
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "N");
	type (&ed, "A **note**");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "second line");
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "o");
	type (&ed, "a = 1");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "b = 2");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "a + b");
	press (&ed, ML_KEY_ENTER);
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "gm");
	type (&ed, "o");
	type (&ed, "[1,2]");
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "gg");

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
	type (&ed, "2j");
	type (&ed, "za");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 2, " 3 alg> \xE2\x96\xB8 a = 1  +2 lines"));
	CHECK (row_has (&l, 3, " 6 mat> [1,2]"));
	CHECK (l.cursor.row == 3);
	ml_layout_free (&l);

	/* so is a folded text block, its text still formatted */
	type (&ed, "gg");
	type (&ed, "za");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 \xE2\x96\xB8 A note  +1 line"));
	CHECK (row_has (&l, 1, " 3 alg> \xE2\x96\xB8 a = 1"));
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "\x1b[1mnote") != NULL);
	ml_layout_free (&l);

	/* the line being typed joins the block above it and opens the block */
	type (&ed, "zR");
	type (&ed, "2j");
	type (&ed, "G");
	type (&ed, "k");
	type (&ed, "ga");
	type (&ed, "o");
	type (&ed, "x");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_has (&l, 4, " 5    \xE2\x94\x82 a + b"));
	CHECK (row_has (&l, 5, " 6    \xE2\x94\x94 x"));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

void run_screen_tests (void)
{
	test_basic_layout ();
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
	test_rule ();
	test_blocks_layout ();
}
