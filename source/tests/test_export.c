#include "test.h"

#include <matlog/export/export.h>
#include <matlog/export/math.h>
#include <matlog/cli/cli.h>
#include <matlog/platform/fs.h>
#include <matlog/session/mlog.h>
#include <matlog/ui/editor.h>

#include <string.h>

static MlSlice lit (const char *text)
{
	return ml_slice_cstr (text);
}

/* The typeset form of a statement, in a notation, compared to expect. */
static int32_t typeset_is (
	MlMathNotation n,
	const char    *source,
	const char    *expect
)
{
	MlStr   out;
	int32_t same;

	ml_str_init (&out);
	same = (
		   ml_math_typeset (&out, n, lit (source)) == ML_OK
		&& ml_slice_equal (ml_str_view (out), lit (expect))
	);

	if (!same)
	{
		printf (
			"  \"%s\" gives \"%.*s\", expected \"%s\"\n",
			source,
			(int)out.len,
			out.data,
			expect
		);
	}

	ml_str_free (&out);
	return same;
}

static int32_t latex_is (const char *source, const char *expect)
{
	return typeset_is (ML_MATH_LATEX, source, expect);
}

static void test_latex_math (void)
{
	/* numbers, names and sums */
	CHECK (latex_is ("1+2", "1+2"));
	CHECK (latex_is ("a - b", "a-b"));
	CHECK (latex_is ("a - (b - c)", "a-\\left(b-c\\right)"));
	CHECK (latex_is ("a - (b + c)", "a-\\left(b+c\\right)"));
	CHECK (latex_is ("a + (b - c)", "a+b-c"));

	/* products: a dot between numbers, side by side otherwise */
	CHECK (latex_is ("2*3", "2\\cdot 3"));
	CHECK (latex_is ("2x", "2 x"));
	CHECK (latex_is ("(a+b)*c", "\\left(a+b\\right) c"));

	/* fractions, powers, roots, factorials */
	CHECK (latex_is ("1/2", "\\frac{1}{2}"));
	CHECK (latex_is ("(a+1)/(b-1)", "\\frac{a+1}{b-1}"));
	CHECK (latex_is ("x^2", "{x}^{2}"));
	CHECK (latex_is ("x\xC2\xB2", "{x}^{2}"));
	CHECK (latex_is ("(x+1)^2", "{\\left(x+1\\right)}^{2}"));
	CHECK (latex_is ("2^3^2", "{2}^{{3}^{2}}"));
	CHECK (latex_is ("2^-1", "{2}^{-1}"));
	CHECK (latex_is ("\xE2\x88\x9A" "x", "\\sqrt{x}"));
	CHECK (latex_is ("sqrt(x+1)", "\\sqrt{x+1}"));
	CHECK (latex_is ("abs(x)", "\\left|x\\right|"));
	CHECK (latex_is ("3!", "3!"));
	CHECK (latex_is ("(n+1)!", "\\left(n+1\\right)!"));
	CHECK (latex_is ("-x", "-x"));
	CHECK (latex_is ("-(a+b)", "-\\left(a+b\\right)"));

	/* names: Greek letters, words, subscripts, functions */
	CHECK (latex_is ("pi", "\\pi "));
	CHECK (latex_is ("\xCF\x80", "\\pi "));
	CHECK (latex_is ("alpha + beta", "\\alpha +\\beta "));
	CHECK (latex_is ("kg", "\\mathrm{kg}"));
	CHECK (latex_is ("x_1", "x_{1}"));
	CHECK (latex_is ("sin(x)", "\\sin \\left(x\\right)"));
	CHECK (latex_is ("f(x, y)", "f\\left(x,\\,y\\right)"));
	CHECK (latex_is ("foo(x)", "\\operatorname{foo}\\left(x\\right)"));

	/* statements */
	CHECK (latex_is ("a = 3", "a=3"));
	CHECK (latex_is ("f(x) = x^2", "f\\left(x\\right)={x}^{2}"));
	CHECK (latex_is ("3 ft to m", "3 \\mathrm{ft}\\;\\mathrm{to}\\;m"));

	/* lists and matrices */
	CHECK (latex_is ("[1, 2, 3]", "\\left[1,\\,2,\\,3\\right]"));
	CHECK (latex_is (
		"[[1,2],[3,4]]",
		"\\begin{pmatrix} 1 & 2 \\\\ 3 & 4 \\end{pmatrix}"
	));
	CHECK (latex_is (
		"[[1,2],[3]]",
		"\\left[\\left[1,\\,2\\right],\\,\\left[3\\right]\\right]"
	));
}

