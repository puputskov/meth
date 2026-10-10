#include "test.h"

#include <matlog/session/results.h>
#include <matlog/session/session.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* A session of lines of one domain, evaluated. */
typedef struct Run
{
	MlSession s;
	MlResults r;
} Run;

static void run_start (
	Run               *run,
	MlDomain          domain,
	const char *const *lines,
	uint64_t          n
)
{
	ml_session_init (&run->s);
	ml_results_init (&run->r);

	for (uint64_t i = 0; i < n; i++)
	{
		CHECK (
			ml_session_append (&run->s, ML_LINE_MATH, domain, lit (lines [i]))
			== ML_OK
		);
	}

	CHECK (ml_results_update (&run->r, &run->s, 0) == ML_OK);
}

static void run_end (Run *run)
{
	ml_results_free (&run->r);
	ml_session_free (&run->s);
}

/* 1 when line idx has the value text. */
static int32_t gives (const Run *run, uint64_t idx, const char *text)
{
	MlResultView v  = ml_results_get (&run->r, idx);
	int32_t      ok = (
		   v.kind == ML_RESULT_VALUE
		&& ml_slice_equal (v.text, lit (text))
	);

	if (!ok)
	{
		printf (
			"  line %d: \"%.*s\" (kind %d), expected \"%s\"\n",
			(int)idx,
			(int)v.text.len,
			v.text.data,
			(int)v.kind,
			text
		);
	}

	return ok;
}

static int32_t fails (const Run *run, uint64_t idx, const char *code)
{
	MlResultView v  = ml_results_get (&run->r, idx);
	int32_t      ok = (
		   v.kind == ML_RESULT_ERROR
		&& ml_slice_equal (v.code, lit (code))
	);

	if (!ok)
	{
		printf (
			"  line %d: kind %d \"%.*s\", expected error %s\n",
			(int)idx,
			(int)v.kind,
			(int)v.code.len,
			v.code.data,
			code
		);
	}

	return ok;
}

static void test_blades (void)
{
	static const char *const lines [] = {
		"e0", "e1", "e12", "e21", "e012", "e120", "e5", "e11", "e", "e0123"
	};

	Run run;

	run_start (&run, ML_DOMAIN_PGA2, lines, ml_count (lines));
	CHECK (gives (&run, 0, "e0"));
	CHECK (gives (&run, 1, "e1"));
	CHECK (gives (&run, 2, "e12"));
	CHECK (gives (&run, 3, "-e12"));
	CHECK (gives (&run, 4, "e012"));

	/* e120 is e012 after two swaps */
	CHECK (gives (&run, 5, "e012"));

	/* not blades of the plane: a name that stands for nothing */
	CHECK (fails (&run, 6, "undefined_name"));
	CHECK (fails (&run, 7, "undefined_name"));
	CHECK (ml_results_get (&run.r, 8).kind == ML_RESULT_VALUE);
	CHECK (fails (&run, 9, "undefined_name"));
	run_end (&run);

	/* the basis of space has e3, the plane has not */
	run_start (&run, ML_DOMAIN_PGA3, lines + 6, 1);
	CHECK (fails (&run, 0, "undefined_name"));
	run_end (&run);

	{
		static const char *const space [] = { "e3", "e0123", "e3210" };

		run_start (&run, ML_DOMAIN_PGA3, space, ml_count (space));
		CHECK (gives (&run, 0, "e3"));
		CHECK (gives (&run, 1, "e0123"));
		CHECK (gives (&run, 2, "e0123"));
		run_end (&run);
	}

	/* in the algebra domain the names are names like any other */
	{
		static const char *const alg [] = { "e1", "e1 = 3", "e1 + 1" };

		run_start (&run, ML_DOMAIN_ALGEBRA, alg, ml_count (alg));
		CHECK (fails (&run, 0, "undefined_name"));
		CHECK (gives (&run, 2, "4"));
		run_end (&run);
	}

	/* a variable of the name hides the blade */
	{
		static const char *const hide [] = { "e1 = 5", "e1 + 1", "e2 + 1" };

		run_start (&run, ML_DOMAIN_PGA2, hide, ml_count (hide));
		CHECK (gives (&run, 1, "6"));
		CHECK (gives (&run, 2, "1 + e2"));
		run_end (&run);
	}
}

