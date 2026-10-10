#ifndef METH_TEST_H
#define METH_TEST_H

#include <meth/core/types.h>
#include <stdio.h>

/*
 * Minimal test harness: each failed check is counted and reported, the run
 * continues.
 */

extern int32_t g_test_failures;
extern int32_t g_test_checks;

#define CHECK(cond) \
	do \
	{ \
		g_test_checks++; \
		if (!(cond)) \
		{ \
			g_test_failures++; \
			printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

#endif /* METH_TEST_H */
