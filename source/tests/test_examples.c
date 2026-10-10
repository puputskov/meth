#include "test.h"

#include "test_keys.h"

#include <matlog/cli/cli.h>
#include <matlog/docs/examples.h>
#include <matlog/session/mlog.h>
#include <matlog/session/results.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static void test_files (void)
{
	CHECK (ml_example_count () >= 10);

	for (uint64_t i = 0; i < ml_example_count (); i++)
	{
		MlStr     title;
		MlStr     summary;
		MlSession s;
		MlResults r;
		uint64_t  math = 0;
		uint64_t  bad  = 0;

		ml_str_init (&title);
		ml_str_init (&summary);
		ml_session_init (&s);
		ml_results_init (&r);

		/* a title and a one line summary, in a group of the contents */
		CHECK (ml_example_title (&title, i) == ML_OK);
		CHECK (ml_example_summary (&summary, i) == ML_OK);
		CHECK (title.len > 0 && title.len <= 60);
		CHECK (summary.len > 0);
		CHECK (ml_example_find (ml_example_name (i)) == i);

		if (title.len == 0 || summary.len == 0)
		{
			printf ("  example %s\n", ml_example_name (i).data);
		}

		/* every math line works: an example shows how, not what fails */
		CHECK (ml_mlog_parse (&s, ml_example_text (i)) == ML_OK);
		CHECK (ml_results_update (&r, &s, 0) == ML_OK);

		for (uint64_t k = 0; k < s.lines.len; k++)
		{
			MlLineView v = ml_session_get (&s, k);

			if (v.kind == ML_LINE_MATH)
			{
				MlResultView res = ml_results_get (&r, k);

				math++;

				if (res.kind == ML_RESULT_ERROR)
				{
					bad++;
					printf (
						"  %s line %d: %.*s: %.*s\n",
						ml_example_name (i).data,
						(int)(k + 1),
						(int)v.text.len,
						v.text.data,
						(int)res.text.len,
						res.text.data
					);
				}
			}

			CHECK (v.kind != ML_LINE_UNKNOWN);
		}

		CHECK (math >= 3);
		CHECK (bad == 0);

		ml_results_free (&r);
		ml_session_free (&s);
		ml_str_free (&summary);
		ml_str_free (&title);
	}
}

static void test_areas (void)
{
	/* every example is in one of the groups of the contents */
	MlStr contents;

	ml_str_init (&contents);
	CHECK (ml_examples_contents (&contents) == ML_OK);

	for (uint64_t i = 0; i < ml_example_count (); i++)
	{
		MlStr link;

		ml_str_init (&link);
		CHECK (
			ml_str_appendf (
				&link,
				"(ex-%.*s)",
				(int)ml_example_name (i).len,
				ml_example_name (i).data
			) == ML_OK
		);
		CHECK (strstr (ml_str_cstr (contents), ml_str_cstr (link)) != NULL);
		ml_str_free (&link);
	}

	ml_str_free (&contents);
}

static void test_pages (void)
{
	MlDocs   d;
	MlStr    text;
	uint64_t page = 0;
	uint64_t line = 0;

	ml_docs_init (&d);
	CHECK (ml_docs_load (&d, NULL, &page, &line) == ML_OK);
	ml_str_init (&text);

	/* the contents and every example are pages, searchable */
	CHECK (ml_docs_find (&d, lit ("examples")) < d.pages.len);

	for (uint64_t i = 0; i < ml_example_count (); i++)
	{
		MlStr id;

		ml_str_init (&id);
		CHECK (
			ml_str_appendf (
				&id,
				"ex-%.*s",
				(int)ml_example_name (i).len,
				ml_example_name (i).data
			) == ML_OK
		);
		CHECK (ml_docs_find (&d, ml_str_view (id)) < d.pages.len);
		ml_str_free (&id);
	}

	/* the results on the page are the ones of the evaluator */
	CHECK (
		ml_example_find (lit ("calculus-newton-sqrt2")) < ml_example_count ()
	);
	CHECK (
		ml_example_page (
			&text,
			ml_example_find (lit ("calculus-newton-sqrt2"))
		) == ML_OK
	);
	CHECK (
		strstr (ml_str_cstr (text), "x4 = g(x3)\n  = 665857/470832\n") != NULL
	);
	CHECK (strstr (ml_str_cstr (text), "```code\n") != NULL);

	/* the whole documentation, examples included, is consistent */
	{
		MlStr    report;
		uint64_t problems = 0;

		ml_str_init (&report);
		CHECK (ml_docs_check (&d, &report, &problems) == ML_OK);

		if (problems > 0)
		{
			printf ("%s", ml_str_cstr (report));
		}

		CHECK (problems == 0);
		ml_str_free (&report);
	}

	ml_str_free (&text);
	ml_docs_free (&d);
}

