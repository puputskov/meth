#include "test.h"

#include <matlog/platform/fs.h>
#include <matlog/session/folds.h>

#include <stdlib.h>

#define CONFIG_DIR  "matlog_test_config/nested"
#define CONFIG_FILE "matlog_test_config/nested/settings.mlcfg"

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static void use_test_config (void)
{
#ifdef _WIN32
	_putenv_s ("MATLOG_CONFIG", CONFIG_DIR);
#else
	setenv ("MATLOG_CONFIG", CONFIG_DIR, 1);
#endif
}

/* A session of two blocks of several lines: lines 0..2 and 4..5. */
static void make_session (MlSession *s)
{
	ml_session_init (s);
	CHECK (ml_session_set_path (s, lit ("a.mlog")) == ML_OK);

	for (uint64_t i = 0; i < 3; i++)
	{
		CHECK (
			ml_session_append (
				s,
				ML_LINE_MATH,
				ML_DOMAIN_ALGEBRA,
				lit ("1+1")
			) == ML_OK
		);
	}

	CHECK (
		ml_session_append (s, ML_LINE_BLANK, ML_DOMAIN_ALGEBRA, lit ("")) == ML_OK
	);

	for (uint64_t i = 0; i < 2; i++)
	{
		CHECK (
			ml_session_append (
				s,
				ML_LINE_NOTE,
				ML_DOMAIN_ALGEBRA,
				lit ("text")
			) == ML_OK
		);
	}
}

static int32_t file_is (const char *expected)
{
	MlStr   text;
	int32_t same;

	ml_str_init (&text);
	same = (
		   ml_fs_read_file (&text, lit (CONFIG_FILE)) == ML_OK
		&& ml_slice_equal (ml_str_view (text), lit (expected))
	);

	if (!same)
	{
		printf ("  settings file: \"%s\"\n", ml_str_cstr (text));
	}

	ml_str_free (&text);
	return same;
}

static int32_t format_is (const MlSession *s, const char *expected)
{
	MlStr   text;
	int32_t same;

	ml_str_init (&text);
	same = (
		   ml_folds_format (&text, s) == ML_OK
		&& ml_slice_equal (ml_str_view (text), lit(expected))
	);
	ml_str_free (&text);
	return same;
}

static void test_folds (void)
{
	MlSession s;
	MlSession t;

	use_test_config ();
	ml_fs_remove (lit (CONFIG_FILE));

	make_session (&s);
	CHECK (format_is (&s, ""));
	ml_session_set_folded (&s, 0, 1);
	ml_session_set_folded (&s, 4, 1);
	CHECK (format_is (&s, "0 4"));

	/* no file yet: it is made, with the folder */
	CHECK (ml_folds_save (&s) == ML_OK);
	CHECK (file_is ("[folds]\na.mlog = 0 4\n"));

	/* the same folds in a new session of the same path */
	make_session (&t);
	CHECK (ml_folds_load (&t) == ML_OK);
	CHECK (format_is (&t, "0 4"));
	ml_session_free (&t);

	/* other content stays, the entry is replaced in its section */
	CHECK (
		ml_fs_write_file_atomic (
			lit (CONFIG_FILE),
			lit (
				"# my settings\n"
				"[editor]\n"
				"width = 1\n"
				"[folds]\n"
				"other.mlog = 2\n"
				"a.mlog = 9 9\n"
				"[keys]\n"
				"x = y\n"
			)
		) == ML_OK
	);
	CHECK (ml_folds_save (&s) == ML_OK);
	CHECK (
		file_is (
			"# my settings\n"
			"[editor]\n"
			"width = 1\n"
			"[folds]\n"
			"other.mlog = 2\n"
			"a.mlog = 0 4\n"
			"[keys]\n"
			"x = y\n"
		)
	);

	/* entries that do not start a block are ignored */
	CHECK (
		ml_fs_write_file_atomic (
			lit (CONFIG_FILE),
			lit ("[folds]\na.mlog = 1 99 x 4 3\n")
		) == ML_OK
	);
	make_session (&t);
	CHECK (ml_folds_load (&t) == ML_OK);
	CHECK (format_is (&t, "4"));
	ml_session_free (&t);

	/* nothing folded removes the entry, and an unchanged file is not written */
	ml_session_set_folded (&s, 0, 0);
	ml_session_set_folded (&s, 4, 0);
	CHECK (ml_folds_save (&s) == ML_OK);
	CHECK (file_is ("[folds]\n"));
	CHECK (ml_folds_save (&s) == ML_OK);
	CHECK (file_is ("[folds]\n"));

	ml_fs_remove (lit (CONFIG_FILE));
	ml_session_free (&s);
}

void run_folds_tests (void);

void run_folds_tests (void)
{
	test_folds ();
}
