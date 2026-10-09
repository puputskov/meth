#include "test.h"

int32_t g_test_failures;
int32_t g_test_checks;

void run_core_tests (void);
void run_platform_tests (void);
void run_session_tests (void);
void run_mlog_tests (void);

int main (void)
{
	run_core_tests ();
	run_platform_tests ();
	run_session_tests ();
	run_mlog_tests ();

	printf ("%d checks, %d failed\n", g_test_checks, g_test_failures);
	return g_test_failures ? 1 : 0;
}