static void test_mathml_math (void)
{
	CHECK (typeset_is (ML_MATH_MATHML, "1+2", "<mn>1</mn><mo>+</mo><mn>2</mn>")
	);
	CHECK (typeset_is (
		ML_MATH_MATHML,
		"1/2",
		"<mfrac><mrow><mn>1</mn></mrow><mrow><mn>2</mn></mrow></mfrac>"
	));
	CHECK (typeset_is (
		ML_MATH_MATHML,
		"x^2",
		"<msup><mrow><mi>x</mi></mrow><mrow><mn>2</mn></mrow></msup>"
	));
	CHECK (typeset_is (
		ML_MATH_MATHML,
		"sqrt(x)",
		"<msqrt><mi>x</mi></msqrt>"
	));
	CHECK (typeset_is (ML_MATH_MATHML, "pi", "<mi>\xCF\x80</mi>"));
	CHECK (typeset_is (
		ML_MATH_MATHML,
		"[[1,2],[3,4]]",
		"<mrow><mo>(</mo><mtable>"
		"<mtr><mtd><mn>1</mn></mtd><mtd><mn>2</mn></mtd></mtr>"
		"<mtr><mtd><mn>3</mn></mtd><mtd><mn>4</mn></mtd></mtr>"
		"</mtable><mo>)</mo></mrow>"
	));
}

static void test_typeset_errors (void)
{
	MlStr out;

	ml_str_init (&out);

	/* text that is not a statement is not typeset, and nothing is written */
	CHECK (ml_math_typeset (&out, ML_MATH_LATEX, lit ("1 +")) == ML_ERR_PARSE);
	CHECK (ml_math_typeset (&out, ML_MATH_LATEX, lit ("")) == ML_ERR_PARSE);
	CHECK (out.len == 0);

	/* a very deep formula needs no deep stack */
	for (int32_t i = 0; i < 150; i++)
	{
		ml_str_append (&out, lit ("("));
	}

	ml_str_append (&out, lit ("x"));

	for (int32_t i = 0; i < 150; i++)
	{
		ml_str_append (&out, lit (")"));
	}

	{
		MlStr typeset;

		ml_str_init (&typeset);
		CHECK (
			ml_math_typeset (&typeset, ML_MATH_LATEX, ml_str_view (out))
			== ML_OK
		);
		CHECK (ml_slice_equal (ml_str_view (typeset), lit ("x")));
		ml_str_free (&typeset);
	}

	ml_str_free (&out);
}

static void test_escape (void)
{
	MlStr out;

	ml_str_init (&out);
	CHECK (
		ml_math_escape (&out, ML_MATH_LATEX, lit ("a_b & 50% #1 {x} ~ ^ \\"))
		== ML_OK
	);
	CHECK (ml_slice_equal (
		ml_str_view (out),
		lit (
			"a\\_b \\& 50\\% \\#1 \\{x\\} \\textasciitilde{} "
			"\\textasciicircum{} \\textbackslash{}"
		)
	));
	ml_str_clear (&out);
	CHECK (
		ml_math_escape (&out, ML_MATH_MATHML, lit ("a < b && c > d")) == ML_OK
	);
	CHECK (
		ml_slice_equal (ml_str_view (out), lit ("a &lt; b &amp;&amp; c &gt; d"))
	);
	ml_str_free (&out);
}

