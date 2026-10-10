#include "test.h"

#include <matlog/logic/lambda.h>
#include <matlog/session/session.h>

#include <string.h>

#define LAM "\xCE\xBB"

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* Runs a line; the definition it makes is kept in defs. */
static int32_t gives (MlLogicDefVec *defs, const char *line, const char *want)
{
	MlLogicOut o;
	int32_t    same;

	ml_logic_out_init (&o);
	same = (
		   ml_lambda_run (defs, defs->len, lit (line), &o) == ML_OK
		&& o.error == ML_LOGIC_OK
		&& ml_slice_equal (ml_str_view (o.text), lit (want))
	);

	if (o.def_name.len > 0)
	{
		same = same && ml_logic_defs_add (
			defs,
			ML_DOMAIN_LAMBDA,
			ml_str_view (o.def_name),
			ml_str_view (o.def_text)
		) == ML_OK;
	}

	if (!same)
	{
		printf (
			"  \"%s\" gives \"%s\"%s, expected \"%s\"\n",
			line,
			ml_str_cstr (o.text),
			ml_choose (o.error != ML_LOGIC_OK, " (error)", ""),
			want
		);
	}

	ml_logic_out_free (&o);
	return same;
}

static int32_t fails (MlLogicDefVec *defs, const char *line, const char *code)
{
	MlLogicOut o;
	int32_t    same;

	ml_logic_out_init (&o);
	same = (
		   ml_lambda_run (defs, defs->len, lit (line), &o) == ML_OK
		&& o.error != ML_LOGIC_OK
		&& strcmp (ml_logic_error_code (o.error), code) == 0
	);

	if (o.def_name.len > 0)
	{
		ml_logic_defs_add (
			defs,
			ML_DOMAIN_LAMBDA,
			ml_str_view (o.def_name),
			ml_str_view (o.def_text)
		);
	}

	if (!same)
	{
		printf (
			"  \"%s\" gives \"%s\", expected error \"%s\"\n",
			line,
			ml_str_cstr (o.text),
			code
		);
	}

	ml_logic_out_free (&o);
	return same;
}

static void test_terms (void)
{
	MlLogicDefVec d;

	MlLogicDefVec_init (&d);

	/* the syntax is written back with the fewest parentheses */
	CHECK (gives (&d, "x", "x"));
	CHECK (gives (&d, "a b c", "a b c"));
	CHECK (gives (&d, "a (b c)", "a (b c)"));
	CHECK (gives (&d, "(a b) c", "a b c"));
	CHECK (gives (&d, "\\x. x", LAM "x. x"));
	CHECK (gives (&d, LAM "x y z. x z (y z)", LAM "x y z. x z (y z)"));
	CHECK (gives (&d, LAM "x. " LAM "y. x", LAM "x y. x"));
	CHECK (gives (&d, "a (" LAM "x. x)", "a (" LAM "x. x)"));
	CHECK (gives (&d, "(" LAM "x. x) a", "a"));
	CHECK (gives (&d, "2", LAM "f x. f (f x)"));
	CHECK (gives (&d, "0", LAM "f x. x"));

	/* reduction */
	CHECK (gives (&d, "(" LAM "x y. x) a b", "a"));
	CHECK (gives (&d, "(" LAM "x y. y) a b", "b"));
	CHECK (gives (&d, "(" LAM "x. x x) (" LAM "y. y)", LAM "y. y"));

	/* normal order finds the normal form that applicative order loses */
	CHECK (
		gives (
			&d,
			"(" LAM "x y. y) ((" LAM "z. z z z) (" LAM "z. z z z)) q",
			"q"
		)
	);

	/* a binder is renamed when it would capture */
	CHECK (gives (&d, "(" LAM "x y. x) y", LAM "y'. y"));
	CHECK (gives (&d, LAM "y. y z", LAM "y. y z"));
	CHECK (gives (&d, "(" LAM "x. " LAM "y. x y) y", LAM "y'. y y'"));

	ml_logic_defs_free (&d);
}

