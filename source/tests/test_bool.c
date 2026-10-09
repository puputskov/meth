#include "test.h"

#include <matlog/logic/bool.h>
#include <matlog/session/session.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* Runs a line against the definitions; the result text is compared. */
static int32_t gives (MlLogicDefVec *defs, const char *line, const char *want)
{
	MlLogicOut o;
	int32_t    same;

	ml_logic_out_init (&o);
	same = (
		   ml_bool_run (defs, defs->len, lit (line), &o) == ML_OK
		&& o.error == ML_LOGIC_OK
		&& ml_slice_equal (ml_str_view (o.text), lit (want))
	);

	if (!same)
	{
		printf (
			"  \"%s\" gives \"%s\"%s, expected \"%s\"\n",
			line,
			ml_str_cstr (o.text),
			ml_choose (o.error != ML_LOGIC_OK, " (error)", ""),
			want
		);
	}

	ml_logic_out_free (&o);
	return same;
}

static int32_t fails (MlLogicDefVec *defs, const char *line, const char *code)
{
	MlLogicOut o;
	int32_t    same;

	ml_logic_out_init (&o);
	same = (
		   ml_bool_run (defs, defs->len, lit (line), &o) == ML_OK
		&& o.error != ML_LOGIC_OK
		&& strcmp (ml_logic_error_code (o.error), code) == 0
	);

	if (!same)
	{
		printf (
			"  \"%s\" gives error \"%s\", expected \"%s\"\n",
			line,
			ml_logic_error_code (o.error),
			code
		);
	}

	ml_logic_out_free (&o);
	return same;
}

/* Keeps the definition a line made. */
static int32_t keep (MlLogicDefVec *defs, const MlLogicOut *o)
{
	return ml_logic_defs_add (
		defs,
		ML_DOMAIN_BOOLEAN,
		ml_str_view (o->def_name),
		ml_str_view (o->def_text)
	) == ML_OK;
}

static void test_values (void)
{
	MlLogicDefVec d;

	MlLogicDefVec_init (&d);

	/* constants and the operators, in their spellings */
	CHECK (gives (&d, "true", "true"));
	CHECK (gives (&d, "1 & 0", "false"));
	CHECK (gives (&d, "true && false || true", "true"));
	CHECK (gives (&d, "~true", "false"));
	CHECK (gives (&d, "!0", "true"));
	CHECK (gives (&d, "not false and true", "true"));
	CHECK (gives (&d, "true ^ true", "false"));
	CHECK (gives (&d, "true xor false", "true"));
	CHECK (gives (&d, "true -> false", "false"));
	CHECK (gives (&d, "false -> anything", "true"));
	CHECK (gives (&d, "true <-> true", "true"));
	CHECK (gives (&d, "true == false", "false"));
	CHECK (
		gives (&d, "\xE2\x8A\xA4 \xE2\x88\xA7 \xC2\xAC\xE2\x8A\xA5", "true")
	);

	/* precedence: not, and, xor, or, implies, equal */
	CHECK (gives (&d, "true | false & false", "true"));
	CHECK (gives (&d, "false -> false -> false", "true"));
	CHECK (gives (&d, "(true | false) & false", "false"));
	CHECK (gives (&d, "1 | 1 ^ 1", "true"));

	/* empty lines have no result */
	{
		MlLogicOut o;

		ml_logic_out_init (&o);
		CHECK (ml_bool_run (&d, 0, lit ("  "), &o) == ML_OK && !o.value);
		ml_logic_out_free (&o);
	}

	MlLogicDefVec_free (&d);
}

static void test_simplify (void)
{
	MlLogicDefVec d;

	MlLogicDefVec_init (&d);

	/* the minimal sum of products, or what was written when it is shorter */
	CHECK (gives (&d, "a & a", "a"));
	CHECK (gives (&d, "a | a & b", "a"));
	CHECK (gives (&d, "a & ~a", "false"));
	CHECK (gives (&d, "a | ~a", "true"));
	CHECK (gives (&d, "a & b | a & ~b", "a"));
	CHECK (gives (&d, "~(a & b)", "\xC2\xAC" "a \xE2\x88\xA8 \xC2\xAC" "b"));
	CHECK (gives (&d, "a ^ b", "a ^ b"));
	CHECK (gives (&d, "a -> b", "\xC2\xAC" "a \xE2\x88\xA8 b"));
	CHECK (gives (&d, "(a | b) & (a | ~b)", "a"));
	CHECK (
		gives (
			&d,
			"a & b | ~a & c | b & c",
			"\xC2\xAC" "a \xE2\x88\xA7 c \xE2\x88\xA8 a \xE2\x88\xA7 b"
		)
	);

	/* equivalences are tautologies */
	CHECK (gives (&d, "~(a & b) == (~a | ~b)", "true"));
	CHECK (gives (&d, "(a -> b) == (~b -> ~a)", "true"));
	CHECK (
		gives (
			&d,
			"(a -> b) == (b -> a)",
			"\xC2\xAC" "a \xE2\x88\xA7 \xC2\xAC" "b "
			"\xE2\x88\xA8 a \xE2\x88\xA7 b"
		)
	);

	MlLogicDefVec_free (&d);
}