/* Exports session text in a format with the words, as a string. */
static int32_t export_is (
	const char     *session,
	MlExportFormat format,
	const char     *words,
	const char     *expect
)
{
	MlSession       s;
	MlResults       res;
	MlExportOptions o    = ml_export_options (format);
	MlSlice         path;
	MlStr           out;
	int32_t         same = 0;

	ml_session_init (&s);
	ml_results_init (&res);
	ml_str_init (&out);

	if (
		   ml_mlog_parse (&s, lit (session)) == ML_OK
		&& ml_results_update (&res, &s, 0) == ML_OK
		&& ml_export_read_words (&o, lit (words), &path) == ML_OK
		&& ml_export (&out, &s, &res, o) == ML_OK
	)
	{
		same = ml_slice_equal (ml_str_view (out), lit (expect));
	}

	if (!same)
	{
		printf (
			"  exported:\n%.*s\n  expected:\n%s\n",
			(int)out.len,
			out.data,
			expect
		);
	}

	ml_str_free (&out);
	ml_results_free (&res);
	ml_session_free (&s);
	return same;
}

static const char *const demo_session =
	"# # Title\n"
	"# Some **bold** and `code` & 50%\n"
	"# second line\n"
	"\n"
	"# next paragraph\n"
	"m:algebra a = 3\n"
	"m:algebra 1/3 + 1/6\n"
	"m:algebra sqrt(2)\n"
	"# ---\n"
	"m:pga2 [1 2; 3 4]\n";

static void test_markdown (void)
{
	CHECK (export_is (
		demo_session,
		ML_EXPORT_MARKDOWN,
		"",
		"# Title\n\n"
		"Some **bold** and `code` & 50%\nsecond line\n\n"
		"next paragraph\n\n"
		"$$\na=3\n$$\n\n"
		"$$\n\\frac{1}{3}+\\frac{1}{6}=\\frac{1}{2}\n$$\n\n"
		"$$\n\\sqrt{2}\\approx 1.41421356\n$$\n\n"
		"---\n\n"
		"```\n[1 2; 3 4]\n```\n\n"
	));
}

static void test_latex (void)
{
	CHECK (export_is (
		demo_session,
		ML_EXPORT_LATEX,
		"body",
		"\\section*{Title}\n\n"
		"Some \\textbf{bold} and \\texttt{code} \\& 50\\% second line\n\n"
		"next paragraph\n\n"
		"\\[\na=3\n\\]\n\n"
		"\\[\n\\frac{1}{3}+\\frac{1}{6}=\\frac{1}{2}\n\\]\n\n"
		"\\[\n\\sqrt{2}\\approx 1.41421356\n\\]\n\n"
		"\\noindent\\rule{\\linewidth}{0.4pt}\n\n"
		"\\begin{verbatim}\n[1 2; 3 4]\n\\end{verbatim}\n\n"
	));

	/* the whole file has a preamble and the document around the body */
	{
		MlSession s;
		MlResults res;
		MlStr     out;

		ml_session_init (&s);
		ml_results_init (&res);
		ml_str_init (&out);
		ml_mlog_parse (&s, lit ("# hello\n"));
		ml_results_update (&res, &s, 0);
		CHECK (
			ml_export (&out, &s, &res, ml_export_options (ML_EXPORT_LATEX))
			== ML_OK
		);
		CHECK (
			strncmp (ml_str_cstr (out), "\\documentclass{article}\n", 24) == 0
		);
		CHECK (strstr (ml_str_cstr (out), "\\usepackage{amsmath}") != NULL);
		CHECK (
			strstr (
				ml_str_cstr (out),
				"\\begin{document}\n\nhello\n\n\\end{document}\n"
			)
			!= NULL
		);
		ml_str_free (&out);
		ml_results_free (&res);
		ml_session_free (&s);
	}
}

