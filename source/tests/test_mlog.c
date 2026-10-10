#include "test.h"

#include <meth/platform/fs.h>
#include <meth/session/mlog.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* Parses text into a fresh session and formats it again. */
static MlResult round_trip (MlStr *out, MlSlice text)
{
	MlSession s;
	MlResult  r;

	ml_session_init (&s);
	r = ml_mlog_parse (&s, text);

	if (r == ML_OK)
	{
		r = ml_mlog_format (out, &s);
	}

	ml_session_free (&s);
	return r;
}

static void test_parse (void)
{
	MlSession  s;
	MlLineView v;

	ml_session_init (&s);

	CHECK (ml_mlog_parse (
		&s,
		lit (
			"# a note\n"
			"m:pga2 A = [[1,2],[3,4]]\n"
			"include \"consts.meth\"\n"
			"\n"
			"m:lambda \xCE\xBB" "x.x\n"
		)
	) == ML_OK);
	CHECK (s.lines.len == 5);

	v = ml_session_get (&s, 0);
	CHECK (v.kind == ML_LINE_NOTE && ml_slice_equal (v.text, lit ("a note")));
	v = ml_session_get (&s, 1);
	CHECK (v.kind == ML_LINE_MATH && v.domain == ML_DOMAIN_PGA2);
	CHECK (ml_slice_equal (v.text, lit ("A = [[1,2],[3,4]]")));
	v = ml_session_get (&s, 2);
	CHECK (
		v.kind == ML_LINE_INCLUDE && ml_slice_equal (
			v.text,
			lit ("consts.meth")
		)
	);
	v = ml_session_get (&s, 3);
	CHECK (v.kind == ML_LINE_BLANK && v.text.len == 0);
	v = ml_session_get (&s, 4);
	CHECK (v.kind == ML_LINE_MATH && v.domain == ML_DOMAIN_LAMBDA);
	CHECK (ml_slice_equal (v.text, lit ("\xCE\xBB" "x.x")));

	ml_session_free (&s);
}

static void test_line_endings (void)
{
	MlSession s;

	/* CRLF, a missing final newline and empty input */
	ml_session_init (&s);
	CHECK (ml_mlog_parse (&s, lit ("# one\r\n# two\r\nm:algebra x")) == ML_OK);
	CHECK (s.lines.len == 3);
	CHECK (ml_slice_equal (ml_session_get (&s, 0).text, lit ("one")));
	CHECK (ml_slice_equal (ml_session_get (&s, 2).text, lit ("x")));
	ml_session_free (&s);

	/* byte order marks are dropped, however many */
	ml_session_init (&s);
	CHECK (ml_mlog_parse (&s, lit ("\xEF\xBB\xBF\xEF\xBB\xBF# a")) == ML_OK);
	CHECK (s.lines.len == 1);
	CHECK (ml_slice_equal (ml_session_get (&s, 0).text, lit ("a")));
	ml_session_free (&s);

	/* a carriage return left over before the CRLF is dropped too */
	ml_session_init (&s);
	CHECK (ml_mlog_parse (&s, lit ("# a\r\r\n# b\r")) == ML_OK);
	CHECK (s.lines.len == 2);
	CHECK (ml_slice_equal (ml_session_get (&s, 0).text, lit ("a")));
	CHECK (ml_slice_equal (ml_session_get (&s, 1).text, lit ("b")));
	ml_session_free (&s);

	ml_session_init (&s);
	CHECK (ml_mlog_parse (&s, lit ("")) == ML_OK);
	CHECK (s.lines.len == 0);
	ml_session_free (&s);

	/* a trailing newline does not add a blank line, a second one does */
	ml_session_init (&s);
	CHECK (ml_mlog_parse (&s, lit ("# a\n")) == ML_OK && s.lines.len == 1);
	ml_session_free (&s);
	ml_session_init (&s);
	CHECK (ml_mlog_parse  (&s, lit ("# a\n\n")) == ML_OK && s.lines.len == 2);
	CHECK (ml_session_get (&s, 1).kind == ML_LINE_BLANK);
	ml_session_free (&s);

	/* byte order mark is skipped */
	ml_session_init (&s);
	CHECK (ml_mlog_parse (&s, lit ("\xEF\xBB\xBF# bom\n")) == ML_OK);
	CHECK (
		s.lines.len == 1 && ml_slice_equal (
			ml_session_get (&s, 0).text,
			lit ("bom")
		)
	);
	ml_session_free (&s);
}

