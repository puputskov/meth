#include "test.h"

#include <matlog/math/eval.h>

/* Integrals beyond the table: partial fractions, substitution, parts. */

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* The text of the value of a line, "" when it is an error. */
static int32_t shows (const char *text, const char *expected)
{
	MlEnv         env;
	MlArena       arena;
	MlSlice       source = lit (text);
	MlParseResult p      = ml_parse (source);
	MlValue       v;
	MlStr         out;
	int32_t       same   = 0;

	ml_env_init (&env);
	ml_arena_init (&arena, 0);
	ml_str_init (&out);
	v = ml_eval (&arena, &env, ml_env_snapshot (&env), source, &p.ast);

	if (ml_value_is_expr (v) || ml_value_is_number (v))
	{
		same = (
			   ml_value_format (&out, &arena, v) == ML_OK
			&& ml_slice_equal (ml_str_view (out), lit (expected))
		);
	}

	else
	{
		same = expected [0] == '\0' && v.kind == ML_VALUE_ERROR;
	}

	if (!same)
	{
		printf (
			"  \"%s\": got \"%s\" (kind %d error %d), expected \"%s\"\n",
			text,
			ml_str_cstr (out),
			(int32_t)v.kind,
			(int32_t)v.error,
			expected
		);
	}

	ml_str_free (&out);
	ml_ast_free (&p.ast);
	ml_arena_free (&arena);
	ml_env_free (&env);
	return same;
}

static void test_partial_fractions (void)
{
	CHECK (shows (
		"integrate(1/(x^2 - 1), x)",
		"ln(abs(x - 1))/2 - ln(abs(x + 1))/2"
	));
	CHECK (shows (
		"integrate(1/(x^2 + 2), x)",
		"atan(x/\xE2\x88\x9A"
		"2)/\xE2\x88\x9A"
		"2"
	));
	CHECK (shows (
		"integrate((x^3 + 1)/(x^2 - 1), x)",
		"x\xC2\xB2/2 + ln(abs(x - 1))"
	));
	CHECK (shows (
		"integrate(1/(x*(x + 1)^2), x)",
		"ln(abs(x)) - ln(abs(x + 1)) + 1/(x + 1)"
	));
	CHECK (shows ("integrate(1/(x^2 + 2x + 5), x)", "atan(x/2 + 1/2)/2"));
}

static void test_substitution (void)
{
	CHECK (shows ("integrate(x*exp(x^2), x)", "exp(x\xC2\xB2)/2"));
	CHECK (shows ("integrate(exp(x)/(1 + exp(x)), x)", "ln(abs(exp(x) + 1))"));
	CHECK (shows ("integrate(cos(x)*exp(sin(x)), x)", "exp(sin(x))"));
	CHECK (shows ("integrate(2x/(x^2 + 1)^2, x)", "-1/(x\xC2\xB2 + 1)"));
	CHECK (shows ("integrate(sin(x)*cos(x)^2, x)", "-cos(x)\xC2\xB3/3"));
	CHECK (shows ("integrate(x*exp(x^2), x, 0, 1)", "exp(1)/2 - 1/2"));
}