static void test_html (void)
{
	CHECK (export_is (
		"# # T\nm:algebra x^2\n",
		ML_EXPORT_MATHML,
		"body",
		"<h1>T</h1>\n"
		"<math display=\"block\"><mrow>"
		"<msup><mrow><mi>x</mi></mrow><mrow><mn>2</mn></mrow></msup>"
		"</mrow></math>\n"
	));

	/* notes are escaped and formatted */
	CHECK (export_is (
		"# a <b> & **c** ~~d~~ <u>e</u>\n",
		ML_EXPORT_MATHML,
		"body",
		"<p>a &lt;b&gt; &amp; <b>c</b> <s>d</s> <u>e</u></p>\n"
	));

	/* the page has a title and the structure of an HTML file */
	{
		MlSession       s;
		MlResults       res;
		MlStr           out;
		MlExportOptions o = ml_export_options (ML_EXPORT_MATHML);

		o.title = lit ("physics");
		ml_session_init (&s);
		ml_results_init (&res);
		ml_str_init (&out);
		ml_mlog_parse (&s, lit ("# hi\n"));
		ml_results_update (&res, &s, 0);
		CHECK (ml_export (&out, &s, &res, o) == ML_OK);
		CHECK (strncmp (ml_str_cstr (out), "<!DOCTYPE html>\n", 16) == 0);
		CHECK (strstr (ml_str_cstr (out), "<title>physics</title>") != NULL);
		CHECK (
			strstr (ml_str_cstr (out), "<p>hi</p>\n</body>\n</html>\n") != NULL
		);
		ml_str_free (&out);
		ml_results_free (&res);
		ml_session_free (&s);
	}
}

static void test_options (void)
{
	/* a range of lines (1 based) */
	CHECK (export_is (
		"# one\n# --\n# two\n\n# three\n",
		ML_EXPORT_MARKDOWN,
		"3-5",
		"two\n\nthree\n\n"
	));
	CHECK (export_is ("# one\n\n# two\n", ML_EXPORT_MARKDOWN, "3", "two\n\n"));

	/* errors only when asked for */
	CHECK (export_is (
		"m:algebra 1/0\n",
		ML_EXPORT_MARKDOWN,
		"",
		"$$\n\\frac{1}{0}\n$$\n\n"
	));
	CHECK (export_is (
		"m:algebra 1/0\n",
		ML_EXPORT_MARKDOWN,
		"errors",
		"$$\n\\frac{1}{0}\n$$\n\n`! division by zero`\n\n"
	));

	/* the words: a path, and what is wrong with them */
	{
		MlExportOptions o = ml_export_options (ML_EXPORT_LATEX);
		MlSlice         path;

		CHECK (
			ml_export_read_words (&o, lit (" body  out.tex errors "), &path)
			== ML_OK
		);
		CHECK (o.body && o.errors && !o.ranged);
		CHECK (ml_slice_equal (path, lit ("out.tex")));
		CHECK (ml_export_read_words (&o, lit ("2-4"), &path) == ML_OK);
		CHECK (o.ranged && o.first == 1 && o.last == 3);
		CHECK (ml_export_read_words (&o, lit ("5-2"), &path) == ML_ERR_INVALID);
		CHECK (ml_export_read_words (&o, lit ("0"), &path) == ML_ERR_INVALID);
		CHECK (
			ml_export_read_words (&o, lit ("a.tex b.tex"), &path)
			== ML_ERR_INVALID
		);
	}

	/* the formats and their names */
	CHECK (ml_export_format_from_name (lit ("latex")) == ML_EXPORT_LATEX);
	CHECK (ml_export_format_from_name (lit ("html")) == ML_EXPORT_MATHML);
	CHECK (ml_export_format_from_name (lit ("md")) == ML_EXPORT_MARKDOWN);
	CHECK (ml_export_format_from_name (lit ("pdf")) == ML_EXPORT_COUNT);
	CHECK (strcmp (ml_export_extension (ML_EXPORT_LATEX), "tex") == 0);
	CHECK (strcmp (ml_export_extension (ML_EXPORT_MATHML), "html") == 0);
	CHECK (strcmp (ml_export_extension (ML_EXPORT_MARKDOWN), "md") == 0);
}

