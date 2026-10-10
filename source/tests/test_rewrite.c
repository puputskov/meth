#include "test.h"

#include <meth/core/utf8.h>
#include <meth/ui/rewrite.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* Types a string one code point at a time through the rewriter. */
static MlResult type (MlRewriter *rw, MlLineEdit *e, const char *text)
{
	MlResult r   = ML_OK;
	MlSlice  s   = lit (text);
	uint64_t pos = 0;

	while (r == ML_OK && pos < s.len)
	{
		MlDecoded d = ml_utf8_decode (ml_slice_sub (s, pos, s.len - pos));

		r = ml_rewriter_insert_cp (rw, e, d.cp);
		pos += d.size;
	}

	return r;
}

/* Types text in a fresh line of the given domain and checks the result. */
static int32_t types_as (
	MlDomain   domain,
	const char *text,
	const char *expected
)
{
	MlRewriter rw;
	MlLineEdit e;
	int32_t    ok;

	ml_rewriter_init (&rw, domain);
	ml_lineedit_init (&e);

	ok = type (
		&rw,
		&e,
		text
	) == ML_OK && ml_slice_equal (ml_str_view (e.text), lit (expected));

	if (!ok)
	{
		printf (
			"  typed \"%s\" in %s, got \"%s\", expected \"%s\"\n",
			text,
			ml_domain_name (domain),
			ml_str_cstr (e.text),
			expected
		);
	}

	ml_lineedit_free (&e);
	ml_rewriter_free (&rw);
	return ok;
}

/* Same as types_as, but flushes the pending token at the end first. */
static int32_t types_flushed (
	MlDomain   domain,
	const char *text,
	const char *expected
)
{
	MlRewriter rw;
	MlLineEdit e;
	int32_t    ok;

	ml_rewriter_init (&rw, domain);
	ml_lineedit_init (&e);

	ok = (
		   type (&rw, &e, text) == ML_OK
		&& ml_rewriter_flush (&rw, &e) == ML_OK
		&& ml_slice_equal (ml_str_view (e.text), lit (expected))
	);

	if (!ok)
	{
		printf (
			"  typed+flushed \"%s\" in %s, got \"%s\", expected \"%s\"\n",
			text,
			ml_domain_name (domain),
			ml_str_cstr (e.text),
			expected
		);
	}

	ml_lineedit_free (&e);
	ml_rewriter_free (&rw);
	return ok;
}

static void test_acceptance (void)
{
	/* the milestone's acceptance example */
	CHECK (
		types_as (
			ML_DOMAIN_ALGEBRA,
			"x^2 + sqrt(y)",
			"x\xC2\xB2 + \xE2\x88\x9A" "y"
		)
	);
}

static void test_literals (void)
{
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "a<=b", "a\xE2\x89\xA4" "b"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "a>=b", "a\xE2\x89\xA5" "b"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "a!=b", "a\xE2\x89\xA0" "b"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "a->b", "a\xE2\x86\x92" "b"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "a=>b", "a\xE2\x87\x92" "b"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "a<->b", "a\xE2\x86\x94" "b"));

	/* partial spellings are left alone */
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "a<b", "a<b"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "a-b", "a-b"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "a=b", "a=b"));
}

static void test_words (void)
{
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "pi ", "\xCF\x80 "));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "2*pi+1", "2*\xCF\x80+1"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "sin(pi)", "sin(\xCF\x80)"));

	/* only whole words, and only once the token is complete */
	CHECK (types_as      (ML_DOMAIN_ALGEBRA, "pi", "pi"));
	CHECK (types_as      (ML_DOMAIN_ALGEBRA, "spin ", "spin "));
	CHECK (types_as      (ML_DOMAIN_ALGEBRA, "pi2 ", "pi2 "));
	CHECK (types_as      (ML_DOMAIN_ALGEBRA, "2pi ", "2pi "));
	CHECK (types_flushed (ML_DOMAIN_ALGEBRA, "pi", "\xCF\x80"));
	CHECK (types_flushed (ML_DOMAIN_ALGEBRA, "spin", "spin"));
}

