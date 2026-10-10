#include "test.h"

#include <meth/core/utf8.h>
#include <meth/session/steps.h>

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

static void add_physics (MlSession *s, const char *text)
{
	CHECK (
		ml_session_append (
			s,
			ML_LINE_MATH,
			ML_DOMAIN_PHYSICS,
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

/*
 * 1 when the lines of the steps of line idx that end in a rule name in
 * brackets, "(power rule)", have the bracket in one column (counted in code
 * points), and there are at least `count` of them.
 */
static int32_t notes_aligned (
	const MlResults *r,
	const MlSession *s,
	uint64_t        idx,
	uint64_t        count
)
{
	MlStr       out;
	MlLineSteps st;
	uint64_t    found = 0;
	uint64_t    col   = 0;
	uint64_t    pos   = 0;
	int32_t     same;

	ml_str_init (&out);
	st   = ml_line_steps (&out, r, s, idx);
	same = st.result == ML_OK && st.found;

	while (same && pos < out.len)
	{
		uint64_t end = pos;
		MlSlice  line;
		uint64_t open;

		while (end < out.len && out.data [end] != '\n')
		{
			end++;
		}

		line = ml_slice_sub (ml_str_view (out), pos, end - pos);
		open = line.len;
		pos  = end + 1;

		for (uint64_t k = line.len; k > 0 && open == line.len; k--)
		{
			open = ml_choose (line.data [k - 1] == '(', k - 1, open);
		}

		if (
			   open < line.len
			&& line.data [line.len - 1] == ')'
			&& line.len - open > 6
			&& ml_slice_equal (
				ml_slice_sub (line, line.len - 6, 6),
				lit (" rule)")
			)
		)
		{
			uint64_t where = ml_utf8_count (ml_slice_sub (line, 0, open));

			same = same && (found == 0 || where == col);
			col  = where;
			found++;
		}
	}

	if (!(same && found >= count))
	{
		printf (
			"  line %d: %d notes, not in one column:\n%s\n",
			(int32_t)idx,
			(int32_t)found,
			ml_str_cstr (out)
		);
	}

	ml_str_free (&out);
	return same && found >= count;
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
	add_physics (&s, "3 ft to m");
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

	/* the rule names stand in one column, however long the steps are */
	CHECK (notes_aligned (&r, &s, 0, 3));
	CHECK (notes_aligned (&r, &s, 7, 2));
	CHECK (steps_contain (&r, &s, 0, "(power rule)"));

	/* no steps for a substitution */
	CHECK (no_steps (&r, &s, 5));

	ml_results_free (&r);
	ml_session_free (&s);
}

static void test_logic_steps (void)
{
	MlSession s;
	MlResults r;

	ml_session_init (&s);
	ml_results_init (&r);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_MATH,
			ML_DOMAIN_LAMBDA,
			lit ("succ = \xCE\xBBn f x. f (n f x)")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s, ML_LINE_MATH, ML_DOMAIN_LAMBDA, lit ("succ 1")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s, ML_LINE_MATH, ML_DOMAIN_LAMBDA, lit ("trace succ 1")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s, ML_LINE_MATH, ML_DOMAIN_BOOLEAN, lit ("a & b")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s, ML_LINE_MATH, ML_DOMAIN_BOOLEAN, lit ("table a | b")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s, ML_LINE_MATH, ML_DOMAIN_BOOLEAN, lit ("true")
		) == ML_OK
	);
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);

	/* a lambda line has the beta steps, with or without `trace` */
	CHECK (steps_contain (&r, &s, 1, "\xE2\x86\x92 "));
	CHECK (steps_contain (&r, &s, 1, "(\xCE\xBBn f x. f (n f x))"));
	CHECK (steps_contain (&r, &s, 2, "\xE2\x86\x92 "));

	/* a definition has nothing to reduce */
	CHECK (no_steps (&r, &s, 0));

	/* a boolean line has its truth table */
	CHECK (steps_contain (&r, &s, 3, "0 0 \xE2\x94\x82 0"));
	CHECK (steps_contain (&r, &s, 3, "1 1 \xE2\x94\x82 1"));
	CHECK (steps_contain (&r, &s, 4, "0 1 \xE2\x94\x82 1"));
	/* even a constant has its one row */
	CHECK (steps_contain (&r, &s, 5, "\xE2\x94\x82 1"));

	ml_results_free (&r);
	ml_session_free (&s);
}

/* A line of steps (...) asks for its steps, wherever the operation is. */
static void test_steps_function (void)
{
	MlSession s;
	MlResults r;

	ml_session_init (&s);
	ml_results_init (&r);
	add (&s, "A = [[1, 2], [3, 4]]");
	add (&s, "det(A)");
	add (&s, "steps(det(A))");
	add (&s, "steps(2 * det(A))");
	add (&s, "d = steps(inv(A))");
	add (&s, "steps(1 + 1)");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);

	/* the value is the value of the argument */
	CHECK (ml_slice_equal (ml_results_get (&r, 2).text, lit ("-2")));
	CHECK (ml_slice_equal (ml_results_get (&r, 3).text, lit ("-4")));

	/* only the lines that call steps ask for steps, and keep them */
	CHECK (!ml_line_wants_steps (&s, 0));
	CHECK (!ml_line_wants_steps (&s, 1));
	CHECK (ml_line_wants_steps (&s, 2));
	CHECK (ml_line_wants_steps (&s, 3));
	CHECK (ml_line_wants_steps (&s, 4));
	CHECK (ml_results_get (&r, 1).steps.len == 0);
	CHECK (ml_results_get (&r, 2).steps.len > 0);
	CHECK (ml_results_get (&r, 3).steps.len > 0);
	CHECK (ml_results_get (&r, 4).steps.len > 0);

	/* the same steps as the call inside gives */
	CHECK (steps_contain (&r, &s, 2, "determinant = 1\xC2\xB7-2 = -2"));
	CHECK (steps_contain (&r, &s, 3, "determinant = 1\xC2\xB7-2 = -2"));
	CHECK (steps_contain (&r, &s, 4, "Row reduce the matrix"));

	/* nothing to explain: the value, and no steps */
	CHECK (ml_slice_equal (ml_results_get (&r, 5).text, lit ("2")));
	CHECK (ml_results_get (&r, 5).steps.len == 0);

	ml_results_free (&r);
	ml_session_free (&s);
}

void run_steps_tests (void);

void run_steps_tests (void)
{
	test_steps_function ();
	test_symbolic_steps ();
	test_line_steps ();
	test_logic_steps ();
}
