#include "test.h"

#include <meth/session/session.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static void test_domains (void)
{
	MlDomainResult r;

	CHECK (strcmp (ml_domain_name (ML_DOMAIN_PGA2), "pga2") == 0);
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
			ML_DOMAIN_PGA2,
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
	CHECK (v.kind == ML_LINE_MATH && v.domain == ML_DOMAIN_PGA2);
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
			lit ("consts.meth")
		) == ML_OK
	);
	CHECK (s.lines.len == 5);
	CHECK (ml_session_get (&s, 0).kind == ML_LINE_INCLUDE);
	CHECK (ml_slice_equal (ml_session_get (&s, 0).text, lit ("consts.meth")));
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
	CHECK (ml_session_set_path (&s, lit ("physics.meth")) == ML_OK);
	CHECK (ml_slice_equal      (ml_str_view (s.path), lit ("physics.meth")));
	CHECK (ml_session_set_path (&s, lit ("other.meth")) == ML_OK);
	CHECK (ml_slice_equal      (ml_str_view (s.path), lit ("other.meth")));

	CHECK (s.domain == ML_DOMAIN_ALGEBRA);
	ml_session_set_domain (&s, ML_DOMAIN_PGA2);
	CHECK (s.domain == ML_DOMAIN_PGA2);

	ml_session_free (&s);
}

/* The text of every line, "a|b|c". */
static int32_t lines_are (const MlSession *s, const char *expected)
{
	MlStr   text;
	int32_t same;

	ml_str_init (&text);

	for (uint64_t i = 0; i < s->lines.len; i++)
	{
		if (i > 0)
		{
			ml_str_append (&text, lit ("|"));
		}

		ml_str_append (&text, ml_session_get (s, i).text);
	}

	same = ml_slice_equal (ml_str_view (text), lit (expected));

	if (!same)
	{
		printf (
			"  lines are \"%s\", expected \"%s\"\n",
			ml_str_cstr (text),
			expected
		);
	}

	ml_str_free (&text);
	return same;
}

static void fill (
	MlSession  *s,
	const char *a,
	const char *b,
	const char *c,
	const char *d
)
{
	const char *list [4] = { a, b, c, d };

	for (uint64_t i = 0; i < 4 && list [i] != NULL; i++)
	{
		CHECK (
			ml_session_append (
				s,
				ML_LINE_MATH,
				ML_DOMAIN_ALGEBRA,
				lit (list [i])
			) == ML_OK
		);
	}
}

static void test_move (void)
{
	MlSession s;

	ml_session_init (&s);
	fill (&s, "a", "b", "c", "d");

	/* one line down, one up, a block both ways */
	CHECK (ml_session_move (&s, 0, 0, 1) == ML_OK);
	CHECK (lines_are (&s, "b|a|c|d"));
	CHECK (ml_session_move (&s, 3, 3, 0) == ML_OK);
	CHECK (lines_are (&s, "d|b|a|c"));
	CHECK (ml_session_move (&s, 1, 2, 2) == ML_OK);
	CHECK (lines_are (&s, "d|c|b|a"));
	CHECK (ml_session_move (&s, 2, 3, 0) == ML_OK);
	CHECK (lines_are (&s, "b|a|d|c"));

	/* no move, and moves that do not fit */
	CHECK (ml_session_move (&s, 1, 1, 1) == ML_OK);
	CHECK (lines_are (&s, "b|a|d|c"));
	CHECK (ml_session_move (&s, 0, 1, 3) == ML_ERR_RANGE);
	CHECK (ml_session_move (&s, 2, 1, 0) == ML_ERR_RANGE);
	CHECK (ml_session_move (&s, 0, 9, 0) == ML_ERR_RANGE);
	CHECK (lines_are (&s, "b|a|d|c"));

	ml_session_free (&s);
}