static void test_defs (void)
{
	MlLogicDefVec d;
	MlLogicOut    o;

	MlLogicDefVec_init (&d);

	/* a definition has the expanded text and is used below */
	ml_logic_out_init (&o);
	CHECK (ml_bool_run (&d, d.len, lit ("p = a & b"), &o) == ML_OK);
	CHECK (ml_slice_equal (ml_str_view (o.text), lit ("a \xE2\x88\xA7 b")));
	CHECK (ml_slice_equal (ml_str_view (o.def_name), lit ("p")));
	CHECK (keep (&d, &o));
	ml_logic_out_free (&o);

	CHECK (gives (&d, "p | ~a", "\xC2\xAC" "a \xE2\x88\xA8 b"));
	CHECK (gives (&d, "p & ~b", "false"));

	/* a name defined as a constant */
	ml_logic_out_init (&o);
	CHECK (ml_bool_run (&d, d.len, lit ("a = true"), &o) == ML_OK);
	CHECK (keep (&d, &o));
	ml_logic_out_free (&o);
	CHECK (gives (&d, "a & c", "c"));

	/* a line sees only the definitions before it */
	{
		MlLogicOut v;

		ml_logic_out_init (&v);
		CHECK (ml_bool_run (&d, 0, lit ("a"), &v) == ML_OK);
		CHECK (ml_slice_equal (ml_str_view (v.text), lit ("a")));
		ml_logic_out_free (&v);
	}

	MlLogicDefVec_free (&d);
}

static void test_table (void)
{
	MlLogicDefVec d;
	MlLogicOut    o;

	MlLogicDefVec_init (&d);
	ml_logic_out_init (&o);
	CHECK (ml_bool_run (&d, 0, lit ("table a & b"), &o) == ML_OK);
	CHECK (ml_slice_equal (ml_str_view (o.text), lit ("a \xE2\x88\xA7 b")));
	CHECK (
		ml_slice_equal (
			ml_str_view (o.block),
			lit (
				"a b \xE2\x94\x82 f\n"
				"0 0 \xE2\x94\x82 0\n"
				"0 1 \xE2\x94\x82 0\n"
				"1 0 \xE2\x94\x82 0\n"
				"1 1 \xE2\x94\x82 1"
			)
		)
	);
	ml_logic_out_free (&o);

	/* table is a name when an operator follows */
	CHECK (gives (&d, "table & true", "table"));
	MlLogicDefVec_free (&d);
}

static void test_errors (void)
{
	MlLogicDefVec d;

	MlLogicDefVec_init (&d);
	CHECK (fails (&d, "a &", "bool_expected_operand"));
	CHECK (fails (&d, "& a", "bool_expected_operand"));
	CHECK (fails (&d, "a b", "bool_expected_operator"));
	CHECK (fails (&d, "(a & b", "bool_unbalanced_parenthesis"));
	CHECK (fails (&d, "a & b)", "bool_unbalanced_parenthesis"));
	CHECK (fails (&d, "a # b", "bool_unexpected_character"));
	CHECK (fails (&d, "true = a", "bool_bad_definition"));
	CHECK (
		fails (
			&d,
			"a & b & c & d & e & f & g & h & i & j & k",
			"bool_too_many_names"
		)
	);
	CHECK (
		fails (
			&d,
			"table a & b & c & d & e & f & g",
			"bool_table_too_big"
		)
	);
	MlLogicDefVec_free (&d);
}

void run_bool_tests (void);

void run_bool_tests (void)
{
	test_values ();
	test_simplify ();
	test_defs ();
	test_table ();
	test_errors ();
}