static void test_escapes (void)
{
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "\\alpha ", "\xCE\xB1 "));
	CHECK (
		types_as (ML_DOMAIN_ALGEBRA, "\\alpha+\\beta)", "\xCE\xB1+\xCE\xB2)")
	);
	CHECK (types_as      (ML_DOMAIN_ALGEBRA, "\\Omega ", "\xCE\xA9 "));
	CHECK (types_as      (ML_DOMAIN_ALGEBRA, "\\pi ", "\xCF\x80 "));
	CHECK (types_as      (ML_DOMAIN_ALGEBRA, "a \\leq b", "a \xE2\x89\xA4 b"));
	CHECK (types_as      (ML_DOMAIN_ALGEBRA, "\\infty ", "\xE2\x88\x9E "));
	CHECK (types_flushed (ML_DOMAIN_ALGEBRA, "\\theta", "\xCE\xB8"));

	/*
	 * unknown names are left alone and the word rules do not fire inside them
	 */
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "\\foo ", "\\foo "));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "\\alpha", "\\alpha"));
}

static void test_superscripts (void)
{
	CHECK (types_as      (ML_DOMAIN_ALGEBRA, "x^2 ", "x\xC2\xB2 "));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "x^10 ", "x\xC2\xB9\xE2\x81\xB0 "));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "x^-1)", "x\xE2\x81\xBB\xC2\xB9)"));
	CHECK (types_as      (ML_DOMAIN_ALGEBRA, "(x^3)", "(x\xC2\xB3)"));
	CHECK (types_flushed (ML_DOMAIN_ALGEBRA, "x^23", "x\xC2\xB2\xC2\xB3"));

	/* still being typed, a decimal point, no digits */
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "x^2", "x^2"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "x^2.5", "x^2.5"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "x^n ", "x^n "));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "x^- ", "x^- "));

	/* ^ is an operator in the boolean and PGA domains */
	CHECK (types_as (ML_DOMAIN_BOOLEAN, "a^2 ", "a^2 "));
	CHECK (types_as (ML_DOMAIN_PGA2, "e1^2 ", "e1^2 "));
	CHECK (types_as (ML_DOMAIN_PGA3, "e1^2 ", "e1^2 "));
	CHECK (types_as (ML_DOMAIN_LAMBDA, "A^2 ", "A\xC2\xB2 "));
}

static void test_sqrt (void)
{
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "sqrt(x)", "\xE2\x88\x9A" "x"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "sqrt(2)", "\xE2\x88\x9A" "2"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "sqrt(x+1)", "\xE2\x88\x9A(x+1)"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "1+sqrt(x)*2", "1+\xE2\x88\x9A" "x*2"));
	CHECK (
		types_as (
			ML_DOMAIN_ALGEBRA,
			"sqrt(sqrt(x))",
			"\xE2\x88\x9A(\xE2\x88\x9A" "x)"
		)
	);
	CHECK (
		types_as (ML_DOMAIN_ALGEBRA, "sqrt(x^2)", "\xE2\x88\x9A(x\xC2\xB2)")
	);
	CHECK (
		types_as (ML_DOMAIN_ALGEBRA, "sqrt(\xCF\x80)", "\xE2\x88\x9A\xCF\x80")
	);

	/* not a sqrt call, or nothing to take the root of */
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "mysqrt(x)", "mysqrt(x)"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "sqrt()", "sqrt()"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "sqrt(x", "sqrt(x"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "f(x)", "f(x)"));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "(x)", "(x)"));
}

static void test_domains (void)
{
	/* lambda: "lambda x." becomes the lambda sign, the space is swallowed */
	CHECK (types_as (ML_DOMAIN_LAMBDA, "lambda x.x", "\xCE\xBB" "x.x"));
	CHECK (types_as (ML_DOMAIN_LAMBDA, "lambda", "lambda"));
	CHECK (types_as (ML_DOMAIN_LAMBDA, "lambda(", "lambda("));
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "lambda x", "lambda x"));
	CHECK (types_as (ML_DOMAIN_LAMBDA, "\\lambda x", "\xCE\xBB x"));

	/* boolean */
	CHECK (
		types_as (
			ML_DOMAIN_BOOLEAN,
			"a&&b||~c",
			"a\xE2\x88\xA7" "b\xE2\x88\xA8\xC2\xAC" "c"
		)
	);
	CHECK (types_as (ML_DOMAIN_ALGEBRA, "a&&b||~c", "a&&b||~c"));
	CHECK (types_as (ML_DOMAIN_BOOLEAN, "a->b", "a\xE2\x86\x92" "b"));
}