static void test_undo_redo (void)
{
	MlSession    s;
	MlUndoResult u;

	ml_session_init (&s);

	/* nothing to undo yet */
	u = ml_session_undo (&s);
	CHECK (u.result == ML_OK && !u.did);
	u = ml_session_redo (&s);
	CHECK (u.result == ML_OK && !u.did);

	/* one group per checkpoint */
	ml_session_checkpoint (&s);
	fill (&s, "a", NULL, NULL, NULL);
	ml_session_checkpoint (&s);
	fill (&s, "b", NULL, NULL, NULL);
	ml_session_checkpoint (&s);
	CHECK (
		ml_session_replace (
			&s,
			0,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			lit ("A")
		) == ML_OK
	);
	ml_session_checkpoint (&s);
	CHECK (ml_session_delete (&s, 0) == ML_OK);
	CHECK (lines_are (&s, "b"));

	/* undo walks back through delete, replace, inserts */
	u = ml_session_undo (&s);
	CHECK (u.did && lines_are (&s, "A|b"));
	CHECK (ml_session_get (&s, 0).kind == ML_LINE_NOTE);
	u = ml_session_undo (&s);
	CHECK (u.did && lines_are (&s, "a|b"));
	CHECK (ml_session_get (&s, 0).kind == ML_LINE_MATH);
	u = ml_session_undo (&s);
	CHECK (u.did && lines_are (&s, "a"));
	u = ml_session_undo (&s);
	CHECK (u.did && lines_are (&s, ""));
	u = ml_session_undo (&s);
	CHECK (!u.did);

	/* and redo walks forward again */
	u = ml_session_redo (&s);
	CHECK (u.did && lines_are (&s, "a"));
	u = ml_session_redo (&s);
	CHECK (u.did && lines_are (&s, "a|b"));
	u = ml_session_redo (&s);
	CHECK (u.did && lines_are (&s, "A|b"));
	u = ml_session_redo (&s);
	CHECK (u.did && lines_are (&s, "b"));
	u = ml_session_redo (&s);
	CHECK (!u.did);

	/* a new change empties the redo stack */
	u = ml_session_undo (&s);
	CHECK (u.did && lines_are (&s, "A|b"));
	ml_session_checkpoint (&s);
	fill (&s, "c", NULL, NULL, NULL);
	u = ml_session_redo (&s);
	CHECK (!u.did);
	CHECK (lines_are (&s, "A|b|c"));

	ml_session_free (&s);
}

static void test_undo_groups (void)
{
	MlSession    s;
	MlUndoResult u;

	ml_session_init (&s);
	fill (&s, "a", "b", "c", "d");
	ml_session_clear_history (&s);

	/* several changes in one group are taken back together */
	ml_session_checkpoint (&s);
	CHECK (ml_session_delete (&s, 1) == ML_OK);
	CHECK (ml_session_delete (&s, 1) == ML_OK);
	CHECK (ml_session_move (&s, 1, 1, 0) == ML_OK);
	CHECK (lines_are (&s, "d|a"));
	u = ml_session_undo (&s);
	CHECK (u.did && lines_are (&s, "a|b|c|d"));
	u = ml_session_redo (&s);
	CHECK (u.did && lines_are (&s, "d|a"));
	u = ml_session_undo (&s);
	CHECK (u.did && lines_are (&s, "a|b|c|d"));
	CHECK (s.dirty);

	/* the history is dropped when asked */
	CHECK (ml_session_delete (&s, 0) == ML_OK);
	ml_session_clear_history (&s);
	u = ml_session_undo (&s);
	CHECK (!u.did);

	ml_session_free (&s);
}

static void test_undo_limit (void)
{
	MlSession s;

	ml_session_init (&s);

	/* the oldest groups are forgotten, the newest stay */
	for (uint64_t i = 0; i < ML_UNDO_MAX + 50; i++)
	{
		ml_session_checkpoint (&s);
		fill (&s, "x", NULL, NULL, NULL);
	}

	CHECK (s.undo.len <= ML_UNDO_MAX);

	for (uint64_t i = 0; i < ML_UNDO_MAX; i++)
	{
		CHECK (ml_session_undo (&s).did);
	}

	CHECK (!ml_session_undo (&s).did);
	CHECK (s.lines.len == 50);
	ml_session_free (&s);
}

static void add_line (MlSession *s, MlLineKind kind, MlDomain d, const char *t)
{
	CHECK (ml_session_append (s, kind, d, lit (t)) == ML_OK);
}