static void test_unknown (void)
{
	MlSession  s;
	MlLineView v;

	ml_session_init (&s);

	CHECK (ml_mlog_parse (
		&s,
		lit (
			"hello world\n"
			"m:nonsense x\n"
			"m:\n"
			"include \"unterminated\n"
			"include \"\"\n"
			"  m:pga2 x\n"
		)
	) == ML_OK);
	CHECK (s.lines.len == 6);

	v = ml_session_get (&s, 0);
	CHECK (
		v.kind == ML_LINE_UNKNOWN && ml_slice_equal (
			v.text,
			lit ("hello world")
		)
	);
	v = ml_session_get (&s, 1);
	CHECK (
		v.kind == ML_LINE_UNKNOWN && ml_slice_equal (
			v.text,
			lit ("m:nonsense x")
		)
	);
	CHECK (ml_session_get (&s, 2).kind == ML_LINE_UNKNOWN);
	CHECK (ml_session_get (&s, 3).kind == ML_LINE_UNKNOWN);
	v = ml_session_get (&s, 4);
	CHECK (v.kind == ML_LINE_INCLUDE && v.text.len == 0);
	CHECK (ml_session_get (&s, 5).kind == ML_LINE_UNKNOWN);

	ml_session_free (&s);

	/* invalid UTF-8 is kept as raw text */
	ml_session_init (&s);
	CHECK (ml_mlog_parse (&s, ml_slice ("# bad \xFF\xFE\n", 9)) == ML_OK);
	v = ml_session_get (&s, 0);
	CHECK (v.kind == ML_LINE_UNKNOWN && v.text.len == 8);
	ml_session_free (&s);
}

static void test_note_and_math_edges (void)
{
	MlSession  s;
	MlLineView v;

	ml_session_init (&s);
	CHECK (
		ml_mlog_parse (
			&s,
			lit ("#\n#x\n#  indented\nm:pga2\nm:pga3  two spaces\n")
		) == ML_OK
	);

	v = ml_session_get (&s, 0);
	CHECK (v.kind == ML_LINE_NOTE && v.text.len == 0);
	v = ml_session_get (&s, 1);
	CHECK (v.kind == ML_LINE_NOTE && ml_slice_equal (v.text, lit ("x")));
	v = ml_session_get (&s, 2);
	CHECK (
		v.kind == ML_LINE_NOTE && ml_slice_equal (v.text, lit (" indented"))
	);
	v = ml_session_get (&s, 3);
	CHECK (
		(
			   v.kind == ML_LINE_MATH
			&& v.domain == ML_DOMAIN_PGA2
			&& v.text.len == 0
		)
	);
	v = ml_session_get (&s, 4);
	CHECK (
		v.kind == ML_LINE_MATH && v.domain == ML_DOMAIN_PGA3 && ml_slice_equal (
			v.text,
			lit (" two spaces")
		)
	);

	ml_session_free (&s);
}

static void test_format (void)
{
	MlSession s;
	MlStr     out;

	ml_session_init (&s);
	ml_str_init (&out);

	CHECK (
		ml_session_append (
			&s,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			lit ("note")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			lit ("")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_MATH,
			ML_DOMAIN_PGA3,
			lit ("v = (1, 2)")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_MATH,
			ML_DOMAIN_BOOLEAN,
			lit ("")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_INCLUDE,
			ML_DOMAIN_ALGEBRA,
			lit ("a/b.meth")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_BLANK,
			ML_DOMAIN_ALGEBRA,
			lit ("ignored")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_UNKNOWN,
			ML_DOMAIN_ALGEBRA,
			lit ("raw ??")
		) == ML_OK
	);

	CHECK (ml_mlog_format (&out, &s) == ML_OK);
	CHECK (strcmp (
		ml_str_cstr (out),
		"# note\n"
		"#\n"
		"m:pga3 v = (1, 2)\n"
		"m:boolean\n"
		"include \"a/b.meth\"\n"
		"\n"
		"raw ??\n"
	) == 0);

	ml_str_free (&out);
	ml_session_free (&s);
}

static void test_round_trip (void)
{
	MlStr      a;
	MlStr      b;
	const char *samples [] = {
		"# note\nm:pga2 A = [[1,2],[3,4]]\n\ninclude \"x.meth\"\n"
		"m:pga2 e12 * e1\n",
		"garbage line\nm:nonsense x\n# ok\n",
		"# \xCF\x80 and \xE2\x88\x9A" "x\nm:lambda \xCE\xBB" "x.x\n",
		"\n\n\n",
		"#  two spaces\n#\n"
	};

	for (uint32_t i = 0; i < ml_count (samples); i++)
	{
		ml_str_init (&a);
		ml_str_init (&b);

		/*
		 * the output is stable: parsing and formatting it again changes nothing
		 */
		CHECK (round_trip     (&a, lit (samples [i])) == ML_OK);
		CHECK (round_trip     (&b, ml_str_view (a)) == ML_OK);
		CHECK (ml_slice_equal (ml_str_view (a), ml_str_view (b)));
		CHECK (ml_slice_equal (ml_str_view (a), lit (samples [i])));

		ml_str_free (&a);
		ml_str_free (&b);
	}

	/*
	 * nonstandard input normalizes: CRLF, missing newline, whitespace only
	 * lines
	 */
	ml_str_init (&a);
	CHECK (round_trip (&a, lit ("# a\r\n   \r\nm:algebra x")) == ML_OK);
	CHECK (strcmp     (ml_str_cstr (a), "# a\n\nm:algebra x\n") == 0);
	ml_str_free (&a);
}

