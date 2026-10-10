#include "test.h"

#include <matlog/docs/stdlib.h>
#include <matlog/session/results.h>
#include <matlog/session/watch.h>
#include <matlog/ui/complete.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* Lines of the algebra domain in one session, evaluated in order. */
typedef struct Run
{
	MlSession s;
	MlResults r;
} Run;

static void run_start (Run *run, const char *const *lines, uint64_t count)
{
	ml_session_init (&run->s);
	ml_results_init (&run->r);

	for (uint64_t i = 0; i < count; i++)
	{
		CHECK (
			ml_session_append (
				&run->s,
				ML_LINE_MATH,
				ML_DOMAIN_ALGEBRA,
				lit (lines [i])
			)
			== ML_OK
		);
	}

	CHECK (ml_results_update (&run->r, &run->s, 0) == ML_OK);
}

static void run_end (Run *run)
{
	ml_results_free (&run->r);
	ml_session_free (&run->s);
}

static int32_t gives (const Run *run, uint64_t idx, const char *text)
{
	MlResultView v  = ml_results_get (&run->r, idx);
	int32_t      ok = (
		   v.kind == ML_RESULT_VALUE
		&& ml_slice_equal (v.text, lit (text))
	);

	if (!ok)
	{
		printf (
			"  line %d: \"%.*s\" (kind %d), expected \"%s\"\n",
			(int32_t)idx,
			(int32_t)v.text.len,
			v.text.data,
			(int32_t)v.kind,
			text
		);
	}

	return ok;
}

static void test_loads (void)
{
	MlEnv env;

	/* an environment has no library until it is asked for */
	ml_env_init (&env);
	CHECK (env.library == 0);
	CHECK (ml_stdlib_load (&env) == ML_OK);
	CHECK (env.library > 20);
	CHECK (env.library == env.bindings.len);
	CHECK (ml_env_lookup (&env, lit ("fib"), env.library).kind == ML_BIND_FUNC);
	ml_env_free (&env);
}

static void test_numbers (void)
{
	const char *lines [] = {
		"fib(0)",
		"fib(1)",
		"fib(10)",
		"fib(100)",
		"isprime(1)",
		"isprime(2)",
		"isprime(97)",
		"isprime(91)",
		"isprime(1000003)",
		"primes(30)",
		"factor(360)",
		"factor(97)",
		"factor(1)",
		"divisors(28)",
		"totient(36)",
		"sigma(28)",
		"collatz(27)",
		"digits(9045)",
		"mod(-1, 5)",
		"mod(17, 5)",
		"sign(-3) + sign(0) + sign(9)",
		"frac(7/2)",
		"trunc(-7/2)",
		"lerp(10, 20, 1/4)",
		"clamp(15, 0, 10)",
		"hypot(3, 4)",
		"linspace(0, 1, 5)",
		"cumprod(range(1, 5))",
		"dotl(range(1, 3), range(4, 6))",
		"normalize(list(2, -4, 1))",
		"len(primes(1000))",
		"len(factor(2^20))",
		"isqrt(99)",
		"isqrt(100)",
		"isqrt(10^30 + 5)",
		"isqrt(1)"
	};

	Run run;

	run_start (&run, lines, ml_count (lines));
	CHECK (gives (&run, 0, "0"));
	CHECK (gives (&run, 1, "1"));
	CHECK (gives (&run, 2, "55"));
	CHECK (gives (&run, 3, "354224848179261915075"));
	CHECK (gives (&run, 4, "false"));
	CHECK (gives (&run, 5, "true"));
	CHECK (gives (&run, 6, "true"));
	CHECK (gives (&run, 7, "false"));
	CHECK (gives (&run, 8, "true"));
	CHECK (gives (&run, 9, "[2, 3, 5, 7, 11, 13, 17, 19, 23, 29]"));
	CHECK (gives (&run, 10, "[2, 2, 2, 3, 3, 5]"));
	CHECK (gives (&run, 11, "[97]"));
	CHECK (gives (&run, 12, "[]"));
	CHECK (gives (&run, 13, "[1, 2, 4, 7, 14, 28]"));
	CHECK (gives (&run, 14, "12"));
	CHECK (gives (&run, 15, "56"));
	CHECK (gives (&run, 16, "111"));
	CHECK (gives (&run, 17, "[9, 0, 4, 5]"));
	CHECK (gives (&run, 18, "4"));
	CHECK (gives (&run, 19, "2"));
	CHECK (gives (&run, 20, "0"));
	CHECK (gives (&run, 21, "1/2"));
	CHECK (gives (&run, 22, "-3"));
	CHECK (gives (&run, 23, "25/2"));
	CHECK (gives (&run, 24, "10"));
	CHECK (gives (&run, 25, "5"));
	CHECK (gives (&run, 26, "[0, 1/4, 1/2, 3/4, 1]"));
	CHECK (gives (&run, 27, "[1, 2, 6, 24, 120]"));
	CHECK (gives (&run, 28, "32"));
	CHECK (gives (&run, 29, "[1/2, -1, 1/4]"));
	CHECK (gives (&run, 30, "168"));
	CHECK (gives (&run, 31, "20"));
	CHECK (gives (&run, 32, "9"));
	CHECK (gives (&run, 33, "10"));
	CHECK (gives (&run, 34, "1000000000000000"));
	CHECK (gives (&run, 35, "1"));
	run_end (&run);
}

