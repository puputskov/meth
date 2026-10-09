#include "test.h"

#include <matlog/math/eval.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/*
 * One session: lines are evaluated in order, each in the environment as it is
 * when the line is typed, and what they define is kept.
 */
typedef struct Session
{
	MlEnv   env;
	MlArena arena;
} Session;

static void session_init (Session *s)
{
	ml_env_init (&s->env);
	ml_arena_init (&s->arena, 0);
}

static void session_free (Session *s)
{
	ml_arena_free (&s->arena);
	ml_env_free (&s->env);
}

/* Evaluates one line and commits its definitions. */
static MlValue run (Session *s, const char *text)
{
	MlSlice       source = lit (text);
	MlParseResult p      = ml_parse (source);
	MlValue       v      = ml_value_error (ML_EVAL_SYNTAX, 0, 0);

	if (p.error.code == ML_PARSE_OK)
	{
		v = ml_eval (
			&s->arena,
			&s->env,
			ml_env_snapshot (&s->env),
			source,
			&p.ast
		);
		ml_eval_commit (&s->env, source, &p.ast, v);
		ml_ast_free (&p.ast);
	}

	return v;
}

static int32_t shows (Session *s, const char *text, const char *expected)
{
	MlValue v    = run (s, text);
	MlStr   out;
	int32_t same = 0;

	ml_str_init (&out);

	if (ml_value_is_number (v))
	{
		same = (
			   ml_value_format (&out, &s->arena, v) == ML_OK
			&& ml_slice_equal (ml_str_view (out), lit (expected))
		);
	}

	if (!same)
	{
		printf (
			"  \"%s\": kind %d error %d (%s), expected \"%s\", got \"%s\"\n",
			text,
			(int32_t)v.kind,
			(int32_t)v.error,
			ml_error_text (v.error),
			expected,
			ml_str_cstr (out)
		);
	}

	ml_str_free (&out);
	return same;
}

static int32_t fails (Session *s, const char *text, MlErrorKind kind)
{
	MlValue v    = run (s, text);
	int32_t same = v.kind == ML_VALUE_ERROR && v.error == kind;

	if (!same)
	{
		printf (
			"  \"%s\": kind %d error %d, expected error %d\n",
			text,
			(int32_t)v.kind,
			(int32_t)v.error,
			(int32_t)kind
		);
	}

	return same;
}

static int32_t is_approx (Session *s, const char *text, double expected)
{
	MlValue v = run (s, text);

	return (
		   v.kind == ML_VALUE_APPROX
		&& v.approx > expected - 1e-9
		&& v.approx < expected + 1e-9
	);
}

static void test_arithmetic (void)
{
	Session s;

	session_init (&s);

	CHECK (shows (&s, "1 + 2 * 3", "7"));
	CHECK (shows (&s, "(1 + 2) * 3", "9"));
	CHECK (shows (&s, "7 / 2", "7/2"));
	CHECK (shows (&s, "10 - 4 - 3", "3"));
	CHECK (shows (&s, "100 / 10 / 5", "2"));
	CHECK (shows (&s, "-2^2", "-4"));
	CHECK (shows (&s, "(-2)^2", "4"));
	CHECK (shows (&s, "2^3^2", "512"));
	CHECK (shows (&s, "2^-1", "1/2"));
	CHECK (shows (&s, "2^0", "1"));
	CHECK (shows (&s, "0.75", "3/4"));
	CHECK (shows (&s, "0.1 + 0.2", "3/10"));
	CHECK (shows (&s, "1/3 + 1/6", "1/2"));
	CHECK (shows (&s, "(2/3)^3", "8/27"));
	CHECK (shows (&s, "(2/3)^-2", "9/4"));
	CHECK (shows (&s, "- -3", "3"));
	CHECK (shows (&s, "2^200", "1606938044258990275541962092341162602522202"
		"993782792835301376"));
	CHECK (shows (&s, "10!", "3628800"));
	CHECK (shows (&s, "0!", "1"));
	CHECK (shows (&s, "3!^2", "36"));
	CHECK (shows (&s, "2^3!", "64"));
	CHECK (shows (&s, "2 * 3!", "12"));

	/* unicode spellings */
	CHECK (shows (&s, "6\xC3\x97" "7", "42"));
	CHECK (shows (&s, "6 \xC3\xB7 4", "3/2"));
	CHECK (shows (&s, "5 \xE2\x88\x92 8", "-3"));
	CHECK (shows (&s, "\xE2\x88\x9A" "4", "2"));
	CHECK (shows (&s, "3\xC2\xB2", "9"));
	CHECK (shows (&s, "2\xE2\x81\xBB\xC2\xB9", "1/2"));

	/* nothing to evaluate */
	{
		MlAst empty;

		MlNodeVec_init (&empty.nodes);
		empty.root = 0;
		CHECK (
			ml_eval (&s.arena, &s.env, 0, lit (""), &empty).kind
			== ML_VALUE_NONE
		);
		MlNodeVec_free (&empty.nodes);
	}

	session_free (&s);
}

