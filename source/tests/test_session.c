#include "test.h"

#include <matlog/session/session.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static void test_domains (void)
{
	MlDomainResult r;

	CHECK (strcmp (ml_domain_name (ML_DOMAIN_MATRIX), "matrix") == 0);
	CHECK (strcmp (ml_domain_name (ML_DOMAIN_PGA3), "pga3") == 0);
	CHECK (strcmp (ml_domain_name ((MlDomain)99), "unknown") == 0);

	/* every name parses back to its domain */
	for (uint32_t i = 0; i < ML_DOMAIN_COUNT; i++)
	{
		r = ml_domain_from_name (lit (ml_domain_name ((MlDomain)i)));
		CHECK (r.result == ML_OK && r.domain == (MlDomain)i);
	}

	r = ml_domain_from_name (lit ("nonsense"));
	CHECK (r.result == ML_ERR_PARSE);
	r = ml_domain_from_name (lit (""));
	CHECK (r.result == ML_ERR_PARSE);
	r = ml_domain_from_name (lit ("matri"));
	CHECK (r.result == ML_ERR_PARSE);
}

static void test_edit (void)
{
	MlSession  s;
	MlLineView v;

	ml_session_init (&s);
	CHECK (s.lines.len == 0 && !s.dirty);
	CHECK (ml_session_get (&s, 0).kind == ML_LINE_NONE);

	CHECK (
		ml_session_append (
			&s,
			ML_LINE_MATH,
			ML_DOMAIN_MATRIX,
			lit ("A = [[1,2],[3,4]]")
		) == ML_OK
	);
	CHECK (s.dirty);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			lit ("a note")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_MATH,
			ML_DOMAIN_LAMBDA,
			lit ("\xCE\xBB" "x.x")
		) == ML_OK
	);
	CHECK (s.lines.len == 3);

	v = ml_session_get (&s, 0);
	CHECK (v.kind == ML_LINE_MATH && v.domain == ML_DOMAIN_MATRIX);
	CHECK (ml_slice_equal (v.text, lit ("A = [[1,2],[3,4]]")));
	v = ml_session_get (&s, 2);
	CHECK (v.kind == ML_LINE_MATH && v.domain == ML_DOMAIN_LAMBDA);
	CHECK (ml_slice_equal (v.text, lit ("\xCE\xBB" "x.x")));

	/* insert in the middle and at the front */
	CHECK (
		ml_session_insert (
			&s,
			1,
			ML_LINE_BLANK,
			ML_DOMAIN_ALGEBRA,
			lit ("")
		) == ML_OK
	);
	CHECK (
		ml_session_insert (
			&s,
			0,
			ML_LINE_INCLUDE,
			ML_DOMAIN_ALGEBRA,
			lit ("consts.mlog")
		) == ML_OK
	);
	CHECK (s.lines.len == 5);
	CHECK (ml_session_get (&s, 0).kind == ML_LINE_INCLUDE);
	CHECK (ml_slice_equal (ml_session_get (&s, 0).text, lit ("consts.mlog")));
	CHECK (ml_session_get (&s, 1).kind == ML_LINE_MATH);
	CHECK (ml_session_get (&s, 2).kind == ML_LINE_BLANK);
	CHECK (ml_session_get (&s, 3).kind == ML_LINE_NOTE);

	/* bad arguments leave the session alone */
	CHECK (
		ml_session_insert (
			&s,
			99,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			lit ("x")
		) == ML_ERR_RANGE
	);
	CHECK (
		ml_session_insert (
			&s,
			0,
			ML_LINE_NONE,
			ML_DOMAIN_ALGEBRA,
			lit ("x")
		) == ML_ERR_INVALID
	);
	CHECK (
		ml_session_insert (
			&s,
			0,
			ML_LINE_NOTE,
			(MlDomain)99,
			lit ("x")
		) == ML_ERR_INVALID
	);
	CHECK (s.lines.len == 5);

	/* replace */
	CHECK (
		ml_session_replace (
			&s,
			3,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			lit ("changed")
		) == ML_OK
	);
	CHECK (ml_slice_equal (ml_session_get (&s, 3).text, lit ("changed")));
	CHECK (
		ml_session_replace (
			&s,
			5,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			lit ("x")
		) == ML_ERR_RANGE
	);
	CHECK (
		ml_session_replace (
			&s,
			3,
			ML_LINE_NONE,
			ML_DOMAIN_ALGEBRA,
			lit ("x")
		) == ML_ERR_INVALID
	);
	CHECK (ml_slice_equal (ml_session_get (&s, 3).text, lit ("changed")));

	/* delete */
	CHECK (ml_session_delete (&s, 0) == ML_OK);
	CHECK (s.lines.len == 4 && ml_session_get (&s, 0).kind == ML_LINE_MATH);
	CHECK (ml_session_delete (&s, 4) == ML_ERR_RANGE);

	ml_session_free (&s);
	CHECK (s.lines.len == 0);
}

static void test_cursor (void)
{
	MlSession s;

	ml_session_init (&s);

	/* empty session: cursor stays at 0 */
	ml_session_set_cursor (&s, 5);
	CHECK (s.cursor == 0);
	ml_session_move_cursor (&s, 3);
	CHECK (s.cursor == 0);

	for (uint32_t i = 0; i < 5; i++)
	{
		CHECK (
			ml_session_append (
				&s,
				ML_LINE_NOTE,
				ML_DOMAIN_ALGEBRA,
				lit ("n")
			) == ML_OK
		);
	}

	ml_session_set_cursor (&s, 2);
	CHECK (s.cursor == 2);
	ml_session_set_cursor (&s, 100);
	CHECK (s.cursor == 4);

	ml_session_move_cursor (&s, -1);
	CHECK (s.cursor == 3);
	ml_session_move_cursor (&s, -100);
	CHECK (s.cursor == 0);
	ml_session_move_cursor (&s, 2);
	CHECK (s.cursor == 2);
	ml_session_move_cursor (&s, 100);
	CHECK (s.cursor == 4);
	ml_session_move_cursor (&s, INT64_MIN);
	CHECK (s.cursor == 0);
	ml_session_move_cursor (&s, INT64_MAX);
	CHECK (s.cursor == 4);

	/* deleting the last line pulls the cursor back */
	CHECK (ml_session_delete (&s, 4) == ML_OK);
	CHECK (s.cursor == 3);

	/* deleting everything */
	while (s.lines.len > 0)
	{
		CHECK (ml_session_delete (&s, 0) == ML_OK);
	}

	CHECK (s.cursor == 0);
	ml_session_free (&s);
}

static void test_misc (void)
{
	MlSession s;

	ml_session_init (&s);
	CHECK (s.path.len == 0);
	CHECK (ml_session_set_path (&s, lit ("physics.mlog")) == ML_OK);
	CHECK (ml_slice_equal      (ml_str_view (s.path), lit ("physics.mlog")));
	CHECK (ml_session_set_path (&s, lit ("other.mlog")) == ML_OK);
	CHECK (ml_slice_equal      (ml_str_view (s.path), lit ("other.mlog")));

	CHECK (s.domain == ML_DOMAIN_ALGEBRA);
	ml_session_set_domain (&s, ML_DOMAIN_PGA2);
	CHECK (s.domain == ML_DOMAIN_PGA2);

	ml_session_free (&s);
}

void run_session_tests (void)
{
	test_domains ();
	test_edit ();
	test_cursor ();
	test_misc ();
}