static void test_products (void)
{
	static const char *const lines [] = {
		"e1 * e2",
		"e2 * e1",
		"e1 * e1",
		"e0 * e0",
		"(e1 + e2) * (e1 + e2)",
		"e12 * e12",
		"e1 ^ e2",
		"e2 ^ e1",
		"e1 ^ e1",
		"e0 ^ e0",
		"(e1 + e2) ^ (e1 - e2)",
		"2 * e1 - e2",
		"e1 / 2",
		"(e1 + e2) / (e1 + e2)",
		"e1 + e1",
		"e1 - e1",
		"2 ^ 3",
		"e0 * e1",
		"e1 * e0",
		"e01 * e01",
		"inner (e1 + e2, e1)",
		"inner (e12, e1)",
		"inner (e1, e12)",
		"inner (e0, e0)"
	};

	Run run;

	run_start (&run, ML_DOMAIN_PGA2, lines, ml_count (lines));
	CHECK (gives (&run, 0, "e12"));
	CHECK (gives (&run, 1, "-e12"));
	CHECK (gives (&run, 2, "1"));

	/* e0 squares to zero: the degenerate direction */
	CHECK (gives (&run, 3, "0"));
	CHECK (gives (&run, 4, "2"));
	CHECK (gives (&run, 5, "-1"));
	CHECK (gives (&run, 6, "e12"));
	CHECK (gives (&run, 7, "-e12"));
	CHECK (gives (&run, 8, "0"));
	CHECK (gives (&run, 9, "0"));
	CHECK (gives (&run, 10, "-2e12"));
	CHECK (gives (&run, 11, "2e1 - e2"));
	CHECK (gives (&run, 12, "(1/2)e1"));
	CHECK (gives (&run, 13, "1"));
	CHECK (gives (&run, 14, "2e1"));
	CHECK (gives (&run, 15, "0"));

	/* ^ between numbers is still the power */
	CHECK (gives (&run, 16, "8"));
	CHECK (gives (&run, 17, "e01"));
	CHECK (gives (&run, 18, "-e01"));
	CHECK (gives (&run, 19, "0"));
	CHECK (gives (&run, 20, "1"));
	CHECK (gives (&run, 21, "-e2"));
	CHECK (gives (&run, 22, "e2"));
	CHECK (gives (&run, 23, "0"));
	run_end (&run);
}

static void test_functions (void)
{
	static const char *const lines [] = {
		"reverse (e12)",
		"reverse (e1 + e12)",
		"reverse (e123)",
		"grade (1 + e1 + e12, 1)",
		"grade (1 + e1 + e12, 0)",
		"grade (e1, 2)",
		"scalar (2 + e1)",
		"scalar (5)",
		"dual (e1)",
		"dual (e12)",
		"inv (e1 + e2)",
		"inv (e0)",
		"norm (3 * e1 + 4 * e2)",
		"norm (e0)",
		"unit (3 * e1 + 4 * e2)",
		"unit (e0)",
		"(e1 + e2) ^ 2",
		"(e1 + e2) ^ -1",
		"e12 ^ 4",
		"e1 / e0"
	};

	Run run;

	run_start (&run, ML_DOMAIN_PGA3, lines, ml_count (lines));
	CHECK (gives (&run, 0, "-e12"));
	CHECK (gives (&run, 1, "e1 - e12"));
	CHECK (gives (&run, 2, "-e123"));
	CHECK (gives (&run, 3, "e1"));
	CHECK (gives (&run, 4, "1"));
	CHECK (gives (&run, 5, "0"));
	CHECK (gives (&run, 6, "2"));
	CHECK (gives (&run, 7, "5"));

	/* e1 ^ dual (e1) is the pseudoscalar e0123 */
	CHECK (gives (&run, 8, "-e023"));
	CHECK (gives (&run, 9, "e03"));
	CHECK (gives (&run, 10, "(1/2)e1 + (1/2)e2"));

	/* e0 has no inverse: e0 reverse (e0) is zero */
	CHECK (fails (&run, 11, "div_zero"));
	CHECK (gives (&run, 12, "5"));
	CHECK (gives (&run, 13, "0"));
	CHECK (gives (&run, 14, "(3/5)e1 + (4/5)e2"));
	CHECK (fails (&run, 15, "div_zero"));
	CHECK (gives (&run, 16, "2"));
	CHECK (gives (&run, 17, "(1/2)e1 + (1/2)e2"));
	CHECK (gives (&run, 18, "1"));
	CHECK (fails (&run, 19, "div_zero"));
	run_end (&run);
}

