#include "test.h"

#include <meth/core/heap.h>
#include <meth/core/names.h>
#include <meth/math/eval.h>
#include <meth/parse/words.h>
#include <meth/texts/texts.h>
#include <meth/tutorial/lessons.h>

#include <string.h>

int32_t g_test_failures;
int32_t g_test_checks;

void run_core_tests (void);
void run_platform_tests (void);
void run_session_tests (void);
void run_mlog_tests (void);
void run_lineedit_tests (void);
void run_rewrite_tests (void);
void run_editor_tests (void);
void run_brackets_tests (void);
void run_bool_tests (void);
void run_lambda_tests (void);
void run_screen_tests (void);
void run_complete_tests (void);
void run_bigint_tests (void);
void run_rational_tests (void);
void run_value_tests (void);
void run_lexer_tests (void);
void run_parser_tests (void);
void run_env_tests (void);
void run_eval_tests (void);
void run_results_tests (void);
void run_ghost_tests (void);
void run_keymap_tests (void);
void run_options_tests (void);
void run_examples_tests (void);
void run_snippets_tests (void);
void run_watch_tests (void);
void run_stats_tests (void);
void run_plot_tests (void);
void run_stdlib_tests (void);
void run_texts_tests (void);
void run_hyper_tests (void);
void run_tutorial_tests (void);
void run_matrix_tests (void);
void run_steps_tests (void);
void run_cli_tests (void);
void run_markup_tests (void);
void run_sym_tests (void);
void run_posix_key_tests (void);
void run_unit_tests (void);
void run_folds_tests (void);
void run_integrate_tests (void);
void run_theme_tests (void);
void run_settings_tests (void);
void run_export_tests (void);
void run_docs_tests (void);
void run_words_tests (void);
void run_proof_tests (void);
void run_pga_tests (void);
void run_fuzz_tests (void);

typedef struct TestGroup
{
	const char *name;
	void       (*run) (void);
} TestGroup;

/* The groups in the order they run; a name on the command line picks one. */
static const TestGroup groups [] = {
	{ .name = "core",      .run = run_core_tests },
	{ .name = "platform",  .run = run_platform_tests },
	{ .name = "session",   .run = run_session_tests },
	{ .name = "mlog",      .run = run_mlog_tests },
	{ .name = "lineedit",  .run = run_lineedit_tests },
	{ .name = "rewrite",   .run = run_rewrite_tests },
	{ .name = "editor",    .run = run_editor_tests },
	{ .name = "brackets",  .run = run_brackets_tests },
	{ .name = "bool",      .run = run_bool_tests },
	{ .name = "lambda",    .run = run_lambda_tests },
	{ .name = "screen",    .run = run_screen_tests },
	{ .name = "complete",  .run = run_complete_tests },
	{ .name = "bigint",    .run = run_bigint_tests },
	{ .name = "rational",  .run = run_rational_tests },
	{ .name = "value",     .run = run_value_tests },
	{ .name = "lexer",     .run = run_lexer_tests },
	{ .name = "parser",    .run = run_parser_tests },
	{ .name = "env",       .run = run_env_tests },
	{ .name = "eval",      .run = run_eval_tests },
	{ .name = "results",   .run = run_results_tests },
	{ .name = "ghost",     .run = run_ghost_tests },
	{ .name = "keymap",    .run = run_keymap_tests },
	{ .name = "options",   .run = run_options_tests },
	{ .name = "examples",  .run = run_examples_tests },
	{ .name = "snippets",  .run = run_snippets_tests },
	{ .name = "watch",     .run = run_watch_tests },
	{ .name = "stats",     .run = run_stats_tests },
	{ .name = "plot",      .run = run_plot_tests },
	{ .name = "stdlib",    .run = run_stdlib_tests },
	{ .name = "texts",     .run = run_texts_tests },
	{ .name = "hyper",     .run = run_hyper_tests },
	{ .name = "tutorial",  .run = run_tutorial_tests },
	{ .name = "matrix",    .run = run_matrix_tests },
	{ .name = "steps",     .run = run_steps_tests },
	{ .name = "cli",       .run = run_cli_tests },
	{ .name = "markup",    .run = run_markup_tests },
	{ .name = "sym",       .run = run_sym_tests },
	{ .name = "posix_key", .run = run_posix_key_tests },
	{ .name = "unit",      .run = run_unit_tests },
	{ .name = "folds",     .run = run_folds_tests },
	{ .name = "integrate", .run = run_integrate_tests },
	{ .name = "theme",     .run = run_theme_tests },
	{ .name = "settings",  .run = run_settings_tests },
	{ .name = "export",    .run = run_export_tests },
	{ .name = "docs",      .run = run_docs_tests },
	{ .name = "pga",       .run = run_pga_tests },
	{ .name = "words",     .run = run_words_tests },
	{ .name = "proof",     .run = run_proof_tests },
	{ .name = "fuzz",      .run = run_fuzz_tests }
};

/*
 * Runs every group, or only the groups named on the command line. Each group
 * prints its name and how many checks it made, so a group that hangs or
 * crashes is the last name on the screen. Exit status 1 when a check failed
 * or a name matches no group.
 */
int main (int argc, char **argv)
{
	int32_t unknown = 0;

	for (int32_t a = 1; a < argc; a++)
	{
		int32_t found = 0;

		for (uint64_t i = 0; i < ml_count (groups); i++)
		{
			found += (strcmp (argv [a], groups [i].name) == 0);
		}

		if (!found)
		{
			printf ("no test group named %s\n", argv [a]);
			unknown = 1;
		}
	}

	for (uint64_t i = 0; i < ml_count (groups) && !unknown; i++)
	{
		int32_t wanted = (argc < 2);

		for (int32_t a = 1; a < argc; a++)
		{
			wanted = wanted || strcmp (argv [a], groups [i].name) == 0;
		}

		if (wanted)
		{
			int32_t before = g_test_checks;

			printf ("%-12s", groups [i].name);
			fflush (stdout);
			groups [i].run ();
			printf ("%d checks\n", g_test_checks - before);
			fflush (stdout);
		}
	}

	/* these are kept for the life of the program */
	ml_lessons_release ();
	ml_words_release ();
	ml_eval_release ();
	ml_names_release ();
	ml_texts_clear ();
	printf ("heap: %d blocks live\n", (int32_t)ml_heap_live_blocks ());

	/* a block still live now was never freed; the tests of the heap itself
	   free a block twice on purpose, two times */
	g_test_checks++;
	g_test_failures += (ml_heap_live_blocks () != 0);
	g_test_checks++;
	g_test_failures += (ml_heap_bad_frees () > 2);
	printf ("%d checks, %d failed\n", g_test_checks, g_test_failures);
	return ml_choose (
		g_test_failures || unknown,
		1,
		0
	);
}