static void test_approximation (void)
{
	Session s;

	session_init (&s);

	CHECK (is_approx (&s, "sqrt(2)", 1.41421356237));
	CHECK (is_approx (&s, "\xE2\x88\x9A" "2", 1.41421356237));
	CHECK (is_approx (&s, "pi", 3.14159265359));
	CHECK (is_approx (&s, "\xCF\x80", 3.14159265359));
	CHECK (is_approx (&s, "e", 2.71828182846));
	CHECK (is_approx (&s, "2pi", 6.28318530718));

	/* it spreads */
	CHECK (is_approx (&s, "1 + sqrt(2)", 2.41421356237));
	CHECK (is_approx (&s, "sqrt(2) * sqrt(2) + 1", 3.0));
	CHECK (is_approx (&s, "2^0.5", 1.41421356237));
	CHECK (is_approx (&s, "2^(1/3)", 1.25992104989));
	CHECK (is_approx (&s, "8^(1/3)", 2.0));

	/* exact stays exact */
	CHECK (shows (&s, "sqrt(4)", "2"));
	CHECK (shows (&s, "sqrt(1/4)", "1/2"));
	CHECK (shows (&s, "sqrt(0)", "0"));
	CHECK (shows (&s, "4^(1/2)", "2"));
	CHECK (shows (&s, "2^-1", "1/2"));

	CHECK (shows (&s, "sqrt(2)", "\xE2\x89\x88 1.41421356"));
	CHECK (shows (&s, "pi", "\xE2\x89\x88 3.14159265"));

	session_free (&s);
}

static void test_builtins (void)
{
	Session s;

	session_init (&s);

	CHECK (shows (&s, "abs(-3)", "3"));
	CHECK (shows (&s, "abs(3)", "3"));
	CHECK (shows (&s, "abs(-7/2)", "7/2"));
	CHECK (is_approx (&s, "abs(-sqrt(2))", 1.41421356237));

	CHECK (shows (&s, "sin(0)", "0"));
	CHECK (shows (&s, "cos(0)", "1"));
	CHECK (shows (&s, "tan(0)", "0"));
	CHECK (shows (&s, "asin(0)", "0"));
	CHECK (shows (&s, "atan(0)", "0"));
	CHECK (shows (&s, "exp(0)", "1"));
	CHECK (shows (&s, "ln(1)", "0"));
	CHECK (shows (&s, "log(1)", "0"));
	CHECK (is_approx (&s, "sin(1)", 0.841470984808));
	CHECK (is_approx (&s, "cos(1)", 0.540302305868));
	CHECK (is_approx (&s, "tan(1)", 1.55740772465));
	CHECK (is_approx (&s, "asin(1)", 1.57079632679));
	CHECK (is_approx (&s, "acos(0)", 1.57079632679));
	CHECK (is_approx (&s, "atan(1)", 0.785398163397));
	CHECK (is_approx (&s, "ln(e)", 1.0));
	CHECK (is_approx (&s, "ln(10)", 2.30258509299));
	CHECK (is_approx (&s, "log(1000)", 3.0));
	CHECK (is_approx (&s, "exp(1)", 2.71828182846));
	CHECK (is_approx (&s, "sin(pi/2)", 1.0));

	CHECK (shows (&s, "floor(7/2)", "3"));
	CHECK (shows (&s, "floor(-7/2)", "-4"));
	CHECK (shows (&s, "floor(5)", "5"));
	CHECK (shows (&s, "ceil(7/2)", "4"));
	CHECK (shows (&s, "ceil(-7/2)", "-3"));
	CHECK (shows (&s, "ceil(5)", "5"));
	CHECK (shows (&s, "round(7/2)", "4"));
	CHECK (shows (&s, "round(-7/2)", "-4"));
	CHECK (shows (&s, "round(10/3)", "3"));
	CHECK (shows (&s, "round(-10/3)", "-3"));
	CHECK (shows (&s, "round(1/3)", "0"));
	CHECK (shows (&s, "round(0.5)", "1"));
	CHECK (is_approx (&s, "floor(sqrt(2))", 1.0));
	CHECK (is_approx (&s, "ceil(sqrt(2))", 2.0));
	CHECK (is_approx (&s, "round(sqrt(2))", 1.0));

	CHECK (shows (&s, "min(3, 1, 2)", "1"));
	CHECK (shows (&s, "max(3, 1, 2)", "3"));
	CHECK (shows (&s, "min(1/2, 1/3)", "1/3"));
	CHECK (shows (&s, "max(-1, -2)", "-1"));
	CHECK (shows (&s, "max(1, 1)", "1"));

	CHECK (shows (&s, "gcd(12, 18)", "6"));
	CHECK (shows (&s, "gcd(-12, 18)", "6"));
	CHECK (shows (&s, "gcd(0, 5)", "5"));
	CHECK (shows (&s, "gcd(0, 0)", "0"));
	CHECK (shows (&s, "gcd(7, 13)", "1"));
	CHECK (shows (&s, "lcm(4, 6)", "12"));
	CHECK (shows (&s, "lcm(-4, 6)", "12"));
	CHECK (shows (&s, "lcm(0, 5)", "0"));
	CHECK (shows (&s, "lcm(0, 0)", "0"));
	CHECK (shows (&s, "lcm(7, 13)", "91"));

	/* nested */
	CHECK (shows (&s, "max(abs(-3), gcd(8, 12)) + 1", "5"));

	session_free (&s);
}