static void command (MlEditor *ed, const char *text)
{
	MlKey ctrl_p = { .code = ML_KEY_CHAR, .cp = 'p', .mods = ML_MOD_CTRL };
	MlKey enter  = { .code = ML_KEY_ENTER, .cp = 0, .mods = 0 };

	CHECK (ml_editor_handle_key (ed, ctrl_p) == ML_OK);

	for (uint64_t i = 0; text [i] != '\0'; i++)
	{
		MlKey k = { .code = ML_KEY_CHAR, .cp = (uint8_t)text [i], .mods = 0 };

		CHECK (ml_editor_handle_key (ed, k) == ML_OK);
	}

	CHECK (ml_editor_handle_key (ed, enter) == ML_OK);
}

static int32_t message_has (const MlEditor *ed, const char *needle)
{
	return strstr (ml_str_cstr (ed->message), needle) != NULL;
}

static void test_editor_command (void)
{
	MlEditor ed;
	MlStr    text;
	MlSlice  file = lit ("matlog_export_test.tmp");

	ml_fs_remove (file);
	ml_str_init (&text);
	ml_editor_init (&ed);
	ml_mlog_parse (&ed.session, lit ("# # Hi\nm:algebra 1/2\n"));
	ml_editor_refresh (&ed);
	ml_editor_begin_note (&ed);

	/* a session without a file needs a path */
	command (&ed, "export markdown");
	CHECK (message_has (&ed, "give a path"));

	/* a bad format or a bad word says what is accepted */
	command (&ed, "export pdf x.tmp");
	CHECK (message_has (&ed, "latex, mathml or markdown"));
	command (&ed, "export markdown 5-2");
	CHECK (message_has (&ed, "range"));

	/* the file is written, and not replaced by accident */
	command (&ed, "export markdown matlog_export_test.tmp");
	CHECK (message_has (&ed, "exported to matlog_export_test.tmp"));
	CHECK (ml_fs_read_file (&text, file) == ML_OK);
	CHECK (ml_slice_equal (
		ml_str_view (text),
		lit ("# Hi\n\n$$\n\\frac{1}{2}\n$$\n\n")
	));
	ml_fs_write_file_atomic (file, lit ("mine"));
	command (&ed, "export markdown matlog_export_test.tmp");
	CHECK (message_has (&ed, "file exists"));
	ml_str_clear (&text);
	ml_fs_read_file (&text, file);
	CHECK (ml_slice_equal (ml_str_view (text), lit ("mine")));

	/* :export! replaces it, a range picks the lines */
	command (&ed, "export! markdown 2 matlog_export_test.tmp");
	ml_str_clear (&text);
	ml_fs_read_file (&text, file);
	CHECK (ml_slice_equal (ml_str_view (text), lit ("$$\n\\frac{1}{2}\n$$\n\n"))
	);

	/* the default file is the session file with the extension of the format */
	ml_session_set_path (&ed.session, lit ("matlog_export_test.tmp.mlog"));
	command (&ed, "export latex body");
	CHECK (message_has (&ed, "exported to matlog_export_test.tmp.tex"));
	CHECK (ml_fs_exists (lit ("matlog_export_test.tmp.tex")));
	ml_fs_remove (lit ("matlog_export_test.tmp.tex"));
	ml_fs_remove (file);
	ml_str_free (&text);
	ml_editor_free (&ed);
}

typedef struct Cli
{
	int32_t code;
	MlStr   out;
	MlStr   err;
} Cli;

