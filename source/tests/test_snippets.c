#include "test.h"
#include "test_keys.h"

#include <meth/docs/snippets.h>
#include <meth/platform/fs.h>
#include <meth/session/results.h>

#include <stdlib.h>
#include <string.h>

#define CONFIG_DIR  "meth_test_config/snippets"
#define SNIPPET_DIR "meth_test_config/snippets/snippets"
#define MINE_FILE   "meth_test_config/snippets/snippets/mine.mlog"
#define QUAD_FILE \
	"meth_test_config/snippets/snippets/algebra-quadratic.mlog"

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static void use_test_config (void)
{
#ifdef _WIN32
	_putenv_s ("METH_CONFIG", CONFIG_DIR);
#else
	setenv ("METH_CONFIG", CONFIG_DIR, 1);
#endif
	ml_fs_remove (lit (MINE_FILE));
	ml_fs_remove (lit (QUAD_FILE));
}

static void test_builtin (void)
{
	CHECK (ml_snippet_count () >= 8);

	for (uint64_t i = 0; i < ml_snippet_count (); i++)
	{
		MlStr     summary;
		MlSession lines;
		MlResults r;
		uint64_t  math = 0;

		ml_str_init (&summary);
		ml_session_init (&lines);
		ml_results_init (&r);

		CHECK (ml_snippet_name_ok (ml_snippet_name (i)));
		CHECK (ml_snippet_summary (&summary, ml_snippet_text (i)) == ML_OK);
		CHECK (summary.len > 0 && summary.len <= 80);
		CHECK (ml_snippet_lines (&lines, ml_snippet_text (i)) == ML_OK);
		CHECK (ml_results_update (&r, &lines, 0) == ML_OK);

		/* the summary is not one of the lines, and every line works */
		for (uint64_t k = 0; k < lines.lines.len; k++)
		{
			MlLineView v = ml_session_get (&lines, k);

			CHECK (
				!(
					   v.kind == ML_LINE_NOTE
					&& ml_slice_equal (v.text, ml_str_view (summary))
				)
			);

			if (v.kind == ML_LINE_MATH)
			{
				math++;
				CHECK (ml_results_get (&r, k).kind != ML_RESULT_ERROR);
			}
		}

		CHECK (math >= 2);
		ml_results_free (&r);
		ml_session_free (&lines);
		ml_str_free (&summary);
	}
}

static void test_names (void)
{
	CHECK (ml_snippet_name_ok (lit ("linalg-linear-system")));
	CHECK (ml_snippet_name_ok (lit ("a_1")));
	CHECK (!ml_snippet_name_ok (lit ("")));
	CHECK (!ml_snippet_name_ok (lit ("Upper")));
	CHECK (!ml_snippet_name_ok (lit ("../x")));
	CHECK (!ml_snippet_name_ok (lit ("a b")));
	CHECK (!ml_snippet_name_ok (lit ("a.b")));
}

static void test_find (void)
{
	MlStr          text;
	MlSnippetFound f;

	use_test_config ();
	ml_str_init (&text);

	f = ml_snippet_find (&text, lit ("algebra-quadratic"));
	CHECK (f.result == ML_OK && f.found);
	CHECK (strstr (ml_str_cstr (text), "discriminant") != NULL);

	ml_str_clear (&text);
	f = ml_snippet_find (&text, lit ("no-such-snippet"));
	CHECK (f.result == ML_OK && !f.found && text.len == 0);

	/* a name that could leave the folder is never looked up */
	f = ml_snippet_find (&text, lit ("../../x"));
	CHECK (f.result == ML_OK && !f.found);

	/* the user's file is found, and hides the built in one */
	CHECK (ml_fs_make_dirs (lit (SNIPPET_DIR)) == ML_OK);
	CHECK (
		ml_fs_write_file_atomic (
			lit (MINE_FILE),
			lit ("# My own\nm:algebra 1 + 1\n")
		) == ML_OK
	);
	CHECK (
		ml_fs_write_file_atomic (
			lit (QUAD_FILE),
			lit ("# My quadratic\nm:algebra 2 + 2\n")
		) == ML_OK
	);
	f = ml_snippet_find (&text, lit ("mine"));
	CHECK (f.result == ML_OK && f.found);
	CHECK (strstr (ml_str_cstr (text), "My own") != NULL);
	ml_str_clear (&text);
	f = ml_snippet_find (&text, lit ("algebra-quadratic"));
	CHECK (strstr (ml_str_cstr (text), "My quadratic") != NULL);

	/* the page lists both */
	ml_str_clear (&text);
	CHECK (ml_snippets_page (&text) == ML_OK);
	CHECK (strstr (ml_str_cstr (text), "# Yours\n") != NULL);
	CHECK (strstr (ml_str_cstr (text), "- `mine`: My own\n") != NULL);
	CHECK (
		strstr (
			ml_str_cstr (text),
			"- `algebra-quadratic`: My quadratic\n"
		) != NULL
	);
	CHECK (
		strstr (
			ml_str_cstr (text),
			"# Linear algebra\n- `linalg-linear-system`: "
		) != NULL
	);

	ml_fs_remove (lit (MINE_FILE));
	ml_fs_remove (lit (QUAD_FILE));
	ml_str_free (&text);
}

static void command (MlEditor *ed, const char *text)
{
	test_normal (ed, text);
	test_key (ed, ML_KEY_ENTER, 0, 0);
}

static void test_editor (void)
{
	MlEditor ed;

	use_test_config ();
	ml_editor_init (&ed);

	command (&ed, ":snippet no-such-snippet");
	CHECK (
		strstr (ml_str_cstr (ed.message), "no snippet no-such-snippet") != NULL
	);
	CHECK (ed.session.lines.len == 0);

	/* the lines after the summary come into the session */
	command (&ed, ":snippet calculus-derivative");
	CHECK (ed.session.lines.len == 3);
	CHECK (ml_session_get (&ed.session, 0).kind == ML_LINE_MATH);
	CHECK (ml_session_get (&ed.session, 0).domain == ML_DOMAIN_ALGEBRA);
	CHECK (ml_editor_refresh (&ed) == ML_OK);
	CHECK (ml_results_get (&ed.results, 1).kind == ML_RESULT_VALUE);

	/* one change for undo */
	command (&ed, ":undo");
	CHECK (ed.session.lines.len == 0);

	/* alone it opens the page */
	command (&ed, ":snippet");
	CHECK (ed.state == ML_STATE_HELP);
	CHECK (ml_str_view (ed.help.docs.pages.data [ed.help.page].id).len == 8);

	ml_editor_free (&ed);
}

void run_snippets_tests (void);

void run_snippets_tests (void)
{
	test_builtin ();
	test_names ();
	test_find ();
	test_editor ();
}