static void test_parts (void)
{
	CHECK (shows ("integrate(x*sin(x), x)", "-x\xC2\xB7" "cos(x) + sin(x)"));
	CHECK (shows (
		"integrate(x^2*exp(x), x)",
		"x\xC2\xB2\xC2\xB7"
		"exp(x) - 2x\xC2\xB7"
		"exp(x) + 2exp(x)"
	));
	CHECK (shows (
		"integrate(x*ln(x), x)",
		"x\xC2\xB2\xC2\xB7"
		"ln(x)/2 - x\xC2\xB2/4"
	));
	CHECK (shows (
		"integrate(atan(x), x)",
		"x\xC2\xB7"
		"atan(x) - ln(x\xC2\xB2 + 1)/2"
	));
	CHECK (shows (
		"integrate(exp(x)*sin(x), x)",
		"-cos(x)\xC2\xB7"
		"exp(x)/2 + exp(x)\xC2\xB7"
		"sin(x)/2"
	));
	CHECK (shows (
		"integrate(exp(2x)*cos(x), x)",
		"2cos(x)\xC2\xB7"
		"exp(2x)/5 + exp(2x)\xC2\xB7"
		"sin(x)/5"
	));
	CHECK (shows (
		"integrate(x*ln(x)^2, x)",
		"x\xC2\xB2\xC2\xB7"
		"ln(x)\xC2\xB2/2 - x\xC2\xB2\xC2\xB7"
		"ln(x)/2 + x\xC2\xB2/4"
	));
	CHECK (shows (
		"integrate(x^3*exp(x^2), x)",
		"x\xC2\xB2\xC2\xB7"
		"exp(x\xC2\xB2)/2 - exp(x\xC2\xB2)/2"
	));
	CHECK (shows (
		"integrate(exp(sqrt(x)), x)",
		"2\xE2\x88\x9Ax\xC2\xB7"
		"exp(\xE2\x88\x9Ax) - 2exp(\xE2\x88\x9Ax)"
	));
	CHECK (shows ("integrate(x*exp(x), x, 0, 1)", "1"));
	CHECK (shows ("integrate(1/(x^2 + 1), x, -inf, inf)", "\xCF\x80"));
}

static void test_half_angle (void)
{
	CHECK (shows ("integrate(sin(x)^2, x)", "x/2 - sin(2x)/4"));
	CHECK (shows ("integrate(cos(2x + 1)^2, x)", "x/2 + sin(4x + 2)/8"));
}

static void test_not_found (void)
{
	CHECK (shows ("integrate(sin(x)/x, x)", ""));
	CHECK (shows ("integrate(exp(x^2), x)", ""));
	CHECK (shows ("integrate(1/(x^3 + x + 1), x)", ""));
}

static void test_solve (void)
{
	CHECK (shows ("solve(x^2 - 4, x)", "{-2, 2}"));
	CHECK (shows (
		"solve(x^2 - 2, x)",
		"{-\xE2\x88\x9A"
		"2, \xE2\x88\x9A"
		"2}"
	));
	CHECK (shows ("solve(x^3 - 6x^2 + 11x - 6, x)", "{1, 2, 3}"));
	CHECK (shows ("solve(2x^2 - 3x - 2, x)", "{-1/2, 2}"));
	CHECK (shows ("solve(x^2 + 1, x)", "{}"));
	CHECK (shows ("solve(2x + 3, x)", "{-3/2}"));
	CHECK (shows ("solve(5, x)", "{}"));
	CHECK (shows ("solve(1/x + 1, x)", "{-1}"));
	CHECK (shows ("solve(x/(x - 1), x)", "{0}"));
	CHECK (shows ("solve(1/x - 2, x)", "{1/2}"));
	CHECK (shows ("solve(x^4 - 5x^2 + 4, x)", "{-2, -1, 1, 2}"));
	CHECK (shows ("solve(exp(x) - 2, x)", "{ln(2)}"));
	CHECK (shows ("solve(exp(2x) - 3exp(x) + 2, x)", "{0, ln(2)}"));
	CHECK (shows ("solve(sqrt(x) - 3, x)", "{9}"));
	CHECK (shows ("solve(ln(x) - 1, x)", "{exp(1)}"));
	CHECK (shows ("solve(x^3 + x + 1, x)", ""));
	CHECK (shows ("solve(x*exp(x), x)", ""));
	CHECK (shows ("solve(0, x)", ""));
}

void run_integrate_tests (void);

void run_integrate_tests (void)
{
	test_solve ();
	test_partial_fractions ();
	test_substitution ();
	test_parts ();
	test_half_angle ();
	test_not_found ();
}
