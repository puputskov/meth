#include "test.h"
#include "test_keys.h"

#include <matlog/ui/ghost.h>
#include <matlog/ui/screen.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static void add_in (MlSession *s, MlDomain domain, const char *text)
{
	CHECK (ml_session_append (s, ML_LINE_MATH, domain, lit (text)) == ML_OK);
}

static void add (MlSession *s, const char *text)
{
	add_in (s, ML_DOMAIN_ALGEBRA, text);
}

/* 1 when the preview of text as line idx is exactly expected. */
static int32_t previews (
	const MlResults *r,
	MlDomain        domain,
	uint64_t        idx,
	const char      *text,
	const char      *expected
)
{
	MlStr   out;
	MlGhost g;
	int32_t same;

	ml_str_init (&out);
	g    = ml_ghost (&out, r, domain, idx, lit (text));
	same = (
		   g.result == ML_OK
		&& g.shown
		&& ml_slice_equal (ml_str_view (out), lit (expected))
	);

	if (!same)
	{
		printf (
			"  \"%s\": shown %d \"%s\", expected \"%s\"\n",
			text,
			g.shown,
			ml_str_cstr (out),
			expected
		);
	}

	ml_str_free (&out);
	return same;
}

/* 1 when nothing is shown and out is left alone. */
static int32_t silent (
	const MlResults *r,
	MlDomain        domain,
	uint64_t        idx,
	const char      *text
)
{
	MlStr   out;
	MlGhost g;
	int32_t same;

	ml_str_init (&out);
	g    = ml_ghost (&out, r, domain, idx, lit (text));
	same = g.result == ML_OK && !g.shown && out.len == 0;

	if (!same)
	{
		printf (
			"  \"%s\": shown %d \"%s\", expected nothing\n",
			text,
			g.shown,
			ml_str_cstr (out)
		);
	}

	ml_str_free (&out);
	return same;
}

static void test_preview (void)
{
	MlSession s;
	MlResults r;
	uint64_t  bindings;

	ml_session_init (&s);
	ml_results_init (&r);
	add (&s, "a = 3");
	add (&s, "f(x) = 2x + a");
	add (&s, "a = 10");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	bindings = ml_env_snapshot (&r.env);

	/* complete input shows the result, at the end of the session */
	CHECK (previews (&r, ML_DOMAIN_ALGEBRA, 3, "1/3 + 1/6", "1/2"));
	CHECK (previews (&r, ML_DOMAIN_ALGEBRA, 3, "f(1)", "12"));
	CHECK (
		previews (
			&r,
			ML_DOMAIN_ALGEBRA,
			3,
			"sqrt(2)",
			"\xE2\x89\x88 1.41421356"
		)
	);

	/* the environment is the one at the line: in line 1 a is still 3 */
	CHECK (previews (&r, ML_DOMAIN_ALGEBRA, 1, "a", "3"));
	CHECK (previews (&r, ML_DOMAIN_ALGEBRA, 3, "a", "10"));
	CHECK (silent (&r, ML_DOMAIN_ALGEBRA, 0, "a"));
	CHECK (silent (&r, ML_DOMAIN_ALGEBRA, 1, "f(1)"));

	/* a definition previews its value and binds nothing */
	CHECK (previews (&r, ML_DOMAIN_ALGEBRA, 3, "b = 5", "5"));
	CHECK (silent (&r, ML_DOMAIN_ALGEBRA, 3, "b"));
	CHECK (ml_env_snapshot (&r.env) == bindings);

	/* incomplete input, errors, empty lines and function definitions */
	CHECK (silent (&r, ML_DOMAIN_ALGEBRA, 3, "2+"));
	CHECK (silent (&r, ML_DOMAIN_ALGEBRA, 3, "(3"));
	CHECK (silent (&r, ML_DOMAIN_ALGEBRA, 3, "f("));
	CHECK (silent (&r, ML_DOMAIN_ALGEBRA, 3, "1/0"));
	CHECK (silent (&r, ML_DOMAIN_ALGEBRA, 3, "nothing + 1"));
	CHECK (silent (&r, ML_DOMAIN_ALGEBRA, 3, ""));
	CHECK (silent (&r, ML_DOMAIN_ALGEBRA, 3, "  "));
	CHECK (silent (&r, ML_DOMAIN_ALGEBRA, 3, "g(x) = x"));
	CHECK (ml_env_snapshot (&r.env) == bindings);

	ml_results_free (&r);
	ml_session_free (&s);
}

