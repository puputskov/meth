#include "test.h"

#include <meth/platform/fs.h>
#include <meth/ui/highlight.h>
#include <meth/ui/theme.h>

static MlSlice lit (const char *text)
{
	return ml_slice_cstr (text);
}

/* The escape sequence of a style as a string in out (cleared first). */
static int32_t sgr_is (MlStyle s, MlColorLevel level, const char *expect)
{
	MlStr   out;
	int32_t same;

	ml_str_init (&out);
	ml_style_sgr (&out, s, level);
	same = ml_slice_equal (ml_str_view (out), lit (expect));
	ml_str_free (&out);
	return same;
}

static MlStyle style_of (const char *text)
{
	MlStyle s;

	ml_style_parse (&s, lit (text));
	return s;
}

static void test_names (void)
{
	CHECK (ml_class_from_name (lit ("number")) == ML_CLASS_NUMBER);
	CHECK (ml_class_from_name (lit ("result-error")) == ML_CLASS_RESULT_ERROR);
	CHECK (ml_class_from_name (lit ("nothing")) == ML_CLASS_COUNT);

	for (uint32_t c = 0; c < ML_CLASS_COUNT; c++)
	{
		MlClass back = ml_class_from_name (lit (ml_class_name ((MlClass)c)));

		CHECK (back == (MlClass)c);
	}

	CHECK (ml_color_level_from_name (lit ("256")) == ML_COLORS_256);
	CHECK (ml_color_level_from_name (lit ("off")) == ML_COLORS_OFF);
	CHECK (ml_color_level_from_name (lit ("8")) == ML_COLORS_COUNT);
}

static void test_detect (void)
{
	MlSlice none = lit ("");

	MlSlice tc   = lit ("truecolor");
	MlSlice xt   = lit ("xterm");
	MlSlice xt256 = lit ("xterm-256color");

	CHECK (ml_colors_detect (tc, xt, 0, 0) == ML_COLORS_24BIT);
	CHECK (ml_colors_detect (none, xt256, 0, 0) == ML_COLORS_256);
	CHECK (ml_colors_detect (none, xt, 0, 0) == ML_COLORS_16);
	CHECK (ml_colors_detect (none, none, 0, 1) == ML_COLORS_24BIT);
	CHECK (ml_colors_detect (none, lit ("dumb"), 0, 0) == ML_COLORS_OFF);

	/* NO_COLOR wins over everything */
	CHECK (ml_colors_detect (tc, xt, 1, 1) == ML_COLORS_OFF);
}

static void test_sgr (void)
{
	MlStyle red   = style_of ("#ff0000");
	MlStyle blue  = style_of ("#112233 on #000000 bold");
	MlStyle attrs = style_of ("bold underline");

	CHECK (sgr_is (red, ML_COLORS_24BIT, "\x1b[38;2;255;0;0m"));
	CHECK (sgr_is (red, ML_COLORS_256, "\x1b[38;5;196m"));
	CHECK (sgr_is (red, ML_COLORS_16, "\x1b[91m"));
	CHECK (sgr_is (red, ML_COLORS_OFF, ""));
	CHECK (sgr_is (blue, ML_COLORS_24BIT, "\x1b[1;38;2;17;34;51;48;2;0;0;0m"));
	CHECK (sgr_is (attrs, ML_COLORS_OFF, "\x1b[1;4m"));
	CHECK (sgr_is (style_of ("none"), ML_COLORS_24BIT, ""));

	/* a grey goes to the grey ramp, white to the cube corner */
	CHECK (sgr_is (style_of ("#808080"), ML_COLORS_256, "\x1b[38;5;244m"));
	CHECK (sgr_is (style_of ("#ffffff"), ML_COLORS_256, "\x1b[38;5;231m"));
	CHECK (sgr_is (style_of ("#0000ee"), ML_COLORS_16, "\x1b[34m"));
	CHECK (sgr_is (style_of ("on #cd0000"), ML_COLORS_16, "\x1b[41m"));

	CHECK (ml_style_is_plain (style_of ("none")));
	CHECK (!ml_style_is_plain (attrs));
	CHECK (ml_style_equal (red, style_of ("#f00")));
	CHECK (!ml_style_equal (red, style_of ("#ff0001")));
}

static int32_t parse_ok (MlStyle *s, const char *text)
{
	return ml_style_parse (s, lit (text)) == ML_OK;
}

