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
	type (&ed, "i");
	type (&ed, "A = 1");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "");
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "n");
	type (&ed, "a note");
	press (&ed, ML_KEY_ESCAPE);

	l = ml_screen_layout (&s, &ed);
	CHECK (l.result == ML_OK);
	CHECK (l.rows.len == 6);
	CHECK (row_is (&l, 0, " 1 alg> 1+2"));
	CHECK (row_is (&l, 1, " 2 alg> x"));
	CHECK (row_is (&l, 2, " 3 mat> A = 1"));
	CHECK (row_is (&l, 3, " 4   #  a note"));
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
	CHECK (row_is (&l, 0, " 1 alg> ab"));
	CHECK (row_is (&l, 1, " 2 alg> x\xC2\xB2 + y"));
	CHECK (row_is (&l, 4, "-- INSERT -- algebra>   "));
	/*
	 * number 3 + gutter 5, "xÂ² + y" is 6 columns: the cursor is in column 15
	 */
	CHECK (l.cursor.row == 2 && l.cursor.col == 15);
	ml_layout_free (&l);

	/* a new line typed in the middle of the session pushes the rest down */
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "kk");
	type (&ed, "n");
	type (&ed, "between");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> ab"));
	CHECK (row_is (&l, 1, " 2   #  between"));
	CHECK (row_is (&l, 2, " 3 alg> x\xC2\xB2 + y"));
	CHECK (l.cursor.row == 2 && l.cursor.col == 8 + 7 + 1);
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
	CHECK (row_is (&l, 0, "16 alg> line 15"));
	CHECK (row_is (&l, 4, "20 alg> line 19"));
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
	CHECK (row_is (&l, 0, "15 alg> line 14"));
	CHECK (l.cursor.row == 1);
	ml_layout_free (&l);

	type (&ed, "gg");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 0);
	CHECK (row_is (&l, 0, " 1 alg> line 0"));
	ml_layout_free (&l);

	type (&ed, "G");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 15);
	ml_layout_free (&l);

	/* typing a new line at the end scrolls it into view */
	type (&ed, "i");
	type (&ed, "new");
	l = ml_screen_layout (&s, &ed);
	CHECK (s.top == 16);
	CHECK (row_is (&l, 4, "21 alg> new"));
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
	CHECK (row_is (&l, 0, " 1 alg> line 18"));
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
	type (&ed, "i");
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
	type (&ed, "i");
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
	CHECK (row_is (&l, 0, "117 alg> line 116"));
	CHECK (row_is (&l, 3, "120 alg> line 119"));
	ml_layout_free (&l);

	/* the line being typed has the next number */
	type (&ed, "i");
	type (&ed, "x");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 3, "121 alg> x"));
	CHECK (l.cursor.row == 4 && l.cursor.col == 4 + 5 + 1 + 1);
	ml_layout_free (&l);

	/* the numbers follow the lines: after deleting one the rest move up */
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "gg");
	type (&ed, "dd");
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, "  1 alg> line 1"));
	CHECK (row_is (&l, 1, "  2 alg> line 2"));
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
	type (&ed, "n");
	type (&ed, "1 + 1");
	press (&ed, ML_KEY_ESCAPE);

	/* nothing is shown before the results are computed */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> a = 3"));
	ml_layout_free (&l);

	CHECK (ml_results_update (&ed.results, &ed.session, 0) == ML_OK);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> a = 3  = 3"));
	CHECK (row_is (&l, 1, " 2 alg> 1/3 + 1/6  = 1/2"));
	CHECK (row_is (&l, 2, " 3 alg> 1/0  ! division by zero"));
	CHECK (row_is (&l, 3, " 4 alg> f(x) = x"));
	CHECK (row_is (&l, 4, " 5   #  1 + 1"));

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
	CHECK (ml_results_update (&ed.results, &ed.session, 0) == ML_OK);

	/* the result is cut to the columns the text leaves */
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> 10!  = 36288"));
	CHECK (row_is (&l, 1, " 2 alg> 1000000 + 1"));
	ml_layout_free (&l);

	/* a width where the text fills the row leaves no result */
	ml_screen_resize (&s, 16, 4);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> 10!  = 3"));
	CHECK (row_is (&l, 1, " 2 alg> 1000000 "));
	ml_layout_free (&l);

	ml_screen_resize (&s, 10, 4);
	l = ml_screen_layout (&s, &ed);
	CHECK (row_is (&l, 0, " 1 alg> 10"));
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
}