static void test_undo (void)
{
	MlRewriter rw;
	MlLineEdit e;

	ml_rewriter_init (&rw, ML_DOMAIN_ALGEBRA);
	ml_lineedit_init (&e);

	/* a word rewrite: the character that triggered it stays */
	CHECK (type                  (&rw, &e, "x^2 ") == ML_OK);
	CHECK (ml_slice_equal        (ml_str_view (e.text), lit ("x\xC2\xB2 ")));
	CHECK (ml_rewriter_backspace (&rw, &e) == ML_OK);
	CHECK (
		ml_slice_equal (ml_str_view (e.text), lit ("x^2 ")) && e.cursor == 4
	);
	CHECK (ml_rewriter_backspace (&rw, &e) == ML_OK);
	CHECK (ml_slice_equal        (ml_str_view (e.text), lit ("x^2")));

	/* a literal rewrite */
	ml_lineedit_clear (&e);
	CHECK (type                  (&rw, &e, "a<=") == ML_OK);
	CHECK (ml_slice_equal        (ml_str_view (e.text), lit ("a\xE2\x89\xA4")));
	CHECK (ml_rewriter_backspace (&rw, &e) == ML_OK);
	CHECK (ml_slice_equal (ml_str_view (e.text), lit ("a<=")) && e.cursor == 3);

	/* sqrt */
	ml_lineedit_clear (&e);
	CHECK (type                  (&rw, &e, "sqrt(x+1)") == ML_OK);
	CHECK (ml_rewriter_backspace (&rw, &e) == ML_OK);
	CHECK (
		ml_slice_equal (
			ml_str_view (e.text),
			lit ("sqrt(x+1)")
		) && e.cursor == 9
	);

	/* a swallowed trigger comes back with the original */
	rw.domain = ML_DOMAIN_LAMBDA;
	ml_lineedit_clear (&e);
	CHECK (type                  (&rw, &e, "lambda ") == ML_OK);
	CHECK (ml_slice_equal        (ml_str_view (e.text), lit ("\xCE\xBB")));
	CHECK (ml_rewriter_backspace (&rw, &e) == ML_OK);
	CHECK (
		ml_slice_equal (ml_str_view (e.text), lit ("lambda ")) && e.cursor == 7
	);
	rw.domain = ML_DOMAIN_ALGEBRA;

	/* one step only: text typed afterwards is deleted normally */
	ml_lineedit_clear (&e);
	CHECK (type                  (&rw, &e, "a<=b") == ML_OK);
	CHECK (ml_rewriter_backspace (&rw, &e) == ML_OK);
	CHECK (ml_slice_equal        (ml_str_view (e.text), lit ("a\xE2\x89\xA4")));
	CHECK (ml_rewriter_backspace (&rw, &e) == ML_OK);
	CHECK (ml_slice_equal        (ml_str_view (e.text), lit ("a")));

	/* after an undo, backspace is normal again */
	ml_lineedit_clear (&e);
	CHECK (type                  (&rw, &e, "a->") == ML_OK);
	CHECK (ml_rewriter_backspace (&rw, &e) == ML_OK);
	CHECK (ml_slice_equal        (ml_str_view (e.text), lit ("a->")));
	CHECK (ml_rewriter_backspace (&rw, &e) == ML_OK);
	CHECK (ml_slice_equal        (ml_str_view (e.text), lit ("a-")));

	/* plain backspace without any rewrite */
	CHECK (ml_rewriter_backspace (&rw, &e) == ML_OK);
	CHECK (ml_slice_equal        (ml_str_view (e.text), lit ("a")));

	/* a flush can be undone too */
	ml_lineedit_clear (&e);
	CHECK (type                  (&rw, &e, "x^2") == ML_OK);
	CHECK (ml_rewriter_flush     (&rw, &e) == ML_OK);
	CHECK (ml_slice_equal        (ml_str_view (e.text), lit ("x\xC2\xB2")));
	CHECK (ml_rewriter_backspace (&rw, &e) == ML_OK);
	CHECK (ml_slice_equal        (ml_str_view (e.text), lit ("x^2")));

	ml_lineedit_free (&e);
	ml_rewriter_free (&rw);
}

