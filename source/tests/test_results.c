#include "test.h"

#include <matlog/platform/fs.h>
#include <matlog/session/results.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* Adds an algebra math line to the session. */
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

static void add_include (MlSession *s, const char *path)
{
	CHECK (
		ml_session_append (
			s,
			ML_LINE_INCLUDE,
			ML_DOMAIN_ALGEBRA,
			lit (path)
		) == ML_OK
	);
}

static int32_t is_value (const MlResults *r, uint64_t idx, const char *text)
{
	MlResultView v    = ml_results_get (r, idx);
	int32_t      same = (
		   v.kind == ML_RESULT_VALUE
		&& ml_slice_equal (v.text, lit (text))
	);

	if (!same)
	{
		printf (
			"  line %d: kind %d \"%.*s\", expected \"%s\"\n",
			(int32_t)idx,
			(int32_t)v.kind,
			(int32_t)v.text.len,
			v.text.data,
			text
		);
	}

	return same;
}

static int32_t is_error (const MlResults *r, uint64_t idx, const char *text)
{
	MlResultView v    = ml_results_get (r, idx);
	int32_t      same = (
		   v.kind == ML_RESULT_ERROR
		&& ml_slice_equal (v.text, lit (text))
	);

	if (!same)
	{
		printf (
			"  line %d: kind %d \"%.*s\", expected error \"%s\"\n",
			(int32_t)idx,
			(int32_t)v.kind,
			(int32_t)v.text.len,
			v.text.data,
			text
		);
	}

	return same;
}

static int32_t is_none (const MlResults *r, uint64_t idx)
{
	return ml_results_get (r, idx).kind == ML_RESULT_NONE;
}

static void test_flow (void)
{
	MlSession s;
	MlResults r;

	ml_session_init (&s);
	ml_results_init (&r);

	/* the acceptance session of the milestone */
	add (&s, "a = 3");
	add (&s, "b = a^2 + 1");
	add (&s, "f(x) = 2x + b");
	add (&s, "f(a)");
	add (&s, "1/3 + 1/6");
	add (&s, "sqrt(2)");
	add (&s, "10!");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (r.lines.len == 7);
	CHECK (is_value (&r, 0, "3"));
	CHECK (is_value (&r, 1, "10"));
	CHECK (is_none (&r, 2));
	CHECK (is_value (&r, 3, "16"));
	CHECK (is_value (&r, 4, "1/2"));
	CHECK (is_value (&r, 5, "\xE2\x89\x88 1.41421356"));
	CHECK (is_value (&r, 6, "3628800"));

	/* a view of a line that does not exist is empty */
	CHECK (ml_results_get (&r, 7).kind == ML_RESULT_NONE);

	ml_results_free (&r);
	ml_session_free (&s);
}

static void test_other_lines (void)
{
	MlSession s;
	MlResults r;

	ml_session_init (&s);
	ml_results_init (&r);

	CHECK (
		ml_session_append (
			&s,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			lit ("1 + 1")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_BLANK,
			ML_DOMAIN_ALGEBRA,
			lit ("")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_MATH,
			ML_DOMAIN_PGA2,
			lit ("1 + 1")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_UNKNOWN,
			ML_DOMAIN_ALGEBRA,
			lit ("1 + 1")
		) == ML_OK
	);
	add (&s, "");
	add (&s, "1 + 1");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (r.lines.len == 6);

	for (uint64_t i = 0; i < 5; i++)
	{
		CHECK (is_none (&r, i));
	}

	CHECK (is_value (&r, 5, "2"));

	ml_results_free (&r);
	ml_session_free (&s);
}

static void test_errors (void)
{
	MlSession    s;
	MlResults    r;
	MlResultView v;

	ml_session_init (&s);
	ml_results_init (&r);

	add (&s, "1/0");
	add (&s, "2 +");
	add (&s, "y + 1");
	add (&s, "0^0");
	add (&s, "1 + 1");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (is_error (&r, 0, "division by zero"));
	CHECK (ml_results_get (&r, 1).kind == ML_RESULT_ERROR);
	CHECK (is_error (&r, 2, "undefined name 'y'"));
	CHECK (is_error (&r, 3, "0 to the power 0 is undefined"));
	CHECK (is_value (&r, 4, "2"));

	/* the byte range of the error is in the line */
	v = ml_results_get (&r, 2);
	CHECK (v.err_start == 0 && v.err_len == 1);

	/* a failed definition defines nothing */
	add (&s, "z = 1/0");
	add (&s, "z");
	CHECK (ml_results_update (&r, &s, 5) == ML_OK);
	CHECK (is_error (&r, 5, "division by zero"));
	CHECK (is_error (&r, 6, "undefined name 'z'"));

	ml_results_free (&r);
	ml_session_free (&s);
}