static void test_style_parse (void)
{
	MlStyle s;

	CHECK (parse_ok (&s, "#b5cea8") && s.fg.set && s.fg.g == 0xCE);
	CHECK (parse_ok (&s, "#f44747 underline") && s.underline);
	CHECK (parse_ok (&s, "bold dim reverse") && s.bold && s.dim && s.reverse);
	CHECK (parse_ok (&s, "#fff on #123") && s.bg.set && s.bg.b == 0x33);

	/* a comment ends the style */
	CHECK (parse_ok (&s, "bold # strong") && s.bold && !s.dim);

	CHECK (ml_style_parse (&s, lit ("italic")) == ML_ERR_PARSE);
	/* not a colour: a comment */
	CHECK (ml_style_parse (&s, lit ("#12345")) == ML_OK && !s.fg.set);
	CHECK (ml_style_parse (&s, lit ("on red")) == ML_ERR_PARSE);
}

static void test_builtin (void)
{
	MlTheme t;

	CHECK (ml_theme_builtin_count () == 4);
	CHECK (ml_theme_builtin (&t, lit ("light")) == ML_OK);
	CHECK (ml_slice_equal (lit (t.name), lit ("light")));
	CHECK (ml_theme_builtin (&t, lit ("neon")) == ML_ERR_INVALID);

	for (uint64_t i = 0; i < ml_theme_builtin_count (); i++)
	{
		CHECK (ml_theme_builtin (&t, lit (ml_theme_builtin_name (i))) == ML_OK);
	}

	/* the high contrast theme keeps 7:1 against its background everywhere */
	ml_theme_builtin (&t, lit ("contrast"));

	for (uint32_t c = 0; c < ML_CLASS_COUNT; c++)
	{
		if (t.styles [c].fg.set)
		{
			CHECK (ml_contrast_ratio (t.styles [c].fg, t.background) >= 7.0);
		}
	}

	/* the light and the contrast themes draw their own background */
	ml_theme_builtin (&t, lit ("light"));
	CHECK (t.paint);
	ml_theme_builtin (&t, lit ("contrast"));
	CHECK (t.paint);
	ml_theme_builtin (&t, lit ("dark"));
	CHECK (!t.paint);

	/*
	 * the themes that paint a background use no faint text: some terminals
	 * draw it wrong on a coloured background
	 */
	for (uint64_t i = 0; i < ml_theme_builtin_count (); i++)
	{
		ml_theme_builtin (&t, lit (ml_theme_builtin_name (i)));

		for (uint32_t c = 0; c < ML_CLASS_COUNT && t.paint; c++)
		{
			CHECK (!t.styles [c].dim);
		}
	}

	/* the mono theme uses no colour at all */
	ml_theme_builtin (&t, lit ("mono"));

	for (uint32_t c = 0; c < ML_CLASS_COUNT; c++)
	{
		CHECK (!t.styles [c].fg.set && !t.styles [c].bg.set);
	}

	CHECK (ml_contrast_ratio (t.background, t.background) < 1.01);
}

static void test_theme_file (void)
{
	MlTheme  t   = ml_theme_default ();
	MlStyle  dim = t.styles [ML_CLASS_UNIT];
	MlStr    report;
	uint64_t bad;

	ml_str_init (&report);

	CHECK (ml_theme_apply_line (&t, lit ("number = #010203")) == ML_OK);
	CHECK (t.styles [ML_CLASS_NUMBER].fg.r == 1);
	CHECK (t.styles [ML_CLASS_NUMBER].fg.b == 3);
	CHECK (ml_theme_apply_line (&t, lit (" error = #f44 underline ")) == ML_OK);
	CHECK (t.styles [ML_CLASS_ERROR].underline);
	CHECK (ml_theme_apply_line (&t, lit ("background = #ffffff")) == ML_OK);
	CHECK (t.background.r == 255);
	CHECK (ml_theme_apply_line (&t, lit ("paint = on")) == ML_OK);
	CHECK (t.paint);
	CHECK (ml_theme_apply_line (&t, lit ("paint = off")) == ML_OK);
	CHECK (!t.paint);
	CHECK (ml_theme_apply_line (&t, lit ("paint = maybe")) == ML_ERR_PARSE);
	CHECK (ml_theme_apply_line (&t, lit ("")) == ML_OK);
	CHECK (ml_theme_apply_line (&t, lit ("# a comment = bold")) == ML_OK);
	CHECK (ml_theme_apply_line (&t, lit ("colour = bold")) == ML_ERR_PARSE);
	CHECK (ml_theme_apply_line (&t, lit ("number bold")) == ML_ERR_PARSE);
	CHECK (ml_theme_apply_line (&t, lit ("number = shiny")) == ML_ERR_PARSE);

	/* a bad line changes nothing, the others still count */
	CHECK (ml_style_equal (t.styles [ML_CLASS_UNIT], dim));
	CHECK (ml_theme_apply_text (
		&t,
		lit ("name = bold\nnonsense\nunit = underline\n"),
		&report,
		&bad
	) == ML_OK);
	CHECK (bad == 1);
	CHECK (t.styles [ML_CLASS_NAME].bold && t.styles [ML_CLASS_UNIT].underline);
	CHECK (ml_slice_equal (
		ml_str_view (report),
		lit ("line 2: not a class = style entry")
	));

	ml_str_free (&report);
}