static MlKey ch (uint32_t cp)
{
	MlKey k = { .code = ML_KEY_CHAR, .cp = cp, .mods = 0 };

	return k;
}

static MlKey code (MlKeyCode c)
{
	MlKey k = { .code = c, .cp = 0, .mods = 0 };

	return k;
}

static void test_keys (void)
{
	MlRewriter rw;
	MlLineEdit e;

	ml_rewriter_init (&rw, ML_DOMAIN_ALGEBRA);
	ml_lineedit_init (&e);

	CHECK (ml_rewriter_handle_key (&rw, &e, ch ('<')).handled);
	CHECK (ml_rewriter_handle_key (&rw, &e, ch ('=')).handled);
	CHECK (ml_slice_equal         (ml_str_view (e.text), lit ("\xE2\x89\xA4")));

	/* moving the cursor makes the rewrite final */
	CHECK (ml_rewriter_handle_key (&rw, &e, code (ML_KEY_LEFT)).handled);
	CHECK (ml_rewriter_handle_key (&rw, &e, code (ML_KEY_RIGHT)).handled);
	CHECK (ml_rewriter_handle_key (&rw, &e, code (ML_KEY_BACKSPACE)).handled);
	CHECK (ml_str_view            (e.text).len == 0);

	/* keys that are not editing keys pass through unhandled */
	CHECK (!ml_rewriter_handle_key (&rw, &e, code (ML_KEY_ENTER)).handled);
	CHECK (!ml_rewriter_handle_key (&rw, &e, code (ML_KEY_ESCAPE)).handled);

	/* ctrl/alt characters are not typed text */
	{
		MlKey k = { .code = ML_KEY_CHAR, .cp = 'a', .mods = ML_MOD_CTRL };

		CHECK (ml_rewriter_handle_key (&rw, &e, k).handled);
		CHECK (ml_str_view            (e.text).len == 0);
	}

	/* a domain change disarms */
	CHECK (ml_rewriter_handle_key (&rw, &e, ch ('<')).handled);
	CHECK (ml_rewriter_handle_key (&rw, &e, ch ('=')).handled);
	ml_rewriter_set_domain (&rw, ML_DOMAIN_ALGEBRA);
	CHECK (ml_rewriter_handle_key (&rw, &e, code (ML_KEY_BACKSPACE)).handled);
	CHECK (ml_str_view            (e.text).len == 0);

	ml_lineedit_free (&e);
	ml_rewriter_free (&rw);
}

static void test_middle_of_line (void)
{
	MlRewriter rw;
	MlLineEdit e;

	ml_rewriter_init (&rw, ML_DOMAIN_ALGEBRA);
	ml_lineedit_init (&e);

	CHECK (ml_lineedit_set (&e, lit ("ab")) == ML_OK);
	ml_lineedit_left (&e);
	CHECK (type           (&rw, &e, "<=") == ML_OK);
	CHECK (ml_slice_equal (ml_str_view (e.text), lit ("a\xE2\x89\xA4" "b")));
	CHECK (e.cursor == 4);

	CHECK (ml_lineedit_set (&e, lit ("x^2y")) == ML_OK);
	ml_lineedit_left (&e);
	CHECK (type           (&rw, &e, "+") == ML_OK);
	CHECK (ml_slice_equal (ml_str_view (e.text), lit ("x\xC2\xB2+y")));

	ml_lineedit_free (&e);
	ml_rewriter_free (&rw);
}

void run_rewrite_tests (void)
{
	test_acceptance ();
	test_literals ();
	test_words ();
	test_escapes ();
	test_superscripts ();
	test_sqrt ();
	test_domains ();
	test_undo ();
	test_keys ();
	test_middle_of_line ();
}
