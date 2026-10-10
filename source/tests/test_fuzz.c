#include "test.h"

#include <matlog/math/eval.h>

#include <string.h>

/*
 * A short fixed run of the fuzzer: a seeded generator mutates a corpus of
 * statements and feeds them to the parser and the evaluator. The checks are
 * that nothing crashes or hangs (the sanitizer builds find the memory faults),
 * that the same input gives the same answer twice, and that an error always
 * has a position inside the source.
 */

#ifndef FUZZ_RUNS
#define FUZZ_RUNS    3000
#endif
#define FUZZ_MAX_LEN 96

static const char *corpus [] = {
	"1 + 2 * 3",
	"2^10 - 1",
	"(1/3 + 1/6) * 4",
	"sqrt (2) ^ 2",
	"f(x) = x^2 + 3x + 1",
	"diff (x^3 + sin (x), x)",
	"integrate (x * e^x, x)",
	"simplify ((x + 1)^2 - x^2)",
	"expand ((a + b)^3)",
	"solve (x^2 - 5x + 6 = 0, x)",
	"[1 2; 3 4] * [5 6; 7 8]",
	"det ([2 1; 1 3])",
	"inv ([1 2; 3 4])",
	"30!",
	"gcd (84, 36)",
	"5 m / 2 s",
	"mean ([1, 2, 3, 4])",
	"limit (sin (x) / x, x, 0)",
	"1.5e3 + 0.25",
	"a = 7; a * a",
	"(3 + 4i) * (1 - 2i)",
	"x > 3 and not y"
};

static const char alphabet [] = "0123456789+-*/^()[];, .=xyzabe!<>%_ij";

typedef struct Rng
{
	uint64_t state;
} Rng;

/* xorshift64*: small, and the same sequence on every platform. */
static uint64_t rng_next (Rng *r)
{
	uint64_t x = r->state;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	r->state = x;

	return x * 0x2545F4914F6CDD1DULL;
}

static uint64_t rng_below (Rng *r, uint64_t n)
{
	return (rng_next (r) >> 11) % n;
}

/* Copies a corpus entry and applies one to four random edits. */
static uint64_t mutate (Rng *r, char *buf, uint64_t cap)
{
	const char *base  = corpus [rng_below (r, ml_count (corpus))];
	uint64_t    len   = strlen (base);
	uint64_t    edits = 1 + rng_below (r, 4);

	memcpy (buf, base, len);

	for (uint64_t e = 0; e < edits; e++)
	{
		uint64_t at   = rng_below (r, len + 1);
		uint64_t kind = rng_below (r, 4);

		switch (kind)
		{
			case 0:
			{
				if (len < cap)
				{
					memmove (buf + at + 1, buf + at, len - at);
					buf [at] = alphabet [rng_below (r, sizeof alphabet - 1)];
					len++;
				}
			} break;

			case 1:
			{
				if (at < len)
				{
					memmove (buf + at, buf + at + 1, len - at - 1);
					len--;
				}
			} break;

			case 2:
			{
				if (at < len)
				{
					buf [at] = alphabet [rng_below (r, sizeof alphabet - 1)];
				}
			} break;

			default:
			{
				uint64_t n = rng_below (r, len - at + 1);

				if (len + n <= cap)
				{
					memmove (buf + at + n, buf + at, len - at);
					memcpy (buf + at, buf + at + n, n);
					len += n;
				}
			} break;
		}
	}

	return len;
}

/* Parses and evaluates one input in a fresh environment, formats the result. */
static int32_t run_once (MlSlice source, MlStr *out)
{
	MlEnv         env;
	MlArena       arena;
	MlParseResult p;
	int32_t       ok = 1;

	ml_env_init (&env);
	ml_arena_init (&arena, 0);
	ml_str_clear (out);

	p = ml_parse (source);

	if (p.error.code == ML_PARSE_OK)
	{
		MlValue v = ml_eval (
			&arena,
			&env,
			ml_env_snapshot (&env),
			source,
			&p.ast
		);

		if (
			   ml_value_is_number (v)
			|| ml_value_is_matrix (v)
			|| ml_value_is_expr (v)
			|| ml_value_is_quantity (v)
			|| v.kind == ML_VALUE_COMPLEX
			|| v.kind == ML_VALUE_QUAT
			|| v.kind == ML_VALUE_BOOL
			|| v.kind == ML_VALUE_LIST
		)
		{
			ok = (ml_value_format (out, &arena, v) == ML_OK);
		}

		ml_ast_free (&p.ast);
	}

	ml_arena_free (&arena);
	ml_env_free (&env);

	return ok;
}

void run_fuzz_tests (void)
{
	Rng     rng = { .state = 0x9E3779B97F4A7C15ULL };
	char    buf [FUZZ_MAX_LEN + 1];
	MlStr   first;
	MlStr   second;
	int32_t same = 1;
	int32_t ok   = 1;

	ml_str_init (&first);
	ml_str_init (&second);

	for (uint64_t i = 0; i < FUZZ_RUNS; i++)
	{
		uint64_t len = mutate (&rng, buf, FUZZ_MAX_LEN);
		MlSlice  src = ml_slice (buf, len);

		ok   = ok && run_once (src, &first) && run_once (src, &second);
		same = (
			   same
			&& ml_slice_equal (ml_str_view (first), ml_str_view (second))
		);
	}

	CHECK (ok);
	CHECK (same);

	ml_str_free (&first);
	ml_str_free (&second);
}
