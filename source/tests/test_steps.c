#include "test.h"

#include <matlog/session/steps.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static void add (MlSession *s, const char *text)
{
	CHECK (
		ml_session_append (
			s,
			ML_LINE_MATH,
			ML_DOMAIN_ALGEBRA,
			lit (text)
		) == ML_OK
	);
}

/* The steps of line idx, "" when there are none. */
static int32_t steps_contain (
	const MlResults *r,
	const MlSession *s,
	uint64_t        idx,
	const char      *needle
)
{
	MlStr       out;
	MlLineSteps st;
	int32_t     same;

	ml_str_init (&out);
	st   = ml_line_steps (&out, r, s, idx);
	same = (
		   st.result == ML_OK
		&& st.found
		&& strstr (ml_str_cstr (out), needle) != NULL
	);

	if (!same)
	{
		printf (
			"  line %d: found %d, \"%s\" not in\n%s\n",
			(int32_t)idx,
			st.found,
			needle,
			ml_str_cstr (out)
		);
	}

	ml_str_free (&out);
	return same;
}

static int32_t no_steps (const MlResults *r, const MlSession *s, uint64_t idx)
{
	MlStr       out;
	MlLineSteps st;
	int32_t     same;

	ml_str_init (&out);
	st   = ml_line_steps (&out, r, s, idx);
	same = st.result == ML_OK && !st.found && out.len == 0;
	ml_str_free (&out);
	return same;
}

static void test_line_steps (void)
{
	MlSession s;
	MlResults r;

	ml_session_init (&s);
	ml_results_init (&r);
	add (&s, "A = [[1,2],[3,4]]");
	add (&s, "det(A)");
	add (&s, "inv(A)");
	add (&s, "A^-1");
	add (&s, "B = rref(A)");
	add (&s, "A * A");
	add (&s, "solve(A, [5,6])");
	add (&s, "rank(A)");
	add (&s, "1 + 1");
	add (&s, "A + A");
	add (&s, "det(1)");
	add (&s, "2 +");
	add (&s, "3 ft to m");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);

	CHECK (steps_contain (&r, &s, 1, "R2 \xE2\x86\x90 R2 \xE2\x88\x92 3"));
	CHECK (steps_contain (&r, &s, 1, "determinant = 1"));
	CHECK (steps_contain (&r, &s, 2, "identity beside it"));
	CHECK (steps_contain (&r, &s, 3, "identity beside it"));
	CHECK (steps_contain (&r, &s, 4, "Row reduce (Gauss-Jordan)"));
	CHECK (
		steps_contain (&r, &s, 5, "c11 = 1\xC2\xB7" "1 + 2\xC2\xB7" "3 = 7")
	);
	CHECK (steps_contain (&r, &s, 6, "right side beside it"));
	CHECK (steps_contain (&r, &s, 7, "Row reduce (Gauss-Jordan)"));
	CHECK (steps_contain (&r, &s, 12, "3 ft in SI units: 1143/1250 m"));
	CHECK (steps_contain (&r, &s, 12, "1 m = 1 m"));
	CHECK (steps_contain (&r, &s, 12, "1143/1250 / 1 = 1143/1250 m"));

	/* lines without steps, errors, lines that do not exist, other kinds */
	CHECK (no_steps (&r, &s, 0));
	CHECK (no_steps (&r, &s, 8));
	CHECK (no_steps (&r, &s, 9));
	CHECK (no_steps (&r, &s, 10));
	CHECK (no_steps (&r, &s, 11));
	CHECK (no_steps (&r, &s, 99));

	/* the environment of the line is used: A is redefined below line 1 */
	add (&s, "A = [[2,0],[0,2]]");
	add (&s, "det(A)");
	CHECK (ml_results_update (&r, &s, 12) == ML_OK);
	CHECK (steps_contain (&r, &s, 14, "determinant = 2\xC2\xB7" "2 = 4"));
	CHECK (steps_contain (&r, &s, 1, "determinant = 1\xC2\xB7" "-2 = -2"));

	ml_results_free (&r);
	ml_session_free (&s);
}

static void test_symbolic_steps (void)
{
	MlSession s;
	MlResults r;

	ml_session_init (&s);
	ml_results_init (&r);
	add (&s, "diff(x^3 + sin(2x), x)");
	add (&s, "simplify(x + x + 1 + 2)");
	add (&s, "expand((x + 1)^2)");
	add (&s, "integrate(3x^2 + cos(x), x)");
	add (&s, "integrate(x^2, x, 0, 1)");
	add (&s, "subs(x + 1, x, 2)");
	add (&s, "p = diff(x^2, x)");
	add (&s, "diff(x*sin(x), x)");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);

	CHECK (steps_contain (&r, &s, 0, "Differentiate, rule by rule:"));
	CHECK (steps_contain (&r, &s, 0, "(power rule)"));
	CHECK (steps_contain (&r, &s, 0, "(chain rule)"));
	CHECK (steps_contain (&r, &s, 0, "(sum rule)"));
	CHECK (steps_contain (&r, &s, 1, "Simplify:"));
	CHECK (steps_contain (&r, &s, 1, "collect like terms: "));
	CHECK (steps_contain (&r, &s, 2, "Multiply out:"));
	CHECK (steps_contain (&r, &s, 3, "linearity: integrate term by term"));
	CHECK (steps_contain (&r, &s, 3, "(power rule)"));
	CHECK (steps_contain (&r, &s, 3, "(cosine)"));
	CHECK (steps_contain (&r, &s, 4, "Integrate with the table:"));
	CHECK (steps_contain (&r, &s, 6, "d/dx"));
	CHECK (steps_contain (&r, &s, 7, "(product rule)"));

	/* no steps for a substitution */
	CHECK (no_steps (&r, &s, 5));

	ml_results_free (&r);
	ml_session_free (&s);
}

void run_steps_tests (void);

void run_steps_tests (void)
{
	test_symbolic_steps ();
	test_line_steps ();
}