static void test_files (void)
{
	MlSlice   path = lit ("meth_test_session.tmp");
	MlSession s;
	MlSession other;
	MlStr     file;

	ml_str_init (&file);
	ml_session_init (&s);
	ml_fs_remove (path);

	/* saving needs a path */
	CHECK (ml_mlog_save (&s) == ML_ERR_INVALID);

	/* a missing file leaves the session as it is */
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			lit ("keep me")
		) == ML_OK
	);
	CHECK (ml_mlog_load (&s, path) == ML_ERR_IO);
	CHECK (
		s.lines.len == 1 && ml_slice_equal (
			ml_session_get (&s, 0).text,
			lit ("keep me")
		)
	);
	ml_session_free (&s);

	/* save then load */
	ml_session_init (&s);
	CHECK (ml_session_set_path (&s, path) == ML_OK);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_MATH,
			ML_DOMAIN_PGA2,
			lit ("A = [[1]]")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&s,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			lit ("hi \xCF\x80")
		) == ML_OK
	);
	CHECK (
		ml_session_append (&s, ML_LINE_MATH, ML_DOMAIN_PGA3, lit ("x")) == ML_OK
	);
	CHECK (s.dirty);
	CHECK (ml_mlog_save (&s) == ML_OK);
	CHECK (!s.dirty);

	ml_session_init (&other);
	CHECK (
		ml_session_append (
			&other,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			lit ("old")
		) == ML_OK
	);
	other.cursor = 0;
	CHECK (ml_mlog_load (&other, path) == ML_OK);
	CHECK (other.lines.len == 3 && !other.dirty && other.cursor == 0);
	CHECK (other.domain == ML_DOMAIN_PGA3);
	CHECK (ml_slice_equal (ml_str_view (other.path), path));
	CHECK (ml_session_get (&other, 0).domain == ML_DOMAIN_PGA2);
	CHECK (
		ml_slice_equal (ml_session_get (&other, 1).text, lit ("hi \xCF\x80"))
	);
	ml_session_free (&other);

	/*
	 * a file with lines we do not understand survives load and save unchanged
	 */
	CHECK (
		ml_fs_write_file_atomic (
			path,
			lit ("# fine\n??? weird\nm:nope x\n\xFF\xFE\n")
		) == ML_OK
	);
	ml_session_free (&s);
	ml_session_init (&s);
	CHECK (ml_mlog_load (&s, path) == ML_OK);
	CHECK (s.lines.len == 4);
	CHECK (ml_mlog_save    (&s) == ML_OK);
	CHECK (ml_fs_read_file (&file, path) == ML_OK);
	CHECK (
		ml_slice_equal (
			ml_str_view (file),
			lit ("# fine\n??? weird\nm:nope x\n\xFF\xFE\n")
		)
	);

	CHECK (ml_fs_remove (path) == ML_OK);
	ml_str_free (&file);
	ml_session_free (&s);
}

/* Files from before matrix, vector and equations joined algebra. */
static void test_old_domain_names (void)
{
	MlSession s;
	MlStr     out;

	ml_session_init (&s);
	ml_str_init (&out);
	CHECK (
		ml_mlog_parse (
			&s,
			lit ("m:matrix A = [[1,2],[3,4]]\nm:vector v = [1, 2]\n"
				"m:equations x + 1\nm:lambda x\n")
		) == ML_OK
	);
	CHECK (s.lines.len == 4);
	CHECK (ml_session_get (&s, 0).domain == ML_DOMAIN_ALGEBRA);
	CHECK (ml_session_get (&s, 1).domain == ML_DOMAIN_ALGEBRA);
	CHECK (ml_session_get (&s, 2).domain == ML_DOMAIN_ALGEBRA);
	CHECK (ml_session_get (&s, 3).domain == ML_DOMAIN_LAMBDA);
	CHECK (ml_session_get (&s, 0).kind == ML_LINE_MATH);

	/* and are written back as algebra */
	CHECK (ml_mlog_format (&out, &s) == ML_OK);
	CHECK (
		ml_slice_equal (
			ml_str_view (out),
			lit ("m:algebra A = [[1,2],[3,4]]\nm:algebra v = [1, 2]\n"
				"m:algebra x + 1\nm:lambda x\n")
		)
	);

	ml_str_free (&out);
	ml_session_free (&s);
}

void run_mlog_tests (void);

void run_mlog_tests (void)
{
	test_parse ();
	test_line_endings ();
	test_unknown ();
	test_note_and_math_edges ();
	test_format ();
	test_round_trip ();
	test_files ();
	test_old_domain_names ();
}