static void test_edit (void)
{
	MlSession s;
	MlResults r;

	ml_session_init (&s);
	ml_results_init (&r);

	add (&s, "a = 2");
	add (&s, "b = a + 1");
	add (&s, "b * 10");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (is_value (&r, 2, "30"));

	/* an edit changes the lines below it, from the changed line down */
	CHECK (
		ml_session_replace (
			&s,
			0,
			ML_LINE_MATH,
			ML_DOMAIN_ALGEBRA,
			lit ("a = 5")
		) == ML_OK
	);
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (r.lines.len == 3);
	CHECK (is_value (&r, 0, "5"));
	CHECK (is_value (&r, 1, "6"));
	CHECK (is_value (&r, 2, "60"));

	/* the lines above the change are kept */
	CHECK (
		ml_session_replace (
			&s,
			1,
			ML_LINE_MATH,
			ML_DOMAIN_ALGEBRA,
			lit ("b = a * 2")
		) == ML_OK
	);
	CHECK (ml_results_update (&r, &s, 1) == ML_OK);
	CHECK (is_value (&r, 0, "5"));
	CHECK (is_value (&r, 1, "10"));
	CHECK (is_value (&r, 2, "100"));

	/* redefinition shadows, and the snapshots show each line's world */
	CHECK (
		ml_session_insert (
			&s,
			2,
			ML_LINE_MATH,
			ML_DOMAIN_ALGEBRA,
			lit ("a = 1")
		) == ML_OK
	);
	CHECK (ml_results_update (&r, &s, 2) == ML_OK);
	CHECK (r.lines.len == 4);
	CHECK (is_value (&r, 2, "1"));
	CHECK (is_value (&r, 3, "100"));
	CHECK (
		ml_results_get (&r, 3).snapshot
		> ml_results_get (&r, 2).snapshot
	);
	CHECK (ml_results_get (&r, 0).snapshot == r.env.library);

	/* deleting a line */
	CHECK (ml_session_delete (&s, 0) == ML_OK);
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (r.lines.len == 3);
	CHECK (is_error (&r, 0, "undefined name 'a'"));

	/* a from beyond the known lines starts at the end of them */
	add (&s, "7");
	CHECK (ml_results_update (&r, &s, 100) == ML_OK);
	CHECK (r.lines.len == 4);
	CHECK (is_value (&r, 3, "7"));

	ml_results_free (&r);
	ml_session_free (&s);
}

static void test_includes (void)
{
	MlSession s;
	MlResults r;
	MlSlice   consts = lit ("matlog_test_consts.tmp");
	MlSlice   outer  = lit ("matlog_test_outer.tmp");
	MlSlice   loop_a = lit ("matlog_test_loop_a.tmp");
	MlSlice   loop_b = lit ("matlog_test_loop_b.tmp");

	CHECK (
		ml_fs_write_file_atomic (
			consts,
			lit ("m:algebra k = 4\nm:algebra g(x) = x + k\n")
		) == ML_OK
	);
	CHECK (
		ml_fs_write_file_atomic (
			outer,
			lit ("include \"matlog_test_consts.tmp\"\nm:algebra h = k * 2\n")
		) == ML_OK
	);
	CHECK (
		ml_fs_write_file_atomic (
			loop_a,
			lit ("include \"matlog_test_loop_b.tmp\"\n")
		) == ML_OK
	);
	CHECK (
		ml_fs_write_file_atomic (
			loop_b,
			lit ("include \"matlog_test_loop_a.tmp\"\n")
		) == ML_OK
	);

	ml_session_init (&s);
	ml_results_init (&r);

	/* definitions of an included file flow into the lines below, also
	   through a second level of includes */
	add_include (&s, "matlog_test_outer.tmp");
	add (&s, "g(1)");
	add (&s, "h");
	add_include (&s, "matlog_test_missing.tmp");
	add (&s, "k + 1");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (r.lines.len == 5);
	CHECK (is_none (&r, 0));
	CHECK (is_value (&r, 1, "5"));
	CHECK (is_value (&r, 2, "8"));
	CHECK (is_error (&r, 3, "included file not found"));
	CHECK (is_value (&r, 4, "5"));

	/* a cycle is reported on the include line and does not hang */
	ml_results_free (&r);
	ml_session_free (&s);
	ml_session_init (&s);
	ml_results_init (&r);
	add_include (&s, "matlog_test_loop_a.tmp");
	add (&s, "1 + 1");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (is_error (&r, 0, "include cycle"));
	CHECK (is_value (&r, 1, "2"));

	/* a file that includes itself */
	CHECK (
		ml_fs_write_file_atomic (
			loop_a,
			lit ("include \"matlog_test_loop_a.tmp\"\nm:algebra q = 1\n")
		) == ML_OK
	);
	ml_results_free (&r);
	ml_session_free (&s);
	ml_session_init (&s);
	ml_results_init (&r);
	add_include (&s, "matlog_test_loop_a.tmp");
	add (&s, "q");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (is_value (&r, 1, "1"));

	/* the session itself, loaded from a file, cannot include itself */
	CHECK (ml_session_set_path (&s, loop_a) == ML_OK);
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (is_error (&r, 0, "include cycle"));

	ml_results_free (&r);
	ml_session_free (&s);
	CHECK (ml_fs_remove (consts) == ML_OK);
	CHECK (ml_fs_remove (outer) == ML_OK);
	CHECK (ml_fs_remove (loop_a) == ML_OK);
	CHECK (ml_fs_remove (loop_b) == ML_OK);
}