static void test_logic_preview (void)
{
	MlSession s;
	MlResults r;
	uint64_t  defs;

	ml_session_init (&s);
	ml_results_init (&r);
	add_in (&s, ML_DOMAIN_BOOLEAN, "p = a & b");
	add_in (&s, ML_DOMAIN_BOOLEAN, "p | a");
	add_in (&s, ML_DOMAIN_LAMBDA, "id = \xCE\xBBx. x");
	add_in (&s, ML_DOMAIN_LAMBDA, "id y");
	CHECK (ml_results_update (&r, &s, 0) == ML_OK);
	defs = r.defs.len;

	/* the boolean preview is the minimal form, with the names above */
	CHECK (previews (&r, ML_DOMAIN_BOOLEAN, 4, "p | a", "a"));
	CHECK (previews (&r, ML_DOMAIN_BOOLEAN, 4, "a & ~a", "false"));
	CHECK (previews (&r, ML_DOMAIN_BOOLEAN, 4, "p", "a \xE2\x88\xA7 b"));
	CHECK (
		previews (
			&r,
			ML_DOMAIN_BOOLEAN,
			4,
			"x -> y",
			"\xC2\xACx \xE2\x88\xA8 y"
		)
	);

	/* a line sees only the definitions above it */
	CHECK (previews (&r, ML_DOMAIN_BOOLEAN, 0, "p", "p"));

	/* the lambda preview is the normal form */
	CHECK (previews (&r, ML_DOMAIN_LAMBDA, 4, "id y", "y"));
	CHECK (previews (&r, ML_DOMAIN_LAMBDA, 4, "2", "\xCE\xBB" "f x. f (f x)"));
	CHECK (previews (&r, ML_DOMAIN_LAMBDA, 2, "id y", "id y"));

	/* incomplete input, errors and empty lines show nothing */
	CHECK (silent (&r, ML_DOMAIN_BOOLEAN, 4, "a &"));
	CHECK (silent (&r, ML_DOMAIN_BOOLEAN, 4, "(a"));
	CHECK (silent (&r, ML_DOMAIN_BOOLEAN, 4, ""));
	CHECK (silent (&r, ML_DOMAIN_LAMBDA, 4, "\xCE\xBBx."));
	CHECK (silent (&r, ML_DOMAIN_LAMBDA, 4, "(x"));
	CHECK (
		silent (
			&r,
			ML_DOMAIN_LAMBDA,
			4,
			"(\xCE\xBBx. x x) (\xCE\xBBx. x x)"
		)
	);
	CHECK (silent (&r, ML_DOMAIN_LAMBDA, 4, ""));

	/* nothing is defined by a preview */
	CHECK (
		previews (
			&r,
			ML_DOMAIN_LAMBDA,
			4,
			"k = \xCE\xBBx y. x",
			"\xCE\xBBx y. x"
		)
	);
	CHECK (r.defs.len == defs);

	ml_results_free (&r);
	ml_session_free (&s);
}

static void test_typing (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 30, 5);
	CHECK (ml_editor_begin (&ed) == ML_OK);

	for (uint64_t i = 0; "1+2"[i] != '\0'; i++)
	{
		MlKey k =
		{
			.code = ML_KEY_CHAR,
			.cp   = (uint8_t)"1+2"[i],
			.mods = 0
		};

		CHECK (ml_editor_handle_key (&ed, k) == ML_OK);
	}

	/* the preview follows the line being typed */
	l = ml_screen_layout (&s, &ed);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "1+2") != NULL);
	CHECK (
		strstr (ml_str_cstr (l.rows.data [0]), "  \x1b[2m= 3\x1b[0m") != NULL
	);
	CHECK (l.cursor.row == 1 && l.cursor.col == 12);
	ml_layout_free (&l);

	{
		MlKey k = { .code = ML_KEY_CHAR, .cp = '+', .mods = 0 };

		CHECK (ml_editor_handle_key (&ed, k) == ML_OK);
	}

	l = ml_screen_layout (&s, &ed);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "= ") == NULL);
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

void run_ghost_tests (void);

void run_ghost_tests (void)
{
	test_preview ();
	test_logic_preview ();
	test_typing ();
}