static void test_variables_and_functions (void)
{
	Session s;

	session_init (&s);

	CHECK (shows (&s, "a = 3", "3"));
	CHECK (shows (&s, "b = a^2 + 1", "10"));
	CHECK (run (&s, "f(x) = 2x + b").kind == ML_VALUE_NONE);
	CHECK (shows (&s, "f(a)", "16"));
	CHECK (shows (&s, "1/3 + 1/6", "1/2"));
	CHECK (shows (&s, "10!", "3628800"));
	CHECK (shows (&s, "a", "3"));
	CHECK (shows (&s, "2a", "6"));

	/* more parameters, calls inside calls */
	CHECK (run (&s, "g(x, y) = x*y - f(x)").kind == ML_VALUE_NONE);
	CHECK (shows (&s, "g(2, 5)", "-4"));
	CHECK (shows (&s, "g(f(1), 2)", "-10"));
	CHECK (shows (&s, "f(f(1))", "34"));
	CHECK (run (&s, "k() = 5").kind == ML_VALUE_NONE);
	CHECK (shows (&s, "k() + 1", "6"));

	/* parameters hide variables and are not seen by the callee */
	CHECK (run (&s, "p(a) = a + 1").kind == ML_VALUE_NONE);
	CHECK (shows (&s, "p(10)", "11"));
	CHECK (shows (&s, "a", "3"));
	CHECK (run (&s, "q(x) = p(x) * a").kind == ML_VALUE_NONE);
	CHECK (shows (&s, "q(4)", "15"));

	/* redefinition shadows */
	CHECK (shows (&s, "a = 100", "100"));
	CHECK (shows (&s, "b", "10"));
	CHECK (shows (&s, "f(1)", "12"));
	CHECK (run (&s, "f(x) = x").kind == ML_VALUE_NONE);
	CHECK (shows (&s, "f(1)", "1"));
	CHECK (shows (&s, "q(4)", "500"));

	/* a call of a number is a product */
	CHECK (shows (&s, "a(2)", "200"));
	CHECK (shows (&s, "(a)(2)", "200"));
	CHECK (is_approx (&s, "pi(2)", 6.28318530718));
	CHECK (run (&s, "r(a) = a(3)").kind == ML_VALUE_NONE);
	CHECK (shows (&s, "r(5)", "15"));

	/* variables and constants can be rebound */
	CHECK (shows (&s, "e = 2", "2"));
	CHECK (shows (&s, "e^3", "8"));
	CHECK (shows (&s, "sin = 4", "4"));
	CHECK (shows (&s, "sin + 1", "5"));

	session_free (&s);
}