static Cli cli (const char *a0, const char *a1, const char *a2, const char *a3)
{
	Cli        c;
	MlSliceVec args;
	const char *list [4] = { a0, a1, a2, a3 };

	MlSliceVec_init (&args);
	ml_str_init (&c.out);
	ml_str_init (&c.err);

	for (uint64_t i = 0; i < 4 && list [i] != NULL; i++)
	{
		MlSliceVec_push (&args, lit (list [i]));
	}

	c.code = ml_cli_run (&args, lit (""), &c.out, &c.err);
	MlSliceVec_free (&args);
	return c;
}

static void test_cli (void)
{
	MlSlice file = lit ("matlog_export_cli.tmp");
	Cli     c;

	ml_fs_write_file_atomic (file, lit ("# # T\nm:algebra 2^3\n"));

	c = cli ("export", "matlog_export_cli.tmp", "markdown", NULL);
	CHECK (c.code == ML_EXIT_OK);
	CHECK (ml_slice_equal (
		ml_str_view (c.out),
		lit ("# T\n\n$$\n{2}^{3}=8\n$$\n\n")
	));
	ml_str_free (&c.out);
	ml_str_free (&c.err);

	c = cli ("export", "matlog_export_cli.tmp", "latex", "body");
	CHECK (c.code == ML_EXIT_OK);
	CHECK (strstr (ml_str_cstr (c.out), "\\section*{T}") != NULL);
	CHECK (strstr (ml_str_cstr (c.out), "documentclass") == NULL);
	ml_str_free (&c.out);
	ml_str_free (&c.err);

	/* with a path the document goes there */
	c = cli (
		"export",
		"matlog_export_cli.tmp",
		"html",
		"matlog_export_cli.tmp.html"
	);
	CHECK (c.code == ML_EXIT_OK && c.out.len == 0);
	CHECK (ml_fs_exists (lit ("matlog_export_cli.tmp.html")));
	ml_fs_remove (lit ("matlog_export_cli.tmp.html"));
	ml_str_free (&c.out);
	ml_str_free (&c.err);

	/* usage errors */
	c = cli ("export", "matlog_export_cli.tmp", "pdf", NULL);
	CHECK (
		c.code == ML_EXIT_USAGE
		&& strstr (ml_str_cstr (c.err), "no such format") != NULL
	);
	ml_str_free (&c.out);
	ml_str_free (&c.err);
	c = cli ("export", NULL, NULL, NULL);
	CHECK (c.code == ML_EXIT_USAGE);
	ml_str_free (&c.out);
	ml_str_free (&c.err);
	c = cli ("export", "no_such_file.tmp", "latex", NULL);
	CHECK (c.code == ML_EXIT_IO);
	ml_str_free (&c.out);
	ml_str_free (&c.err);

	ml_fs_remove (file);
}

/* The typeset form of a boolean (lambda 0) or lambda (lambda 1) line. */
static int32_t logic_is (
	MlMathNotation n,
	int32_t        lambda,
	const char     *source,
	const char     *expect
)
{
	MlStr   out;
	int32_t same;

	ml_str_init (&out);
	same = (
		   ml_logic_typeset (&out, n, lambda, lit (source)) == ML_OK
		&& ml_slice_equal (ml_str_view (out), lit (expect))
	);

	if (!same)
	{
		printf (
			"  \"%s\" gives \"%.*s\", expected \"%s\"\n",
			source,
			(int)out.len,
			out.data,
			expect
		);
	}

	ml_str_free (&out);
	return same;
}