/* The class of the byte at pos in the text highlighted in a domain. */
static MlClass class_of (MlDomain domain, const char *text, uint64_t pos)
{
	MlRunVec runs;
	MlClass  c;

	MlRunVec_init (&runs);
	ml_highlight (&runs, domain, lit (text), NULL, 0, 0, 0);
	c = ml_runs_class_at (&runs, pos);
	MlRunVec_free (&runs);
	return c;
}

static MlClass class_in (const char *text, uint64_t pos)
{
	return class_of (ML_DOMAIN_ALGEBRA, text, pos);
}

static void test_highlight_proof (void)
{
	const char *step = "x + x = 2x  by simplify";
	const char *thm  = "theorem t: x = 1";
	const char *num  = "x = 1  by step 2";
	const char *cont = "... = 2x  by expand";

	CHECK (class_of (ML_DOMAIN_PROOF, thm, 0) == ML_CLASS_KEYWORD);
	CHECK (class_of (ML_DOMAIN_PROOF, thm, 8) == ML_CLASS_NAME);
	CHECK (class_of (ML_DOMAIN_PROOF, thm, 9) == ML_CLASS_OPERATOR);
	CHECK (class_of (ML_DOMAIN_PROOF, thm, 15) == ML_CLASS_NUMBER);
	CHECK (class_of (ML_DOMAIN_PROOF, step, 0) == ML_CLASS_NAME);
	CHECK (class_of (ML_DOMAIN_PROOF, step, 6) == ML_CLASS_OPERATOR);
	CHECK (class_of (ML_DOMAIN_PROOF, step, 12) == ML_CLASS_KEYWORD);
	CHECK (class_of (ML_DOMAIN_PROOF, step, 16) == ML_CLASS_KEYWORD);
	CHECK (class_of (ML_DOMAIN_PROOF, num, 15) == ML_CLASS_NUMBER);
	CHECK (class_of (ML_DOMAIN_PROOF, "qed", 1) == ML_CLASS_KEYWORD);
	CHECK (class_of (ML_DOMAIN_PROOF, cont, 0) == ML_CLASS_OPERATOR);
}

static void test_highlight_logic (void)
{
	/* boolean: words and symbols of the operators, constants, names */
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a & b", 0) == ML_CLASS_NAME);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a & b", 2) == ML_CLASS_LOGIC);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a and b", 2) == ML_CLASS_LOGIC);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a and b", 4) == ML_CLASS_LOGIC);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a and b", 6) == ML_CLASS_NAME);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "android", 0) == ML_CLASS_NAME);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "~a", 0) == ML_CLASS_LOGIC);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a <-> b", 3) == ML_CLASS_LOGIC);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a <-> b", 4) == ML_CLASS_LOGIC);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a -> b", 2) == ML_CLASS_LOGIC);
	CHECK (
		class_of (ML_DOMAIN_BOOLEAN, "a \xE2\x88\xA7 b", 2)
		== ML_CLASS_LOGIC
	);
	CHECK (
		class_of (ML_DOMAIN_BOOLEAN, "a \xE2\x88\xA7 b", 4)
		== ML_CLASS_LOGIC
	);
	CHECK (
		class_of (ML_DOMAIN_BOOLEAN, "a \xE2\x88\xA7 b", 6)
		== ML_CLASS_NAME
	);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a & true", 4) == ML_CLASS_CONSTANT);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a | 0", 4) == ML_CLASS_CONSTANT);
	CHECK (
		class_of (ML_DOMAIN_BOOLEAN, "\xE2\x8A\xA4", 0)
		== ML_CLASS_CONSTANT
	);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "(a)", 0) == ML_CLASS_PAREN);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "p = a", 2) == ML_CLASS_OPERATOR);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a $ b", 2) == ML_CLASS_ERROR);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "  table a", 2) == ML_CLASS_KEYWORD);
	CHECK (class_of (ML_DOMAIN_BOOLEAN, "a & table", 4) == ML_CLASS_NAME);

	/* lambda: the binder, its names and dot, numbers, names */
	CHECK (class_of (ML_DOMAIN_LAMBDA, "\xCE\xBBx. x", 0) == ML_CLASS_BINDER);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "\xCE\xBBx. x", 2) == ML_CLASS_BINDER);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "\xCE\xBBx. x", 3) == ML_CLASS_BINDER);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "\xCE\xBBx. x", 5) == ML_CLASS_NAME);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "\\x y. x y", 0) == ML_CLASS_BINDER);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "\\x y. x y", 3) == ML_CLASS_BINDER);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "\\x y. x y", 4) == ML_CLASS_BINDER);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "\\x y. x y", 6) == ML_CLASS_NAME);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "plus 2 3", 0) == ML_CLASS_NAME);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "plus 2 3", 5) == ML_CLASS_NUMBER);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "(f x)", 0) == ML_CLASS_PAREN);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "id = x", 3) == ML_CLASS_OPERATOR);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "trace f x", 0) == ML_CLASS_KEYWORD);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "tracer x", 0) == ML_CLASS_NAME);
	CHECK (class_of (ML_DOMAIN_LAMBDA, "x $", 2) == ML_CLASS_ERROR);
}