static void test_late_binding_and_snapshots (void)
{
	Session  s;
	uint64_t before;
	MlSlice  src = lit ("h(1)");
	MlParseResult p;

	session_init (&s);

	CHECK (run (&s, "h(x) = x + k").kind == ML_VALUE_NONE);
	before = ml_env_snapshot (&s.env);
	CHECK (fails (&s, "h(1)", ML_EVAL_UNDEFINED_NAME));
	CHECK (shows (&s, "k = 5", "5"));
	CHECK (shows (&s, "h(1)", "6"));
	CHECK (shows (&s, "k = 7", "7"));
	CHECK (shows (&s, "h(1)", "8"));

	/* the same line again, in the environment it saw */
	p = ml_parse (src);
	CHECK (p.error.code == ML_PARSE_OK);
	CHECK (
		ml_eval (&s.arena, &s.env, before, src, &p.ast).error
		== ML_EVAL_UNDEFINED_NAME
	);
	CHECK (
		ml_eval (&s.arena, &s.env, before + 2, src, &p.ast).kind
		== ML_VALUE_EXACT
	);
	ml_ast_free (&p.ast);

	session_free (&s);
}

static void test_no_side_effects (void)
{
	Session       s;
	MlSlice       src = lit ("a = 3");
	MlParseResult p   = ml_parse (src);
	MlValue       v;

	session_init (&s);
	CHECK (p.error.code == ML_PARSE_OK);

	v = ml_eval (&s.arena, &s.env, 0, src, &p.ast);
	CHECK (ml_value_is_exact (v));
	CHECK (ml_env_snapshot (&s.env) == 0);

	/* the caller commits */
	CHECK (ml_eval_commit (&s.env, src, &p.ast, v) == ML_OK);
	CHECK (ml_env_snapshot (&s.env) == 1);
	ml_ast_free (&p.ast);

	/* a failed assignment binds nothing */
	CHECK (fails (&s, "z = 1/0", ML_EVAL_DIV_ZERO));
	CHECK (ml_env_snapshot (&s.env) == 1);
	CHECK (fails (&s, "z", ML_EVAL_UNDEFINED_NAME));

	/* expressions bind nothing */
	CHECK (shows (&s, "a + 1", "4"));
	CHECK (ml_env_snapshot (&s.env) == 1);

	session_free (&s);
}

static void test_errors (void)
{
	Session s;

	session_init (&s);

	CHECK (fails (&s, "1/0", ML_EVAL_DIV_ZERO));
	CHECK (fails (&s, "1/(2-2)", ML_EVAL_DIV_ZERO));
	CHECK (fails (&s, "0^-1", ML_EVAL_DIV_ZERO));
	CHECK (fails (&s, "0^0", ML_EVAL_ZERO_POW_ZERO));
	CHECK (fails (&s, "ln(0)", ML_EVAL_DOMAIN));
	CHECK (fails (&s, "ln(-1)", ML_EVAL_DOMAIN));
	CHECK (fails (&s, "log(0)", ML_EVAL_DOMAIN));
	CHECK (fails (&s, "asin(2)", ML_EVAL_DOMAIN));
	CHECK (fails (&s, "acos(-2)", ML_EVAL_DOMAIN));
	CHECK (fails (&s, "sqrt(-1)", ML_EVAL_NOT_REAL));
	CHECK (fails (&s, "(-8)^(1/3)", ML_EVAL_NOT_REAL));
	CHECK (fails (&s, "(1/2)!", ML_EVAL_NOT_INTEGER));
	CHECK (fails (&s, "sqrt(2)!", ML_EVAL_NOT_INTEGER));
	CHECK (fails (&s, "(-1)!", ML_EVAL_DOMAIN));
	CHECK (fails (&s, "gcd(1/2, 3)", ML_EVAL_NOT_INTEGER));
	CHECK (fails (&s, "lcm(sqrt(2), 3)", ML_EVAL_NOT_INTEGER));

	CHECK (fails (&s, "q", ML_EVAL_UNDEFINED_NAME));
	CHECK (fails (&s, "1 + q", ML_EVAL_UNDEFINED_NAME));
	CHECK (fails (&s, "q(2)", ML_EVAL_UNDEFINED_NAME));
	CHECK (fails (&s, "unknown(1, 2)", ML_EVAL_UNDEFINED_NAME));

	CHECK (run (&s, "f(x) = x").kind == ML_VALUE_NONE);
	CHECK (fails (&s, "f", ML_EVAL_NOT_VALUE));
	CHECK (fails (&s, "f + 1", ML_EVAL_NOT_VALUE));
	CHECK (fails (&s, "f(1, 2)", ML_EVAL_ARITY));
	CHECK (fails (&s, "f()", ML_EVAL_ARITY));
	CHECK (fails (&s, "sin(1, 2)", ML_EVAL_ARITY));
	CHECK (fails (&s, "sin()", ML_EVAL_ARITY));
	CHECK (fails (&s, "min(1)", ML_EVAL_ARITY));
	CHECK (fails (&s, "gcd(1, 2, 3)", ML_EVAL_ARITY));

	CHECK (shows (&s, "n = 2", "2"));
	CHECK (fails (&s, "n(1, 2)", ML_EVAL_NOT_FUNCTION));
	CHECK (fails (&s, "n()", ML_EVAL_NOT_FUNCTION));

	/* the first error wins and later parts are not looked at */
	CHECK (fails (&s, "1/0 + q", ML_EVAL_DIV_ZERO));
	CHECK (fails (&s, "q + 1/0", ML_EVAL_UNDEFINED_NAME));
	CHECK (fails (&s, "sin(1/0)", ML_EVAL_DIV_ZERO));
	CHECK (fails (&s, "f(1/0)", ML_EVAL_DIV_ZERO));
	CHECK (fails (&s, "-(1/0)", ML_EVAL_DIV_ZERO));
	CHECK (fails (&s, "(1/0)!", ML_EVAL_DIV_ZERO));
	CHECK (fails (&s, "sqrt(1/0)", ML_EVAL_DIV_ZERO));
	CHECK (fails (&s, "max(1, 1/0)", ML_EVAL_DIV_ZERO));

	/* sizes */
	CHECK (fails (&s, "20001!", ML_EVAL_TOO_LARGE));
	CHECK (fails (&s, "9000!", ML_EVAL_TOO_LARGE));
	CHECK (shows (&s, "0!", "1"));
	CHECK (fails (&s, "2^1000000", ML_EVAL_TOO_LARGE));
	CHECK (fails (&s, "10^10^10", ML_EVAL_TOO_LARGE));
	CHECK (fails (&s, "2^100000000000000000000", ML_EVAL_TOO_LARGE));
	CHECK (fails (&s, "exp(1000)", ML_EVAL_TOO_LARGE));
	CHECK (fails (&s, "1000000000000000000000^100000", ML_EVAL_TOO_LARGE));

	session_free (&s);
}

