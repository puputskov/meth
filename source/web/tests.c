#include "../tests/test.h"

#include <meth/core/heap.h>
#include <meth/core/names.h>
#include <meth/math/eval.h>
#include <meth/parse/words.h>
#include <meth/texts/texts.h>
#include <meth/tutorial/lessons.h>

#include <string.h>

/*
 * The C test groups inside the module (build/web/tests.wasm, made by
 * build_web.bat). The page calls meth_test_run once per group name and reads
 * the checks and failures; what the groups print (the FAIL lines) goes to the
 * page through console_write. The groups here are the ones that need no files,
 * no terminal and no operating system: the others (platform, file, cli, ...)
 * test those, and run natively.
 */

int32_t g_test_failures;
int32_t g_test_checks;

void run_core_tests (void);
void run_session_tests (void);
void run_lineedit_tests (void);
void run_rewrite_tests (void);
void run_brackets_tests (void);
void run_bool_tests (void);
void run_lambda_tests (void);
void run_bigint_tests (void);
void run_rational_tests (void);
void run_value_tests (void);
void run_lexer_tests (void);
void run_parser_tests (void);
void run_eval_tests (void);
void run_stats_tests (void);
void run_plot_tests (void);
void run_hyper_tests (void);
void run_matrix_tests (void);
void run_steps_tests (void);
void run_markup_tests (void);
void run_sym_tests (void);
void run_unit_tests (void);
void run_integrate_tests (void);
void run_words_tests (void);
void run_proof_tests (void);
void run_pga_tests (void);
void run_webapi_tests (void);

typedef struct TestGroup
{
	const char *name;
	void       (*run) (void);
} TestGroup;

static const TestGroup groups [] = {
	{ .name = "core",      .run = run_core_tests },
	{ .name = "session",   .run = run_session_tests },
	{ .name = "lineedit",  .run = run_lineedit_tests },
	{ .name = "rewrite",   .run = run_rewrite_tests },
	{ .name = "brackets",  .run = run_brackets_tests },
	{ .name = "bool",      .run = run_bool_tests },
	{ .name = "lambda",    .run = run_lambda_tests },
	{ .name = "bigint",    .run = run_bigint_tests },
	{ .name = "rational",  .run = run_rational_tests },
	{ .name = "value",     .run = run_value_tests },
	{ .name = "lexer",     .run = run_lexer_tests },
	{ .name = "parser",    .run = run_parser_tests },
	{ .name = "eval",      .run = run_eval_tests },
	{ .name = "stats",     .run = run_stats_tests },
	{ .name = "plot",      .run = run_plot_tests },
	{ .name = "hyper",     .run = run_hyper_tests },
	{ .name = "matrix",    .run = run_matrix_tests },
	{ .name = "steps",     .run = run_steps_tests },
	{ .name = "markup",    .run = run_markup_tests },
	{ .name = "sym",       .run = run_sym_tests },
	{ .name = "unit",      .run = run_unit_tests },
	{ .name = "integrate", .run = run_integrate_tests },
	{ .name = "words",     .run = run_words_tests },
	{ .name = "proof",     .run = run_proof_tests },
	{ .name = "pga",       .run = run_pga_tests },
	{ .name = "webapi",    .run = run_webapi_tests }
};

#define EXPORT(name) \
	__attribute__ ((export_name (name)))

/* How many groups there are, and the name of the n-th (NULL past the end). */
EXPORT ("meth_test_count")
uint32_t meth_test_count (void)
{
	return (uint32_t)ml_count (groups);
}

EXPORT ("meth_test_name")
const char *meth_test_name (uint32_t n)
{
	return ml_choose (n < ml_count (groups), groups [n].name, NULL);
}

/* Runs the n-th group; returns the number of checks it made. */
EXPORT ("meth_test_run")
int32_t meth_test_run (uint32_t n)
{
	int32_t before = g_test_checks;

	if (n < ml_count (groups))
	{
		groups [n].run ();
	}

	return g_test_checks - before;
}

EXPORT ("meth_test_failures")
int32_t meth_test_failures (void)
{
	return g_test_failures;
}

/*
 * Frees what the groups keep for the life of the program and checks that
 * nothing is left on the heap. Returns the failures of the whole run.
 */
EXPORT ("meth_test_finish")
int32_t meth_test_finish (void)
{
	ml_lessons_release ();
	ml_words_release ();
	ml_eval_release ();
	ml_names_release ();
	ml_texts_clear ();
	g_test_checks++;
	g_test_failures += (ml_heap_live_blocks () != 0);
	printf ("heap: %d blocks live\n", (int32_t)ml_heap_live_blocks ());
	return g_test_failures;
}