static void command (MlEditor *ed, const char *text)
{
	test_normal (ed, text);
	test_key (ed, ML_KEY_ENTER, 0, 0);
}

static void test_editor (void)
{
	MlEditor ed;
	uint64_t want = 0;

	ml_editor_init (&ed);
	command (&ed, ":example no-such-example");
	CHECK (
		strstr (ml_str_cstr (ed.message), "no example no-such-example") != NULL
	);
	CHECK (ed.session.lines.len == 0);

	/* the lines of the example come into the session, after the cursor */
	{
		MlSession ex;

		ml_session_init (&ex);
		CHECK (
			ml_mlog_parse (
				&ex,
				ml_example_text (
					ml_example_find (lit ("logic-minimise-circuit"))
				)
			) == ML_OK
		);
		want = ex.lines.len;
		ml_session_free (&ex);
	}

	command (&ed, ":example logic-minimise-circuit");
	CHECK (ed.session.lines.len == want);
	CHECK (ed.session.cursor == 0);
	CHECK (ml_session_get (&ed.session, 0).kind == ML_LINE_NOTE);
	CHECK (
		strstr (ml_str_cstr (ed.message), "lines of logic-minimise") != NULL
	);

	/* the results are computed, a boolean line has its value */
	CHECK (ml_editor_refresh (&ed) == ML_OK);
	CHECK (ml_results_get (&ed.results, 2).kind == ML_RESULT_VALUE);

	/* undo takes the whole example back at once */
	command (&ed, ":undo");
	CHECK (ed.session.lines.len == 0);

	/* :examples opens the table of contents in the help */
	command (&ed, ":examples");
	CHECK (ed.state == ML_STATE_HELP);
	CHECK (ml_str_view (ed.help.docs.pages.data [ed.help.page].id).len == 8);

	ml_editor_free (&ed);
}

typedef struct Run
{
	int32_t code;
	MlStr   out;
	MlStr   err;
} Run;

static Run run (const char *a0, const char *a1, const char *a2)
{
	Run        r;
	MlSliceVec args;
	const char *list [3] = { a0, a1, a2 };

	MlSliceVec_init (&args);
	ml_str_init (&r.out);
	ml_str_init (&r.err);

	for (uint64_t i = 0; i < 3 && list [i] != NULL; i++)
	{
		CHECK (MlSliceVec_push (&args, lit (list [i])) == ML_OK);
	}

	r.code = ml_cli_run (&args, lit (""), &r.out, &r.err);
	MlSliceVec_free (&args);
	return r;
}

static void test_cli (void)
{
	Run r = run ("examples", NULL, NULL);

	CHECK (r.code == ML_EXIT_OK);
	CHECK (strstr (ml_str_cstr (r.out), "calculus-newton-sqrt2") != NULL);
	ml_str_free (&r.out);
	ml_str_free (&r.err);

	r = run ("examples", "--format=json", NULL);
	CHECK (strstr (ml_str_cstr (r.out), "\"area\":\"Calculus\"") != NULL);
	ml_str_free (&r.out);
	ml_str_free (&r.err);

	/* a name prints the file, which is the example text */
	r = run ("examples", "units-kinetic-energy", NULL);
	CHECK (r.code == ML_EXIT_OK);
	CHECK (
		ml_slice_equal (
			ml_str_view (r.out),
			ml_example_text (ml_example_find (lit ("units-kinetic-energy")))
		)
	);
	ml_str_free (&r.out);
	ml_str_free (&r.err);

	r = run ("examples", "nothing", NULL);
	CHECK (r.code == ML_EXIT_USAGE);
	ml_str_free (&r.out);
	ml_str_free (&r.err);
}

void run_examples_tests (void);

void run_examples_tests (void)
{
	test_files ();
	test_areas ();
	test_pages ();
	test_editor ();
	test_cli ();
}
