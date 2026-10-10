#include "test.h"

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

int main (void)
{
	run_core_tests ();
	run_platform_tests ();
	run_session_tests ();
	run_mlog_tests ();
	run_lineedit_tests ();
	run_rewrite_tests ();
	run_editor_tests ();
	run_brackets_tests ();
	run_bool_tests ();
	run_lambda_tests ();
	run_screen_tests ();
	run_complete_tests ();
	run_bigint_tests ();
	run_rational_tests ();
	run_value_tests ();
	run_lexer_tests ();
	run_parser_tests ();
	run_env_tests ();
	run_eval_tests ();
	run_results_tests ();
	run_ghost_tests ();
	run_keymap_tests ();
	run_options_tests ();
	run_examples_tests ();
	run_snippets_tests ();
	run_watch_tests ();
	run_stats_tests ();
	run_hyper_tests ();
	run_tutorial_tests ();
	run_matrix_tests ();
	run_steps_tests ();
	run_cli_tests ();
	run_markup_tests ();
	run_sym_tests ();
	run_posix_key_tests ();
	run_unit_tests ();
	run_folds_tests ();
	run_integrate_tests ();
	run_theme_tests ();
	run_settings_tests ();
	run_export_tests ();
	run_docs_tests ();

	printf ("%d checks, %d failed\n", g_test_checks, g_test_failures);
	return ml_choose (
		g_test_failures,
		1,
		0
	);
}