static void test_error_ranges (void)
{
	Session s;
	MlValue v;

	session_init (&s);

	v = run (&s, "1 + q * 2");
	CHECK (v.error == ML_EVAL_UNDEFINED_NAME);
	CHECK (v.error_start == 4 && v.error_len == 1);

	v = run (&s, "3 + 1/0");
	CHECK (v.error == ML_EVAL_DIV_ZERO);
	CHECK (v.error_start == 4 && v.error_len == 3);

	v = run (&s, "sin(2) + ln(0)");
	CHECK (v.error == ML_EVAL_DOMAIN);
	CHECK (v.error_start == 9 && v.error_len == 5);

	v = run (&s, "x = 1/0");
	CHECK (v.error == ML_EVAL_DIV_ZERO);
	CHECK (v.error_start == 4 && v.error_len == 3);

	v = run (&s, "foo(2)");
	CHECK (v.error == ML_EVAL_UNDEFINED_NAME);
	CHECK (v.error_start == 0 && v.error_len == 3);

	/* inside a function body the error is reported at the call */
	CHECK (run (&s, "d(x) = 1 / x").kind == ML_VALUE_NONE);
	v = run (&s, "2 + d(0)");
	CHECK (v.error == ML_EVAL_DIV_ZERO);
	CHECK (v.error_start == 4 && v.error_len == 4);

	CHECK (run (&s, "u(x) = x + nothere").kind == ML_VALUE_NONE);
	v = run (&s, "u(1)");
	CHECK (v.error == ML_EVAL_UNDEFINED_NAME);
	CHECK (v.error_start == 0 && v.error_len == 4);

	{
		MlStr out;

		ml_str_init (&out);
		CHECK (ml_error_message (&out, v, lit ("u(1)")) == ML_OK);
		CHECK (ml_slice_equal (ml_str_view (out), lit ("undefined name")));
		ml_str_free (&out);

		v = run (&s, "1 + nothere");
		ml_str_init (&out);
		CHECK (ml_error_message (&out, v, lit ("1 + nothere")) == ML_OK);
		CHECK (
			ml_slice_equal (
				ml_str_view (out),
				lit ("undefined name 'nothere'")
			)
		);
		ml_str_free (&out);
	}

	/* an error in an argument keeps its own place in the line */
	v = run (&s, "d(1/0) + 1");
	CHECK (v.error == ML_EVAL_DIV_ZERO);
	CHECK (v.error_start == 2 && v.error_len == 3);

	session_free (&s);
}