static void test_highlight (void)
{
	MlRunVec runs;

	/* 2x + sin(pi) */
	CHECK (class_in ("2x + sin(pi)", 0) == ML_CLASS_NUMBER);
	CHECK (class_in ("2x + sin(pi)", 1) == ML_CLASS_NAME);
	CHECK (class_in ("2x + sin(pi)", 2) == ML_CLASS_TEXT);
	CHECK (class_in ("2x + sin(pi)", 3) == ML_CLASS_OPERATOR);
	CHECK (class_in ("2x + sin(pi)", 5) == ML_CLASS_FUNCTION);
	CHECK (class_in ("2x + sin(pi)", 8) == ML_CLASS_PAREN);
	CHECK (class_in ("2x + sin(pi)", 9) == ML_CLASS_CONSTANT);
	CHECK (class_in ("2x + sin(pi)", 11) == ML_CLASS_PAREN);

	/* a name followed by '(' is a function, even an undefined one */
	CHECK (class_in ("f(x)", 0) == ML_CLASS_FUNCTION);
	CHECK (class_in ("f (x)", 0) == ML_CLASS_NAME);

	/* units, keywords, unicode spellings */
	CHECK (class_of (ML_DOMAIN_PHYSICS, "3 km", 2) == ML_CLASS_UNIT);
	CHECK (class_of (ML_DOMAIN_ALGEBRA, "3 km", 2) == ML_CLASS_NAME);
	CHECK (class_of (ML_DOMAIN_PHYSICS, "3 m to km", 4) == ML_CLASS_KEYWORD);
	CHECK (class_of (ML_DOMAIN_ALGEBRA, "3 m to km", 4) == ML_CLASS_NAME);
	CHECK (class_in ("include a.meth", 0) == ML_CLASS_KEYWORD);
	CHECK (class_in ("\xCF\x80", 0) == ML_CLASS_CONSTANT);
	CHECK (class_in ("a \xC3\x97 b", 2) == ML_CLASS_OPERATOR);
	CHECK (class_in ("\xE2\x88\x9A" "4", 0) == ML_CLASS_OPERATOR);

	/* a character that is not syntax is an error, the rest goes on */
	CHECK (class_in ("1 $ 2", 2) == ML_CLASS_ERROR);
	CHECK (class_in ("1 $ 2", 4) == ML_CLASS_NUMBER);

	/* the runs cover the text without gaps, neighbours merge */
	MlRunVec_init (&runs);
	CHECK (
		ml_highlight (&runs, ML_DOMAIN_ALGEBRA, lit ("12+3"), NULL, 0, 0, 0)
		== ML_OK
	);
	CHECK (runs.len == 3);
	CHECK (runs.data [0].start == 0 && runs.data [0].len == 2);
	CHECK (runs.data [2].start == 3 && runs.data [2].cls == ML_CLASS_NUMBER);
	MlRunVec_free (&runs);

	/* an error range overrides the classes in it */
	MlRunVec_init (&runs);
	CHECK (
		ml_highlight (&runs, ML_DOMAIN_ALGEBRA, lit ("1 + 22"), NULL, 0, 4, 2)
		== ML_OK
	);
	CHECK (ml_runs_class_at (&runs, 4) == ML_CLASS_ERROR);
	CHECK (ml_runs_class_at (&runs, 5) == ML_CLASS_ERROR);
	CHECK (ml_runs_class_at (&runs, 0) == ML_CLASS_NUMBER);
	MlRunVec_free (&runs);

	/* empty text has no runs */
	MlRunVec_init (&runs);
	CHECK (
		ml_highlight (&runs, ML_DOMAIN_ALGEBRA, lit (""), NULL, 0, 0, 0)
		== ML_OK
	);
	CHECK (runs.len == 0);
	MlRunVec_free (&runs);
}

