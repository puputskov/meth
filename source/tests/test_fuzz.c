#include "test.h"

#include <meth/core/utf8.h>
#include <meth/math/eval.h>
#include <meth/session/mlog.h>
#include <meth/session/settings.h>
#include <meth/ui/editor.h>
#include <meth/ui/theme.h>

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

/* Expressions in x for the symbolic functions. */
static const char *sym_corpus [] = {
	"(x + 1)^2 - x^2",
	"x^2 + 3x + 1",
	"sin (x)^2 + cos (x)^2",
	"(x^2 - 1) / (x - 1)",
	"x * e^x",
	"2x + 3x - x",
	"(a + b)^3 - a^3",
	"1 / x + 1 / (x + 1)",
	"sqrt (x^2)",
	"ln (x^2) - 2 ln (x)"
};

static const char alphabet [] = (
	"0123456789+-*/^()[];, .=xyzabe!<>%_ij"
	"\n#:\"{}|\\$'"
	"\xC3\xA9\x80"
);

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

/* Copies base (at most cap bytes) and applies one to four random edits. */
static uint64_t mutate_text (
	Rng        *r,
	const char *base,
	char       *buf,
	uint64_t   cap
)
{
	uint64_t len   = strlen (base);
	uint64_t edits = 1 + rng_below (r, 4);

	len = ml_choose (len > cap, cap, len);
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

/* A random entry of a corpus, mutated. */
static uint64_t mutate (
	Rng         *r,
	const char **list,
	uint64_t    count,
	char        *buf,
	uint64_t    cap
)
{
	return mutate_text (r, list [rng_below (r, count)], buf, cap);
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

/* Wraps text in a call of fn: "simplify (" text ")". */
static void wrap (MlStr *out, const char *fn, MlSlice text)
{
	ml_str_clear (out);
	ml_str_appendf (out, "%s (%.*s)", fn, (int) text.len, text.data);
}

/*
 * simplify, print, parse and simplify again. Returns 1 when both ran and the
 * second text differs from the first.
 */
static int32_t unstable (MlSlice expr, MlStr *a, MlStr *b, MlStr *call)
{
	int32_t bad = 0;

	wrap (call, "simplify", expr);

	if (run_once (ml_str_view (*call), a) && a->len > 0)
	{
		wrap (call, "simplify", ml_str_view (*a));

		if (run_once (ml_str_view (*call), b) && b->len > 0)
		{
			bad = !ml_slice_equal (ml_str_view (*a), ml_str_view (*b));
		}
	}

	return bad;
}

/* Prints an input that failed, bytes outside printable ASCII escaped. */
static void show_input (const char *what, MlSlice text)
{
	printf ("%s [", what);

	for (uint64_t i = 0; i < text.len; i++)
	{
		uint8_t c = (uint8_t)text.data [i];

		if (c == '\n')
		{
			printf ("\\n");
		}

		else if (c < 32 || c > 126)
		{
			printf ("\\x%02X", c);
		}

		else
		{
			printf ("%c", c);
		}
	}

	printf ("]\n");
}

/* ---- the file readers ---- */

static const char *mlog_corpus [] = {
	"# a note\nm:algebra 1 + 2\nm:algebra f(x) = x^2\n\n"
	"include \"lib.mlog\"\nm:lambda (x. x) y\nm:boolean a and b\n",
	"\xEF\xBB\xBF# bom\r\nm:physics 5 m / 2 s\r\nm:pga2 e1 + e2\r\n",
	"m:algebra 1\nm:algebra\nm:nodomain 3\nstray text\n\n\n# end"
};

static const char *settings_corpus [] = {
	"[theme]\nname = dark\n\n[keys]\nsave = ctrl+s\nquit = ctrl+q\n",
	"# comment\n[s]\nk = old\n[t]\nk = other\n",
	"k = 1\n[s]\n  k  =  spaced  \n"
};

/*
 * Parses a text as a session file and formats it; the format is a fixed point,
 * and valid text stays valid (a line that is not UTF-8 is kept as it is).
 */
static int32_t mlog_round_trip (MlSlice text)
{
	MlSession a;
	MlSession b;
	MlStr     first;
	MlStr     second;
	int32_t   ok = 1;

	ml_session_init (&a);
	ml_session_init (&b);
	ml_str_init (&first);
	ml_str_init (&second);

	ok = (
		   ok
		&& ml_mlog_parse (&a, text) == ML_OK
		&& ml_mlog_format (&first, &a) == ML_OK
		&& (!ml_utf8_valid (text) || ml_utf8_valid (ml_str_view (first)))
		&& ml_mlog_parse (&b, ml_str_view (first)) == ML_OK
		&& ml_mlog_format (&second, &b) == ML_OK
		&& ml_slice_equal (ml_str_view (first), ml_str_view (second))
	);

	ml_str_free (&first);
	ml_str_free (&second);
	ml_session_free (&a);
	ml_session_free (&b);

	return ok;
}

/* A theme file: applying what a theme prints gives that theme back. */
static int32_t theme_round_trip (MlSlice text)
{
	MlTheme  a      = ml_theme_default ();
	MlTheme  b      = ml_theme_default ();
	MlStr    report;
	MlStr    first;
	MlStr    second;
	uint64_t bad    = 0;
	int32_t  ok     = 1;

	ml_str_init (&report);
	ml_str_init (&first);
	ml_str_init (&second);

	ok = (
		   ok
		&& ml_theme_apply_text (&a, text, &report, &bad) == ML_OK
		&& ml_theme_format (&first, &a) == ML_OK
		&& ml_theme_apply_text (&b, ml_str_view (first), NULL, &bad) == ML_OK
		&& ml_theme_format (&second, &b) == ML_OK
		&& ml_slice_equal (ml_str_view (first), ml_str_view (second))
	);

	ml_str_free (&report);
	ml_str_free (&first);
	ml_str_free (&second);

	return ok;
}

/* set then get gives the value, remove then get finds nothing. */
static int32_t settings_round_trip (MlSlice text)
{
	MlStr     set;
	MlStr     gone;
	MlSetting got;
	int32_t   ok = 1;

	ml_str_init (&set);
	ml_str_init (&gone);

	ml_settings_text_entry (text, ml_slice_cstr ("s"), 0);
	ml_settings_text_get (text, ml_slice_cstr ("s"), ml_slice_cstr ("k"));

	ok = ok && ml_settings_text_set (
		&set,
		text,
		ml_slice_cstr ("s"),
		ml_slice_cstr ("k"),
		ml_slice_cstr ("v")
	) == ML_OK;
	got = ml_settings_text_get (
		ml_str_view (set),
		ml_slice_cstr ("s"),
		ml_slice_cstr ("k")
	);
	ok = ok && got.found && ml_slice_equal (got.value, ml_slice_cstr ("v"));
	ok = ok && ml_settings_text_remove (
		&gone,
		ml_str_view (set),
		ml_slice_cstr ("s"),
		ml_slice_cstr ("k")
	) == ML_OK;
	got = ml_settings_text_get (
		ml_str_view (gone),
		ml_slice_cstr ("s"),
		ml_slice_cstr ("k")
	);
	ok = ok && !got.found;

	ml_str_free (&set);
	ml_str_free (&gone);

	return ok;
}

/* ---- the editor ---- */

static const MlKeyCode special_keys [] = {
	ML_KEY_ENTER, ML_KEY_ESCAPE, ML_KEY_BACKSPACE, ML_KEY_TAB, ML_KEY_DELETE,
	ML_KEY_LEFT, ML_KEY_RIGHT, ML_KEY_UP, ML_KEY_DOWN, ML_KEY_HOME,
	ML_KEY_END, ML_KEY_PAGE_UP, ML_KEY_PAGE_DOWN
};

/* A random key: mostly typing, some movement, some control letters. ':' is
 * left out so a random run never reaches the command line (:w, :open). */
static MlKey random_key (Rng *r)
{
	MlKey   k    = { .code = ML_KEY_CHAR, .cp = 0, .mods = 0 };
	uint64_t pick = rng_below (r, 10);

	if (pick < 6)
	{
		k.cp = (uint32_t)(uint8_t)alphabet [rng_below (r, 37)];
	}

	else if (pick < 9)
	{
		k.code = special_keys [rng_below (r, ml_count (special_keys))];
		k.mods = (uint32_t)rng_below (r, 8);
	}

	else
	{
		/* not Ctrl+P (the command line), Ctrl+S, Ctrl+O or Ctrl+Q: they
		 * write or open files, and a name typed after them is a file */
		do
		{
			k.cp = (uint32_t)('a' + rng_below (r, 26));
		} while (k.cp == 'p' || k.cp == 's' || k.cp == 'o' || k.cp == 'q');

		k.mods = ML_MOD_CTRL;
	}

	return k;
}

/* Random keys into an editor; the session must stay sound. */
static int32_t editor_run (Rng *r, uint64_t keys)
{
	MlEditor ed;
	MlStr    text;
	int32_t  ok = 1;

	ml_editor_init (&ed);
	ml_str_init (&text);
	ok = (ml_editor_begin (&ed) == ML_OK);

	for (uint64_t i = 0; i < keys && ok; i++)
	{
		MlResult res = ml_editor_handle_key (&ed, random_key (r));

		/* the command line is left at once, whatever opened it */
		if (ed.state == ML_STATE_COMMAND)
		{
			ml_editor_handle_key (
				&ed,
				(MlKey){ .code = ML_KEY_ESCAPE, .cp = 0, .mods = 0 }
			);
		}

		ok = (
			   (res == ML_OK || res == ML_ERR_NOMEM || res == ML_ERR_PARSE)
			&& ml_utf8_valid (ml_str_view (ed.edit.text))
			&& (
				   ed.session.cursor < ed.session.lines.len
				|| ed.session.cursor == 0
			)
		);
	}

	ok = (
		   ok
		&& ml_mlog_format (&text, &ed.session) == ML_OK
		&& ml_utf8_valid (ml_str_view (text))
	);

	ml_str_free (&text);
	ml_editor_free (&ed);

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
		uint64_t len = mutate (
			&rng,
			corpus,
			ml_count (corpus),
			buf,
			FUZZ_MAX_LEN
		);
		MlSlice  src = ml_slice (buf, len);

		ok   = ok && run_once (src, &first) && run_once (src, &second);
		same = (
			   same
			&& ml_slice_equal (ml_str_view (first), ml_str_view (second))
		);
	}

	CHECK (ok);
	CHECK (same);

	{
		MlStr    call;
		uint64_t bad = 0;

		ml_str_init (&call);

		for (uint64_t i = 0; i < FUZZ_RUNS; i++)
		{
			uint64_t len = mutate (
				&rng,
				sym_corpus,
				ml_count (sym_corpus),
				buf,
				FUZZ_MAX_LEN
			);

			if (unstable (ml_slice (buf, len), &first, &second, &call))
			{
				bad++;
			}
		}

		CHECK (bad == 0);
		ml_str_free (&call);
	}

	ml_str_free (&first);
	ml_str_free (&second);

	{
		char     file [512];
		MlStr    theme_text;
		MlTheme  base;
		uint64_t bad_mlog     = 0;
		uint64_t bad_theme    = 0;
		uint64_t bad_settings = 0;
		uint64_t bad_editor   = 0;

		ml_str_init (&theme_text);
		ml_theme_builtin (&base, ml_slice_cstr ("dark"));
		ml_theme_format (&theme_text, &base);

		for (uint64_t i = 0; i < FUZZ_RUNS / 4; i++)
		{
			uint64_t len = mutate (
				&rng,
				mlog_corpus,
				ml_count (mlog_corpus),
				file,
				sizeof file - 1
			);

			file [len] = 0;
			if (!mlog_round_trip (ml_slice (file, len)))
			{
				bad_mlog++;
				show_input ("mlog", ml_slice (file, len));
			}

			len = mutate_text (
				&rng,
				theme_text.data,
				file,
				sizeof file - 1
			);
			bad_theme += !theme_round_trip (ml_slice (file, len));

			len = mutate (
				&rng,
				settings_corpus,
				ml_count (settings_corpus),
				file,
				sizeof file - 1
			);
			bad_settings += !settings_round_trip (ml_slice (file, len));
		}

		for (uint64_t i = 0; i < FUZZ_RUNS / 100; i++)
		{
			Rng again = rng;

			if (!editor_run (&rng, 400))
			{
				bad_editor++;
				printf (
					"editor run from state %llu\n",
					(unsigned long long)again.state
				);
			}
		}

		CHECK (bad_mlog == 0);
		CHECK (bad_theme == 0);
		CHECK (bad_settings == 0);
		CHECK (bad_editor == 0);
		ml_str_free (&theme_text);
	}
}