static void test_logic_math (void)
{
	/* the operators in every spelling are the same symbols */
	CHECK (logic_is (ML_MATH_LATEX, 0, "a & b", "a\\land b"));
	CHECK (logic_is (ML_MATH_LATEX, 0, "a and b", "a\\land b"));
	CHECK (logic_is (ML_MATH_LATEX, 0, "a \xE2\x88\xA7 b", "a\\land b"));
	CHECK (logic_is (ML_MATH_LATEX, 0, "a | ~b", "a\\lor \\lnot b"));
	CHECK (logic_is (ML_MATH_LATEX, 0, "a ^ b", "a\\oplus b"));
	CHECK (logic_is (ML_MATH_LATEX, 0, "a -> b", "a\\rightarrow b"));
	CHECK (logic_is (ML_MATH_LATEX, 0, "a <-> b", "a\\leftrightarrow b"));
	CHECK (logic_is (ML_MATH_LATEX, 0, "a == b", "a\\leftrightarrow b"));
	CHECK (logic_is (ML_MATH_LATEX, 0, "p = (a | b)", "p=(a\\lor b)"));
	CHECK (logic_is (ML_MATH_LATEX, 0, "true & false", "\\top \\land \\bot "));
	CHECK (logic_is (ML_MATH_LATEX, 0, "table a", "\\operatorname{table}\\;a"));
	CHECK (
		logic_is (
			ML_MATH_LATEX,
			0,
			"abc & x1",
			"\\mathit{abc}\\land \\mathit{x1}"
		)
	);

	/* a lambda term: binders, applications with a thin space, numerals */
	CHECK (
		logic_is (
			ML_MATH_LATEX,
			1,
			"\xCE\xBBx. x",
			"\\lambda x.\\,x"
		)
	);
	CHECK (logic_is (ML_MATH_LATEX, 1, "\\x. x y", "\\lambda x.\\,x\\,y"));
	CHECK (logic_is (ML_MATH_LATEX, 1, "f (g x)", "f\\,(g\\,x)"));
	CHECK (logic_is (ML_MATH_LATEX, 1, "plus 2 3", "\\mathit{plus}\\,2\\,3"));
	CHECK (
		logic_is (
			ML_MATH_LATEX,
			1,
			"trace f x",
			"\\operatorname{trace}\\;f\\,x"
		)
	);
	CHECK (logic_is (ML_MATH_LATEX, 1, "x'", "x'"));

	/* MathML */
	CHECK (
		logic_is (
			ML_MATH_MATHML,
			0,
			"a & ~b",
			"<mrow><mi>a</mi><mo>\xE2\x88\xA7</mo><mo>\xC2\xAC</mo>"
			"<mi>b</mi></mrow>"
		)
	);
	CHECK (
		logic_is (
			ML_MATH_MATHML,
			1,
			"\xCE\xBBx. x",
			"<mrow><mo>\xCE\xBB</mo><mi>x</mi><mo>.</mo><mi>x</mi></mrow>"
		)
	);
}

static void test_logic_export (void)
{
	MlSession s;
	MlResults r;
	MlStr     out;

	ml_session_init (&s);
	ml_results_init (&r);
	ml_str_init (&out);
	CHECK (
		ml_mlog_parse (
			&s,
			lit ("m:boolean table a & b\nm:lambda succ 1\nm:boolean a &\n")
		) == ML_OK
	);
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);

	/* the line and the value are typeset, the table stays as code */
	CHECK (
		ml_export (&out, &s, &r, ml_export_options (ML_EXPORT_LATEX)) == ML_OK
	);
	CHECK (
		strstr (
			ml_str_cstr (out),
			"\\operatorname{table}\\;a\\land b=a\\land b"
		) != NULL
	);
	CHECK (strstr (ml_str_cstr (out), "\\begin{verbatim}\na b") != NULL);
	CHECK (strstr (ml_str_cstr (out), "\\mathit{succ}\\,1") != NULL);

	/* an error line is left out unless errors are asked for */
	CHECK (strstr (ml_str_cstr (out), "a\\land \n") == NULL);

	ml_str_free (&out);
	ml_results_free (&r);
	ml_session_free (&s);
}

void run_export_tests (void)
{
	test_editor_command ();
	test_cli ();
	test_latex_math ();
	test_logic_math ();
	test_logic_export ();
	test_mathml_math ();
	test_typeset_errors ();
	test_escape ();
	test_markdown ();
	test_latex ();
	test_html ();
	test_options ();
}
