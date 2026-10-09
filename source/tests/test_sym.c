#include "test.h"

#include <matlog/math/env.h>
#include <matlog/sym/expr.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

typedef enum Op
{
	OP_SIMPLIFY = 0,
	OP_EXPAND,
	OP_DIFF,
	OP_INTEGRATE
} Op;

/*
 * Parses the text as an expression, applies op (diff and integrate by the
 * symbol x) and formats the result. "" when anything fails.
 */
static int32_t gives_op (Op op, const char *source, const char *expected)
{
	MlSlice       text = lit (source);
	MlParseResult p    = ml_parse (text);
	MlArena       arena;
	MlSymCtx      ctx;
	MlEnv         env;
	MlStr         out;
	int32_t       same = 0;

	ml_arena_init (&arena, 0);
	ml_symctx_init (&ctx, &arena);
	ml_env_init (&env);
	ml_str_init (&out);

	if (p.error.code == ML_PARSE_OK)
	{
		const MlSym *s = ml_sym_from_ast (
			&ctx,
			&env,
			0,
			text,
			&p.ast,
			p.ast.root,
			lit ("x")
		);
		const MlSym *x = ml_sym_var (&ctx, lit ("x"));
		const MlSym *r = s;

		if (op == OP_SIMPLIFY)
		{
			r = ml_sym_simplify (&ctx, s);
		}

		else if (op == OP_EXPAND)
		{
			r = ml_sym_expand (&ctx, s);
		}

		else if (op == OP_DIFF)
		{
			r = ml_sym_diff (&ctx, s, x);
		}

		else
		{
			MlSymIntegral i = ml_sym_integrate (&ctx, s, x);

			r = ml_choose (i.found, i.value, NULL);
		}

		if (r != NULL && ml_symctx_ok (&ctx))
		{
			same = (
				   ml_sym_format (&out, r) == ML_OK
				&& ml_slice_equal (ml_str_view (out), lit (expected))
			);
		}

		else
		{
			same = ml_slice_equal (lit (""), lit (expected));
		}
	}

	if (!same)
	{
		printf (
			"  \"%s\" gives \"%s\" (status %d), expected \"%s\"\n",
			source,
			ml_str_cstr (out),
			(int32_t)ctx.status,
			expected
		);
	}

	ml_ast_free (&p.ast);
	ml_str_free (&out);
	ml_env_free (&env);
	ml_arena_free (&arena);
	return same;
}

static int32_t simplifies (const char *source, const char *expected)
{
	return gives_op (OP_SIMPLIFY, source, expected);
}

static int32_t expands (const char *source, const char *expected)
{
	return gives_op (OP_EXPAND, source, expected);
}

static void test_numbers (void)
{
	CHECK (simplifies ("1", "1"));
	CHECK (simplifies ("1 + 2", "3"));
	CHECK (simplifies ("2 * 3 + 4", "10"));
	CHECK (simplifies ("1/3 + 1/6", "1/2"));
	CHECK (simplifies ("2^10", "1024"));
	CHECK (simplifies ("2^-2", "1/4"));
	CHECK (simplifies ("sqrt(16)", "4"));
	CHECK (simplifies ("sqrt(2) * sqrt(2)", "2"));
	CHECK (simplifies ("sqrt(2)", "\xE2\x88\x9A" "2"));
	CHECK (simplifies ("3!", "6"));
	CHECK (simplifies ("-5 + 2", "-3"));
	CHECK (simplifies ("0.25 * 4", "1"));
}

static void test_sums_products (void)
{
	CHECK (simplifies ("x", "x"));
	CHECK (simplifies ("x + x", "2x"));
	CHECK (simplifies ("x + x + x", "3x"));
	CHECK (simplifies ("x - x", "0"));
	CHECK (simplifies ("x + 0", "x"));
	CHECK (simplifies ("0 + x", "x"));
	CHECK (simplifies ("x * 1", "x"));
	CHECK (simplifies ("x * 0", "0"));
	CHECK (simplifies ("2x + 3x", "5x"));
	CHECK (simplifies ("2x - 5x", "-3x"));
	CHECK (simplifies ("x/2 + x/2", "x"));
	CHECK (simplifies ("x * y", "x\xC2\xB7y"));
	CHECK (simplifies ("y * x", "x\xC2\xB7y"));
	CHECK (simplifies ("2 * x * 3", "6x"));
	CHECK (simplifies ("x + 1 + x + 2", "2x + 3"));
	CHECK (simplifies ("1 + x", "x + 1"));
	CHECK (simplifies ("x^2 + x + 1", "x\xC2\xB2 + x + 1"));
	CHECK (simplifies ("1 + x + x^2", "x\xC2\xB2 + x + 1"));
	CHECK (simplifies ("x - 1", "x - 1"));
	CHECK (simplifies ("-x + 3", "-x + 3"));
	CHECK (simplifies ("3 - x", "-x + 3"));
	CHECK (simplifies ("x*y + y*x", "2x\xC2\xB7y"));
	CHECK (simplifies ("(x + 1) + (x + 2)", "2x + 3"));
	CHECK (simplifies ("2 (x + 1)", "2(x + 1)"));
}

