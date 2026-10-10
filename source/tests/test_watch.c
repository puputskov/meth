#include "test.h"

#include <matlog/session/watch.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static void add_in (MlSession *s, MlDomain domain, const char *text)
{
	CHECK (ml_session_append (s, ML_LINE_MATH, domain, lit (text)) == ML_OK);
}

/* 1 when row i has the name, the type and the value. */
static int32_t row_is (
	const MlWatchRowVec *rows,
	uint64_t            i,
	const char          *name,
	const char          *type,
	const char          *value,
	int32_t             fresh
)
{
	int32_t ok = i < rows->len;

	if (ok)
	{
		MlWatchRow r = rows->data [i];

		ok = (
			   ml_slice_equal (ml_str_view (r.name), lit (name))
			&& ml_slice_equal (ml_str_view (r.type), lit (type))
			&& ml_slice_equal (ml_str_view (r.value), lit (value))
			&& r.fresh == fresh
		);

		if (!ok)
		{
			printf (
				"  row %d: %s | %s | %s | %d, expected %s | %s | %s | %d\n",
				(int)i,
				ml_str_cstr (r.name),
				ml_str_cstr (r.type),
				ml_str_cstr (r.value),
				r.fresh,
				name,
				type,
				value,
				fresh
			);
		}
	}

	return ok;
}

static void test_rows (void)
{
	MlSession     s;
	MlResults     r;
	MlWatchRowVec rows;

	ml_session_init (&s);
	ml_results_init (&r);
	MlWatchRowVec_init (&rows);
	add_in (&s, ML_DOMAIN_ALGEBRA, "a = 3");
	add_in (&s, ML_DOMAIN_ALGEBRA, "A = [[1, 2], [3, 4]]");
	add_in (&s, ML_DOMAIN_ALGEBRA, "v = [1, 2, 3]");
	add_in (&s, ML_DOMAIN_ALGEBRA, "f(x, y) = x^2 + y");
	add_in (&s, ML_DOMAIN_ALGEBRA, "r = sqrt(2)");
	add_in (&s, ML_DOMAIN_ALGEBRA, "a = 5");
	add_in (&s, ML_DOMAIN_BOOLEAN, "p = x & y");
	add_in (&s, ML_DOMAIN_LAMBDA, "id = \xCE\xBBx. x");
	add_in (&s, ML_DOMAIN_ALGEBRA, "d = diff(x^3, x)");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);

	/* before anything is defined */
	CHECK (ml_watch_rows (&rows, &r, 0) == ML_OK);
	CHECK (rows.len == 1);
	CHECK (row_is (&rows, 0, "a", "exact", "3", 1));
	ml_watch_rows_free (&rows);

	/* the names after a line, the ones of that line marked */
	MlWatchRowVec_init (&rows);
	CHECK (ml_watch_rows (&rows, &r, 3) == ML_OK);
	CHECK (rows.len == 4);
	CHECK (row_is (&rows, 0, "a", "exact", "3", 0));
	CHECK (row_is (&rows, 1, "A", "matrix 2x2", "[[1, 2], [3, 4]]", 0));
	CHECK (row_is (&rows, 2, "v", "vector 3", "[[1], [2], [3]]", 0));
	CHECK (row_is (&rows, 3, "f", "f (x, y)", "f(x, y) = x^2 + y", 1));
	ml_watch_rows_free (&rows);

	/* a redefined name has one row, with its newest value, at the end */
	MlWatchRowVec_init (&rows);
	CHECK (ml_watch_rows (&rows, &r, 5) == ML_OK);
	CHECK (rows.len == 5);
	CHECK (row_is (&rows, 3, "r", "approx", "\xE2\x89\x88 1.41421356", 0));
	CHECK (row_is (&rows, 4, "a", "exact", "5", 1));
	ml_watch_rows_free (&rows);

	/* the definitions of the other domains come after the variables */
	MlWatchRowVec_init (&rows);
	CHECK (ml_watch_rows (&rows, &r, 7) == ML_OK);
	CHECK (rows.len == 7);
	CHECK (row_is (&rows, 5, "p", "boolean", "x \xE2\x88\xA7 y", 0));
	CHECK (row_is (&rows, 6, "id", "lambda", "\xCE\xBBx. x", 1));
	ml_watch_rows_free (&rows);

	/* a symbolic value, and the end of the session (where a new line goes) */
	MlWatchRowVec_init (&rows);
	CHECK (ml_watch_rows (&rows, &r, 100) == ML_OK);
	CHECK (rows.len == 8);
	CHECK (row_is (&rows, 5, "d", "expr", "3x\xC2\xB2", 0));
	ml_watch_rows_free (&rows);

	ml_results_free (&r);
	ml_session_free (&s);
}

void run_watch_tests (void);

void run_watch_tests (void)
{
	test_rows ();
}