/* The library is the library's: hidden, and the user's names win. */
static void test_hidden_and_shadowed (void)
{
	const char *lines [] = {
		"a = 5",
		"fib(n) = n * 100",
		"fib(3)",
		"mod(7, 4)"
	};

	Run            run;
	MlWatchRowVec rows;

	run_start (&run, lines, ml_count (lines));
	CHECK (gives (&run, 2, "300"));
	CHECK (gives (&run, 3, "3"));

	/* the watch window lists a and fib of the user, not the library */
	MlWatchRowVec_init (&rows);
	CHECK (ml_watch_rows (&rows, &run.r, 3) == ML_OK);
	CHECK (rows.len == 2);
	ml_watch_rows_free (&rows);

	/* the completion offers the library functions, not its helpers */
	{
		uint64_t     all = run.r.env.bindings.len;
		MlCompletion c   = ml_complete_math (&run.r.env, all, lit ("fa"));

		CHECK (c.count > 0);

		c = ml_complete_math (&run.r.env, all, lit ("_fi"));
		CHECK (c.count == 0);

		c = ml_complete_math (&run.r.env, all, lit ("isp"));
		CHECK (c.count == 1);
	}

	/* an edit that starts over keeps the library */
	CHECK (ml_results_update (&run.r, &run.s, 0) == ML_OK);
	CHECK (run.r.env.library > 20);
	CHECK (gives (&run, 3, "3"));
	run_end (&run);
}

/* Every function of the library has the note above it: # name (args): ... */
static void test_documented (void)
{
	for (uint64_t i = 0; i < ml_stdlib_source_count; i++)
	{
		MlSlice  text = lit (ml_stdlib_sources [i].text);
		uint64_t pos  = 0;
		MlSlice  note = lit ("");

		while (pos < text.len)
		{
			uint64_t end = pos;
			MlSlice  line;

			while (end < text.len && text.data [end] != '\n')
			{
				end++;
			}

			line = ml_slice_sub (text, pos, end - pos);
			pos  = end + 1;

			if (line.len > 10 && memcmp (line.data, "m:algebra ", 10) == 0)
			{
				MlSlice def  = ml_slice_sub (line, 10, line.len - 10);
				uint64_t k   = 0;

				while (k < def.len && def.data [k] != '(')
				{
					k++;
				}

				/* helpers (a leading underscore) need no note */
				if (def.data [0] != '_')
				{
					MlStr want;

					ml_str_init (&want);
					ml_str_appendf (&want, "# %.*s (", (int32_t)k, def.data);
					int32_t ok = (
						   note.len >= want.len
						&& memcmp (note.data, want.data, want.len) == 0
					);

					CHECK (ok);

					if (!ok)
					{
						printf ("  no note for: %.*s\n", (int32_t)k, def.data);
					}

					ml_str_free (&want);
				}
			}

			note = line;
		}
	}
}

void run_stdlib_tests (void);

void run_stdlib_tests (void)
{
	test_loads ();
	test_numbers ();
	test_hidden_and_shadowed ();
	test_documented ();
}