static void test_powers (void)
{
	CHECK (simplifies ("x * x", "x\xC2\xB2"));
	CHECK (simplifies ("x^2 * x^3", "x\xE2\x81\xB5"));
	CHECK (simplifies ("x * x^2", "x\xC2\xB3"));
	CHECK (simplifies ("x / x", "1"));
	CHECK (simplifies ("x^2 / x", "x"));
	CHECK (simplifies ("x / x^2", "1/x"));
	CHECK (simplifies ("x^0", "1"));
	CHECK (simplifies ("x^1", "x"));
	CHECK (simplifies ("(x^2)^3", "x\xE2\x81\xB6"));
	CHECK (simplifies ("(x*y)^2", "x\xC2\xB2\xC2\xB7y\xC2\xB2"));
	CHECK (simplifies ("1/x", "1/x"));
	CHECK (simplifies ("2/x", "2/x"));
	CHECK (simplifies ("x/2", "x/2"));
	CHECK (simplifies ("3x/2", "3x/2"));
	CHECK (simplifies ("x^-2", "1/x\xC2\xB2"));
	CHECK (simplifies ("sqrt(x)", "\xE2\x88\x9A" "x"));
	CHECK (simplifies ("sqrt(x) * sqrt(x)", "x"));
	CHECK (simplifies ("x^(1/3)", "x^(1/3)"));
	CHECK (simplifies ("x^y", "x^y"));
	CHECK (simplifies ("(x+1)^2", "(x + 1)\xC2\xB2"));
	CHECK (simplifies ("(x+1) * (x+1)", "(x + 1)\xC2\xB2"));
	CHECK (simplifies ("x / (x + 1)", "x/(x + 1)"));
	CHECK (simplifies ("x*y / (x*z)", "y/z"));
}

static void test_functions (void)
{
	CHECK (simplifies ("sin(x)", "sin(x)"));
	CHECK (simplifies ("sin(0)", "0"));
	CHECK (simplifies ("cos(0)", "1"));
	CHECK (simplifies ("exp(0)", "1"));
	CHECK (simplifies ("ln(1)", "0"));
	CHECK (simplifies ("sin(-x)", "-sin(x)"));
	CHECK (simplifies ("cos(-x)", "cos(x)"));
	CHECK (simplifies ("abs(-3)", "3"));
	CHECK (simplifies ("exp(ln(x))", "x"));
	CHECK (simplifies ("ln(exp(x))", "x"));
	CHECK (simplifies ("sin(x) + sin(x)", "2sin(x)"));
	CHECK (simplifies ("sin(x)^2 + cos(x)^2", "cos(x)\xC2\xB2 + sin(x)\xC2\xB2"));
	CHECK (simplifies ("2 sin(x) cos(x)", "2cos(x)\xC2\xB7sin(x)"));
	CHECK (simplifies ("pi + pi", "2\xCF\x80"));
	CHECK (simplifies ("2*pi*r", "2\xCF\x80\xC2\xB7r"));
}

static void test_expand (void)
{
	CHECK (expands ("(x+1)^2", "x\xC2\xB2 + 2x + 1"));
	CHECK (expands ("(x+1)(x-1)", "x\xC2\xB2 - 1"));
	CHECK (expands ("(x+1)^3", "x\xC2\xB3 + 3x\xC2\xB2 + 3x + 1"));
	CHECK (expands ("(x+y)^2", "x\xC2\xB2 + 2x\xC2\xB7y + y\xC2\xB2"));
	CHECK (expands ("2(x + 3)", "2x + 6"));
	CHECK (expands ("x(x + 1)", "x\xC2\xB2 + x"));
	CHECK (expands ("(a+b)(c+d)", "a\xC2\xB7" "c + a\xC2\xB7" "d + b\xC2\xB7" "c + b\xC2\xB7" "d"));
	CHECK (expands ("(x-1)^2 - (x+1)^2", "-4x"));
	CHECK (expands ("x + 1", "x + 1"));
	CHECK (expands ("(x+1)^-1", "1/(x + 1)"));
	CHECK (expands ("((x+1)^2)^2", "x\xE2\x81\xB4 + 4x\xC2\xB3 + 6x\xC2\xB2 + 4x + 1"));
}

void run_sym_tests (void);

void run_sym_tests (void)
{
	test_numbers ();
	test_sums_products ();
	test_powers ();
	test_functions ();
	test_expand ();
}
