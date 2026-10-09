#include "test.h"

int32_t g_test_failures;
int32_t g_test_checks;

void run_core_tests (void);

int main (void)
{
	run_core_tests ();

	printf ("%d checks, %d failed\n", g_test_checks, g_test_failures);
	return g_test_failures ? 1 : 0;
}