static void test_motors (void)
{
	static const char *const lines [] = {
		"P = point (1, 2)",
		"T = translator (3, 4)",
		"T * P * reverse (T)",
		"point (4, 6)",
		"R = rotor (0)",
		"R * P * reverse (R)",
		"Q = rotor (1.5707963267948966) * point (1, 0) * "
		"reverse (rotor (1.5707963267948966))",
		"grade (Q, 2)",
		"line (1, 0, -2)",
		"join (point (0, 0), point (1, 1))",
		"line (1, 0, -2) ^ line (0, 1, -3)",
		"point (2, 3)",
		"translator (1)",
		"point (1)",
		"rotor (1, 2)"
	};

	Run run;

	run_start (&run, ML_DOMAIN_PGA2, lines, ml_count (lines));
	CHECK (gives (&run, 0, "2e01 - e02 + e12"));
	CHECK (gives (&run, 2, "6e01 - 4e02 + e12"));
	CHECK (gives (&run, 3, "6e01 - 4e02 + e12"));

	/* the rotor of zero is exactly one */
	CHECK (gives (&run, 5, "2e01 - e02 + e12"));

	/* a quarter turn takes (1, 0) to (0, 1), up to rounding */
	CHECK (gives (&run, 7, "\xE2\x89\x88 e01 + e12"));
	CHECK (gives (&run, 8, "-2e0 + e1"));

	/* the line through two points, the point where two lines meet */
	CHECK (gives (&run, 9, "-e1 + e2"));
	CHECK (gives (&run, 10, "3e01 - 2e02 + e12"));
	CHECK (gives (&run, 11, "3e01 - 2e02 + e12"));
	CHECK (fails (&run, 12, "arity"));
	CHECK (fails (&run, 13, "arity"));
	CHECK (fails (&run, 14, "type"));
	run_end (&run);

	{
		static const char *const space [] = {
			"T = translator (1, 2, 3)",
			"T * point (1, 1, 1) * reverse (T)",
			"point (2, 3, 4)",
			"R = rotor (1.5707963267948966, 0, 0, 1)",
			"grade (R * point (1, 0, 0) * reverse (R), 3)",
			"plane (1, 0, 0, -2)",
			"join (point (0, 0, 0), point (1, 0, 0))",
			"plane (1, 0, 0, 0) ^ plane (0, 1, 0, 0)",
			"rotor (1, 0, 0, 0)"
		};

		run_start (&run, ML_DOMAIN_PGA3, space, ml_count (space));
		CHECK (gives (&run, 1, "-4e012 + 3e013 - 2e023 + e123"));
		CHECK (gives (&run, 2, "-4e012 + 3e013 - 2e023 + e123"));
		CHECK (gives (&run, 4, "\xE2\x89\x88 e013 + e123"));
		CHECK (gives (&run, 5, "-2e0 + e1"));
		CHECK (gives (&run, 6, "e23"));
		CHECK (gives (&run, 7, "e12"));

		/* an axis of length zero turns nothing */
		CHECK (fails (&run, 8, "domain"));
		run_end (&run);
	}
}

static void test_mixing (void)
{
	static const char *const lines [] = {
		"A = e1 + 2",
		"A * A",
		"A + 0.5",
		"e1 + [[1, 2], [3, 4]]",
		"e1 < e2",
		"sin (e1)",
		"x = e12",
		"x * x",
		"f (a) = a * e1",
		"f (e2)",
		"(e1 + 1) * (e1 - 1)"
	};

	Run run;

	run_start (&run, ML_DOMAIN_PGA2, lines, ml_count (lines));
	CHECK (gives (&run, 1, "5 + 4e1"));
	CHECK (gives (&run, 2, "5/2 + e1"));
	CHECK (fails (&run, 3, "type"));
	CHECK (ml_results_get (&run.r, 4).kind == ML_RESULT_ERROR);
	CHECK (ml_results_get (&run.r, 5).kind == ML_RESULT_ERROR);
	CHECK (gives (&run, 7, "-1"));
	CHECK (gives (&run, 9, "-e12"));
	CHECK (gives (&run, 10, "0"));
	run_end (&run);

	/* variables flow between domains, a multivector of the plane does not
	   meet one of space */
	{
		static const char *const two [] = { "p = point (1, 2)" };
		static const char *const three [] = { "q = point (1, 2, 3)", "q * q" };

		run_start (&run, ML_DOMAIN_PGA2, two, 1);
		CHECK (ml_results_get (&run.r, 0).kind == ML_RESULT_VALUE);
		run_end (&run);

		run_start (&run, ML_DOMAIN_PGA3, three, 2);
		CHECK (ml_results_get (&run.r, 1).kind == ML_RESULT_VALUE);
		run_end (&run);
	}
}

void run_pga_tests (void);

void run_pga_tests (void)
{
	test_blades ();
	test_products ();
	test_functions ();
	test_motors ();
	test_mixing ();
}