static void test_depth (void)
{
	Session s;
	MlStr   line;
	MlValue v;

	session_init (&s);

	CHECK (run (&s, "g(n) = g(n)").kind == ML_VALUE_NONE);
	CHECK (fails (&s, "g(1)", ML_EVAL_DEPTH));
	CHECK (run (&s, "h(n) = 1 + h(n + 1)").kind == ML_VALUE_NONE);
	CHECK (fails (&s, "h(1)", ML_EVAL_DEPTH));

	/* mutual recursion */
	CHECK (run (&s, "m(n) = w(n)").kind == ML_VALUE_NONE);
	CHECK (run (&s, "w(n) = m(n)").kind == ML_VALUE_NONE);
	CHECK (fails (&s, "m(1)", ML_EVAL_DEPTH));

	/* the environment is still fine afterwards */
	CHECK (shows (&s, "1 + 1", "2"));

	/* a long chain: 900 terms is a tree 900 levels high */
	ml_str_init (&line);
	CHECK (ml_str_append (&line, lit ("1")) == ML_OK);

	for (uint64_t i = 0; i < 850; i++)
	{
		CHECK (ml_str_append (&line, lit ("+1")) == ML_OK);
	}

	v = run (&s, ml_str_cstr (line));
	CHECK (ml_value_is_exact (v));
	CHECK (ml_value_to_i64 (v).value == 851);
	ml_str_free (&line);

	/* a chain of 2000 functions that call each other: far deeper than the
	   C stack could take with a recursive evaluator, fine on the heap */
	CHECK (run (&s, "c0(x) = x + 1").kind == ML_VALUE_NONE);

	for (uint64_t i = 1; i < 2000; i++)
	{
		MlStr def;

		ml_str_init (&def);
		CHECK (
			ml_str_appendf (
				&def,
				"c%llu(x) = c%llu(x) + 1",
				(unsigned long long)i,
				(unsigned long long)(i - 1)
			) == ML_OK
		);
		CHECK (run (&s, ml_str_cstr (def)).kind == ML_VALUE_NONE);
		ml_str_free (&def);
	}

	v = run (&s, "c1999(5)");
	CHECK (ml_value_is_exact (v));
	CHECK (ml_value_to_i64 (v).value == 2005);

	/* a bounded recursion through a function */
	CHECK (run (&s, "t(n) = n + 1").kind == ML_VALUE_NONE);
	CHECK (shows (&s, "t(t(t(t(t(t(t(t(1))))))))", "9"));

	session_free (&s);
}

static void test_commit (void)
{
	Session       s;
	MlSlice       fsrc = lit ("sq(x) = x^2");
	MlParseResult p    = ml_parse (fsrc);

	session_init (&s);

	/* commit of a function and of statements that define nothing */
	CHECK (p.error.code == ML_PARSE_OK);
	CHECK (ml_eval_commit (&s.env, fsrc, &p.ast, ml_value_none ()) == ML_OK);
	CHECK (ml_env_lookup (&s.env, lit ("sq"), 1).kind == ML_BIND_FUNC);
	ml_ast_free (&p.ast);

	{
		MlSlice       src = lit ("1 + 2");
		MlParseResult e   = ml_parse (src);

		CHECK (
			ml_eval_commit (
				&s.env,
				src,
				&e.ast,
				ml_value_from_i64 (&s.arena, 3)
			) == ML_OK
		);
		CHECK (ml_env_snapshot (&s.env) == 1);
		ml_ast_free (&e.ast);
	}

	CHECK (shows (&s, "sq(7)", "49"));
	CHECK (shows (&s, "sq(1/2)", "1/4"));

	session_free (&s);
}

static void test_arena_independence (void)
{
	Session s;

	session_init (&s);
	CHECK (shows (&s, "big = 2^200", "1606938044258990275541962092341162602522"
		"202993782792835301376"));

	/* a line's temporary memory can be dropped; bindings keep their digits */
	ml_arena_reset (&s.arena);
	CHECK (shows (&s, "big / 2^199", "2"));

	session_free (&s);
}

void run_eval_tests (void)
{
	test_arithmetic ();
	test_approximation ();
	test_builtins ();
	test_variables_and_functions ();
	test_late_binding_and_snapshots ();
	test_no_side_effects ();
	test_errors ();
	test_error_ranges ();
	test_depth ();
	test_commit ();
	test_arena_independence ();
}