static void test_format (void)
{
	/* the text of a theme file gives the theme back, for every built in one */
	for (uint64_t i = 0; i < ml_theme_builtin_count (); i++)
	{
		MlTheme  want;
		MlTheme  got  = ml_theme_default ();
		MlSlice  name = lit (ml_theme_builtin_name (i));
		MlSlice  all;
		MlStr    text;
		uint64_t bad  = 0;

		ml_str_init (&text);
		CHECK (ml_theme_builtin (&want, name) == ML_OK);
		CHECK (ml_theme_format (&text, &want) == ML_OK);

		/* from a theme that differs everywhere, so nothing is left over */
		got.paint = !want.paint;

		for (uint32_t c = 0; c < ML_CLASS_COUNT; c++)
		{
			got.styles [c].bold = 1;
			got.styles [c].reverse = 1;
			got.styles [c].bg = want.background;
		}

		all = ml_str_view (text);
		CHECK (ml_theme_apply_text (&got, all, NULL, &bad) == ML_OK);
		CHECK (bad == 0);
		CHECK (got.background.r == want.background.r);
		CHECK (got.paint == want.paint);

		for (uint32_t c = 0; c < ML_CLASS_COUNT; c++)
		{
			CHECK (ml_style_equal (got.styles [c], want.styles [c]));
		}

		ml_str_free (&text);
	}
}

/*
 * The theme files that ship in data/config/themes are the built in themes: they
 * are generated by `meth theme NAME`, and this finds the ones that were not
 * generated again after a change. Skipped when the tests do not run from the
 * root of the repository.
 */
static void test_shipped_files (void)
{
	for (uint64_t i = 0; i < ml_theme_builtin_count (); i++)
	{
		MlSlice name = lit (ml_theme_builtin_name (i));
		MlStr   path;
		MlStr   text;
		MlTheme want;
		MlTheme got  = ml_theme_default ();

		ml_str_init (&path);
		ml_str_init (&text);
		ml_str_appendf (&path, "data/config/themes/%s.theme", name.data);

		if (ml_fs_exists (ml_str_view (path)))
		{
			uint64_t bad = 0;

			CHECK (ml_fs_read_file (&text, ml_str_view (path)) == ML_OK);
			CHECK (ml_theme_builtin (&want, name) == ML_OK);
			CHECK (
				ml_theme_apply_text (
					&got,
					ml_str_view (text),
					NULL,
					&bad
				) == ML_OK
			);
			CHECK (bad == 0);

			for (uint32_t c = 0; c < ML_CLASS_COUNT; c++)
			{
				CHECK (ml_style_equal (got.styles [c], want.styles [c]));
			}
		}

		ml_str_free (&text);
		ml_str_free (&path);
	}
}

static int32_t background_is (const char *value, const char *theme)
{
	return ml_slice_equal (
		lit (ml_theme_for_background (lit (value))),
		lit (theme)
	);
}

static void test_background (void)
{
	CHECK (background_is ("15;0", "dark"));
	CHECK (background_is ("0;15", "light"));
	CHECK (background_is ("0;7", "light"));
	CHECK (background_is ("default;default;15", "light"));
	CHECK (background_is ("12;8", "dark"));
	CHECK (background_is ("7;1", "dark"));

	/* anything else means nothing */
	CHECK (background_is ("", "dark"));
	CHECK (background_is ("15", "dark"));
	CHECK (background_is ("0;", "dark"));
	CHECK (background_is ("0;x", "dark"));
	CHECK (background_is ("0;150", "dark"));
}

void run_theme_tests (void)
{
	test_background ();
	test_shipped_files ();
	test_format ();
	test_names ();
	test_detect ();
	test_sgr ();
	test_style_parse ();
	test_builtin ();
	test_theme_file ();
	test_highlight ();
	test_highlight_logic ();
	test_highlight_proof ();
}