static void test_blocks (void)
{
	MlSession    s;
	MlResults    r;
	MlResultView v;

	ml_session_init (&s);
	ml_results_init (&r);
	add (&s, "A = [[1,2],[3,4]]");
	add (&s, "2 + 2");
	add (&s, "A^-1");
	add (&s, "[1,2,3]");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);

	/* a matrix has its text and the typeset rows, a number only text */
	CHECK (is_value (&r, 0, "[[1, 2], [3, 4]]"));
	v = ml_results_get (&r, 0);
	CHECK (
		ml_slice_equal (
			v.block,
			lit (
				"\xE2\x8E\xA1 1 2 \xE2\x8E\xA4\n"
				"\xE2\x8E\xA3 3 4 \xE2\x8E\xA6"
			)
		)
	);
	CHECK (is_value (&r, 1, "4"));
	CHECK (ml_results_get (&r, 1).block.len == 0);
	CHECK (is_value (&r, 2, "[[-2, 1], [3/2, -1/2]]"));
	CHECK (ml_results_get (&r, 2).block.len > 0);
	CHECK (is_value (&r, 3, "[[1], [2], [3]]"));

	/* an error has no block, and lines that do not exist none either */
	add (&s, "[[1,2]] + [[1]]");
	CHECK (ml_results_update (&r, &s, 4) == ML_OK);
	CHECK (is_error (&r, 4, "matrix sizes do not fit"));
	CHECK (ml_results_get (&r, 4).block.len == 0);
	CHECK (ml_results_get (&r, 99).block.len == 0);

	ml_results_free (&r);
	ml_session_free (&s);
}

static void add_bool (MlSession *s, const char *text)
{
	CHECK (
		ml_session_append (
			s,
			ML_LINE_MATH,
			ML_DOMAIN_BOOLEAN,
			lit (text)
		) == ML_OK
	);
}

static void test_boolean (void)
{
	MlSession s;
	MlResults r;

	ml_session_init (&s);
	ml_results_init (&r);
	add_bool (&s, "p = a & b");
	add_bool (&s, "p | ~a");
	add_bool (&s, "table p");
	add_bool (&s, "a &");
	add (&s, "1 + 1");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);

	/* definitions flow down, a table is a block, an error has its range */
	CHECK (is_value (&r, 0, "a \xE2\x88\xA7 b"));
	CHECK (is_value (&r, 1, "\xC2\xAC" "a \xE2\x88\xA8 b"));
	CHECK (is_value (&r, 2, "a \xE2\x88\xA7 b"));
	CHECK (ml_results_get (&r, 2).block.len > 0);
	CHECK (ml_results_get (&r, 3).kind == ML_RESULT_ERROR);
	CHECK (
		ml_slice_equal (
			ml_results_get (&r, 3).code,
			lit ("bool_expected_operand")
		)
	);
	CHECK (is_value (&r, 4, "2"));

	/* the algebra lines do not see the boolean names */
	add (&s, "p");
	CHECK (ml_results_update (&r, &s, 5) == ML_OK);
	CHECK (ml_results_get (&r, 5).kind == ML_RESULT_ERROR);

	/* editing the definition changes the lines below it */
	CHECK (
		ml_session_replace (
			&s,
			0,
			ML_LINE_MATH,
			ML_DOMAIN_BOOLEAN,
			lit ("p = a | b")
		) == ML_OK
	);
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (is_value (&r, 1, "true"));

	ml_results_free (&r);
	ml_session_free (&s);
}

/*
 * A symbolic value kept in a variable must not point into the memory of the
 * line that made it: that is reused by the lines below.
 */
static void test_symbolic_kept (void)
{
	MlSession s;
	MlResults r;

	ml_session_init (&s);
	ml_results_init (&r);
	add (&s, "fp = diff(x^3, x)");
	add (&s, "f(x) = x");
	add (&s, "f(1)");
	add (&s, "fp");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (is_value (&r, 2, "1"));
	CHECK (is_value (&r, 3, "3x\xC2\xB2"));

	/* a longer number in a variable, and lines that use much memory */
	add (&s, "g = diff(x^7, x) * 123456789012345678901234567890");
	add (&s, "7^300 + 100!");
	add (&s, "g");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	CHECK (is_value (&r, 3, "3x\xC2\xB2"));
	CHECK (is_value (&r, 6, "864197523086419752308641975230x\xE2\x81\xB6"));

	ml_results_free (&r);
	ml_session_free (&s);
}

void run_results_tests (void);

void run_results_tests (void)
{
	test_symbolic_kept ();
	test_boolean ();
	test_blocks ();
	test_flow ();
	test_other_lines ();
	test_errors ();
	test_edit ();
	test_includes ();
}