static void test_blocks (void)
{
	MlSession s;
	MlBlock   b;

	ml_session_init (&s);
	b = ml_session_block (&s, 0);
	CHECK (b.kind == ML_BLOCK_NONE);

	/* note, two math lines, a matrix line, a blank, two notes, an include */
	add_line (&s, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "intro");
	add_line (&s, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "a = 1");
	add_line (&s, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "b = 2");
	add_line (&s, ML_LINE_MATH, ML_DOMAIN_PGA2, "A = 1");
	add_line (&s, ML_LINE_BLANK, ML_DOMAIN_ALGEBRA, "");
	add_line (&s, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "more");
	add_line (&s, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "text");
	add_line (&s, ML_LINE_INCLUDE, ML_DOMAIN_ALGEBRA, "x.meth");
	add_line (&s, ML_LINE_BLANK, ML_DOMAIN_ALGEBRA, "");
	add_line (&s, ML_LINE_BLANK, ML_DOMAIN_ALGEBRA, "");

	b = ml_session_block (&s, 0);
	CHECK (b.kind == ML_BLOCK_TEXT && b.first == 0 && b.last == 0);
	b = ml_session_block (&s, 2);
	CHECK (b.kind == ML_BLOCK_CODE && b.first == 1 && b.last == 2);
	CHECK (b.domain == ML_DOMAIN_ALGEBRA);
	b = ml_session_block (&s, 3);
	CHECK (b.kind == ML_BLOCK_CODE && b.first == 3 && b.last == 3);
	CHECK (b.domain == ML_DOMAIN_PGA2);
	b = ml_session_block (&s, 4);
	CHECK (b.kind == ML_BLOCK_BLANK && b.first == 4 && b.last == 4);
	b = ml_session_block (&s, 6);
	CHECK (b.kind == ML_BLOCK_TEXT && b.first == 5 && b.last == 6);
	b = ml_session_block (&s, 7);
	CHECK (b.kind == ML_BLOCK_INCLUDE && b.first == 7 && b.last == 7);

	/* every blank line is a block of its own */
	b = ml_session_block (&s, 8);
	CHECK (b.kind == ML_BLOCK_BLANK && b.first == 8 && b.last == 8);
	b = ml_session_block (&s, 9);
	CHECK (b.first == 9 && b.last == 9);
	CHECK (ml_session_block (&s, 10).kind == ML_BLOCK_NONE);

	/* a fold belongs to the first line of the block, is no change */
	CHECK (ml_session_block (&s, 2).folded == 0);
	ml_session_clear_history (&s);
	s.dirty = 0;
	ml_session_set_folded (&s, 1, 1);
	CHECK (ml_session_block (&s, 2).folded == 1);
	CHECK (ml_session_block (&s, 1).folded == 1);
	CHECK (ml_session_get (&s, 1).folded == 1);
	CHECK (ml_session_get (&s, 2).folded == 0);
	CHECK (!s.dirty && !ml_session_undo (&s).did);
	ml_session_set_folded (&s, 99, 1);
	ml_session_set_folded (&s, 1, 0);
	CHECK (ml_session_block (&s, 1).folded == 0);

	/* blocks follow the lines: a blank line splits, a deleted one joins */
	CHECK (
		ml_session_insert (
			&s,
			2,
			ML_LINE_BLANK,
			ML_DOMAIN_ALGEBRA,
			lit ("")
		) == ML_OK
	);
	b = ml_session_block (&s, 1);
	CHECK (b.first == 1 && b.last == 1);
	b = ml_session_block (&s, 3);
	CHECK (b.first == 3 && b.last == 3);
	CHECK (ml_session_delete (&s, 2) == ML_OK);
	b = ml_session_block (&s, 1);
	CHECK (b.first == 1 && b.last == 2);

	ml_session_free (&s);
}

void run_session_tests (void)
{
	test_domains ();
	test_edit ();
	test_cursor ();
	test_misc ();
	test_move ();
	test_undo_redo ();
	test_undo_groups ();
	test_undo_limit ();
	test_blocks ();
}