static void test_defs (void)
{
	MlLogicDefVec d;

	MlLogicDefVec_init (&d);
	CHECK (gives (&d, "id = " LAM "x. x", LAM "x. x"));
	CHECK (gives (&d, "id id", LAM "x. x"));
	CHECK (gives (&d, "id a", "a"));

	/* a lambda that binds the name hides the definition */
	CHECK (gives (&d, LAM "id. id", LAM "id. id"));
	CHECK (gives (&d, "(" LAM "id. id) q", "q"));

	/* Church numerals */
	CHECK (
		gives (&d, "succ = " LAM "n f x. f (n f x)", LAM "n f x. f (n f x)")
	);
	CHECK (gives (&d, "succ 1", LAM "f x. f (f x)"));
	CHECK (
		gives (
			&d,
			"plus = " LAM "m n f x. m f (n f x)",
			LAM "m n f x. m f (n f x)"
		)
	);
	CHECK (gives (&d, "plus 2 3", LAM "f x. f (f (f (f (f x))))"));
	CHECK (gives (&d, "mult = " LAM "m n f. m (n f)", LAM "m n f. m (n f)"));
	CHECK (
		gives (&d, "mult 3 3", LAM "f x. f (f (f (f (f (f (f (f (f x))))))))")
	);

	/* the free names of a definition stay free */
	CHECK (gives (&d, "k = " LAM "x. y", LAM "x. y"));
	CHECK (gives (&d, LAM "y. k", LAM "y' x. y"));

	/* a definition is kept as a term, so a loop can be named */
	CHECK (
		fails (
			&d,
			"omega = (" LAM "x. x x) (" LAM "x. x x)",
			"lambda_no_normal_form"
		)
	);
	CHECK (gives (&d, "(" LAM "x y. y) omega z", "z"));

	/* a line sees only the definitions before it */
	{
		MlLogicOut o;

		ml_logic_out_init (&o);
		CHECK (ml_lambda_run (&d, 0, lit ("id"), &o) == ML_OK);
		CHECK (ml_slice_equal (ml_str_view (o.text), lit ("id")));
		ml_logic_out_free (&o);
	}

	ml_logic_defs_free (&d);
}

static void test_trace (void)
{
	MlLogicDefVec d;
	MlLogicOut    o;

	MlLogicDefVec_init (&d);
	ml_logic_out_init (&o);
	CHECK (
		ml_lambda_run (
			&d,
			0,
			lit ("trace (" LAM "x. x x) (" LAM "y. y) z"),
			&o
		) == ML_OK
	);
	CHECK (ml_slice_equal (ml_str_view (o.text), lit ("z")));
	CHECK (
		ml_slice_equal (
			ml_str_view (o.block),
			lit (
				"(" LAM "x. x x) (" LAM "y. y) z\n"
				"\xE2\x86\x92 (" LAM "y. y) (" LAM "y. y) z\n"
				"\xE2\x86\x92 (" LAM "y. y) z\n"
				"\xE2\x86\x92 z"
			)
		)
	);
	ml_logic_out_free (&o);

	/* trace is a name when no term follows */
	CHECK (gives (&d, "trace", "trace"));
	ml_logic_defs_free (&d);
}

static void test_errors (void)
{
	MlLogicDefVec d;

	MlLogicDefVec_init (&d);
	CHECK (fails (&d, LAM ". x", "lambda_expected_parameter"));
	CHECK (fails (&d, LAM "x x", "lambda_expected_parameter"));
	CHECK (fails (&d, LAM "x.", "lambda_expected_term"));
	CHECK (fails (&d, "()", "lambda_expected_term"));
	CHECK (fails (&d, "(x", "lambda_unbalanced_parenthesis"));
	CHECK (fails (&d, "x)", "lambda_unbalanced_parenthesis"));
	CHECK (fails (&d, "x #", "lambda_unexpected_character"));
	CHECK (fails (&d, "201", "lambda_number_too_big"));
	CHECK (fails (&d, "3 = x", "lambda_bad_definition"));
	CHECK (
		fails (&d, "(" LAM "x. x x) (" LAM "x. x x)", "lambda_no_normal_form")
	);
	CHECK (fails (&d, "(" LAM "x. x x x) (" LAM "x. x x x)", "lambda_too_big"));
	ml_logic_defs_free (&d);
}

void run_lambda_tests (void);

void run_lambda_tests (void)
{
	test_terms ();
	test_defs ();
	test_trace ();
	test_errors ();
}
