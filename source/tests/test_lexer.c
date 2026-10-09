#include "test.h"

#include <matlog/parse/lexer.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* The kinds of all tokens as short text: "number + number end". */
static int32_t kinds_are (const char *source, const char *expected)
{
	MlLexResult lexed = ml_lex (lit (source));
	MlStr       s;
	int32_t     same;

	ml_str_init (&s);

	for (uint64_t i = 0; i < lexed.tokens.len; i++)
	{
		if (i > 0)
		{
			ml_str_append (&s, lit (" "));
		}

		ml_str_append (
			&s,
			lit (ml_token_kind_name (lexed.tokens.data [i].kind))
		);
	}

	same = (
		   lexed.result == ML_OK
		&& ml_slice_equal (ml_str_view (s), lit (expected))
	);

	if (!same)
	{
		printf (
			"  \"%s\" gives \"%s\", expected \"%s\"\n",
			source,
			ml_str_cstr (s),
			expected
		);
	}

	ml_str_free (&s);
	MlTokenVec_free (&lexed.tokens);
	return same;
}

/* Does token index idx cover exactly the text expected? */
static int32_t token_is (
	const char *source,
	uint64_t   idx,
	uint64_t   start,
	uint64_t   len
)
{
	MlLexResult lexed = ml_lex (lit (source));
	MlToken     t     = MlTokenVec_get (&lexed.tokens, idx);
	int32_t     same  = t.start == start && t.len == len;

	if (!same)
	{
		printf (
			"  \"%s\" token %u is [%u, +%u), expected [%u, +%u)\n",
			source,
			(uint32_t)idx,
			(uint32_t)t.start,
			(uint32_t)t.len,
			(uint32_t)start,
			(uint32_t)len
		);
	}

	MlTokenVec_free (&lexed.tokens);
	return same;
}

static void test_basic (void)
{
	CHECK (kinds_are ("", "end"));
	CHECK (kinds_are ("   \t ", "end"));
	CHECK (kinds_are ("1+2", "number + number end"));
	CHECK (kinds_are ("1 + 2", "number + number end"));
	CHECK (
		kinds_are ("12 * 3 / 4 - 5", "number * number / number - number end")
	);
	CHECK (kinds_are ("2^3!", "number ^ number ! end"));
	CHECK (kinds_are ("f(a, b)", "name ( name , name ) end"));
	CHECK (kinds_are ("x = 3", "name = number end"));
	CHECK (kinds_are ("f(x) = 2x", "name ( name ) = number name end"));
	CHECK (kinds_are ("sqrt(x)", "name ( name ) end"));
}

static void test_numbers (void)
{
	CHECK (kinds_are ("0", "number end"));
	CHECK (kinds_are ("123", "number end"));
	CHECK (kinds_are ("3.14", "number end"));
	CHECK (kinds_are (".5", "number end"));
	CHECK (kinds_are ("007.50", "number end"));
	CHECK (token_is  ("3.14", 0, 0, 4));
	CHECK (token_is  (".5", 0, 0, 2));
	CHECK (token_is  ("12 + 3.5", 2, 5, 3));

	/* a dot without digits after it is not part of the number */
	CHECK (kinds_are ("5.", "number error end"));
	CHECK (token_is  ("5.", 0, 0, 1));
	CHECK (token_is  ("5.", 1, 1, 1));
	CHECK (kinds_are (".", "error end"));
	CHECK (kinds_are ("1.2.3", "number number end"));
	CHECK (token_is  ("1.2.3", 1, 3, 2));
	CHECK (kinds_are (".5.3", "number number end"));

	/* no exponent syntax: 2e3 is a number followed by a name */
	CHECK (kinds_are ("2e3", "number name end"));
	CHECK (token_is  ("2e3", 1, 1, 2));
}

static void test_names (void)
{
	CHECK (kinds_are ("x", "name end"));
	CHECK (kinds_are ("xy2", "name end"));
	CHECK (token_is  ("xy2", 0, 0, 3));
	CHECK (kinds_are ("_a_1", "name end"));
	CHECK (kinds_are ("Alpha", "name end"));
	CHECK (kinds_are ("2x", "number name end"));
	CHECK (kinds_are ("x2y", "name end"));
	CHECK (kinds_are ("a b", "name name end"));

	/* unicode letters */
	CHECK (kinds_are ("\xCF\x80", "name end"));
	CHECK (token_is  ("\xCF\x80", 0, 0, 2));
	CHECK (kinds_are ("2\xCF\x80", "number name end"));
	CHECK (kinds_are ("\xCE\xB1\xCE\xB2", "name end"));
	CHECK (kinds_are ("\xC3\xA4" "bc", "name end"));
	CHECK (token_is  ("12 + \xCF\x80", 2, 5, 2));
}

static void test_unicode_spellings (void)
{
	/* the same token kinds as the ASCII forms */
	CHECK (kinds_are ("2\xC3\x97" "3", "number * number end"));
	CHECK (kinds_are ("2\xC2\xB7" "3", "number * number end"));
	CHECK (kinds_are ("2\xE2\x8B\x85" "3", "number * number end"));
	CHECK (kinds_are ("6\xC3\xB7" "3", "number / number end"));
	CHECK (kinds_are ("6\xE2\x88\x95" "3", "number / number end"));
	CHECK (kinds_are ("6\xE2\x88\x92" "3", "number - number end"));
	CHECK (kinds_are ("\xE2\x88\x9A" "x", "sqrt name end"));
	CHECK (kinds_are ("\xE2\x88\x9A(x+1)", "sqrt ( name + number ) end"));
	CHECK (kinds_are (
		"2\xC3\x97" "3\xC3\xB7" "4\xE2\x88\x92" "5\xC2\xB7" "6",
		"number * number / number - number * number end"
	));

	/* ranges are in bytes */
	CHECK (token_is ("2\xC3\x97" "3", 1, 1, 2));
	CHECK (token_is ("2\xC3\x97" "3", 2, 3, 1));
	CHECK (token_is ("\xE2\x88\x9A" "x", 0, 0, 3));
	CHECK (token_is ("\xE2\x88\x9A" "x", 1, 3, 1));
}

static void test_superscripts (void)
{
	MlStr s;

	/* a run of superscript characters is one token */
	CHECK (kinds_are ("x\xC2\xB2", "name superscript end"));
	CHECK (kinds_are ("x\xC2\xB9\xE2\x81\xB0", "name superscript end"));
	CHECK (token_is  ("x\xC2\xB9\xE2\x81\xB0", 1, 1, 5));
	CHECK (kinds_are ("x\xE2\x81\xBB\xC2\xB9", "name superscript end"));
	CHECK (kinds_are (
		"(a+b)\xC2\xB3",
		"( name + name ) superscript end"
	));
	CHECK (kinds_are (
		"x\xE2\x81\xB4 y\xE2\x81\xB5",
		"name superscript name superscript end"
	));

	/* the digits */
	ml_str_init (&s);
	CHECK (ml_superscript_to_ascii (&s, lit ("\xC2\xB2")) == ML_OK);
	CHECK (strcmp                  (ml_str_cstr (s), "2") == 0);
	ml_str_clear (&s);
	CHECK (
		ml_superscript_to_ascii (
			&s,
			lit ("\xE2\x81\xBB\xC2\xB9\xE2\x81\xB0")
		) == ML_OK
	);
	CHECK (strcmp (ml_str_cstr (s), "-10") == 0);
	ml_str_clear (&s);
	CHECK (
		ml_superscript_to_ascii (&s, lit ("\xC2\xB9\xC2\xB2\xC2\xB3")) == ML_OK
	);
	CHECK (strcmp (ml_str_cstr (s), "123") == 0);
	ml_str_clear (&s);
	CHECK (ml_superscript_to_ascii (
		&s,
		lit (
			"\xE2\x81\xB0\xC2\xB9\xC2\xB2\xC2\xB3\xE2\x81\xB4"
			"\xE2\x81\xB5\xE2\x81\xB6\xE2\x81\xB7\xE2\x81\xB8\xE2\x81\xB9"
		)
	) == ML_OK);
	CHECK (strcmp (ml_str_cstr (s), "0123456789") == 0);

	/* not a superscript run */
	ml_str_clear (&s);
	CHECK (ml_superscript_to_ascii (&s, lit ("")) == ML_ERR_PARSE);
	CHECK (ml_superscript_to_ascii (&s, lit ("2")) == ML_ERR_PARSE);
	CHECK (ml_superscript_to_ascii (&s, lit ("\xC2\xB2" "x")) == ML_ERR_PARSE);
	CHECK (
		ml_superscript_to_ascii (
			&s,
			lit ("\xC2\xB2\xE2\x81\xBB\xC2\xB3")
		) == ML_ERR_PARSE
	);
	ml_str_free (&s);
}

static void test_errors (void)
{
	CHECK (kinds_are ("a $ b", "name error name end"));
	CHECK (token_is  ("a $ b", 1, 2, 1));
	CHECK (kinds_are ("{1}", "error number error end"));
	CHECK (kinds_are ("1 < 2", "number error number end"));
	CHECK (kinds_are ("1 % 2", "number error number end"));

	/* one error token per bad character, lexing goes on */
	CHECK (kinds_are ("$$", "error error end"));
	CHECK (kinds_are ("1 # 2 @ 3", "number error number error number end"));

	/* a unicode character outside the syntax covers its whole encoding */
	CHECK (kinds_are ("a \xE2\x89\xA4 b", "name error name end"));
	CHECK (token_is  ("a \xE2\x89\xA4 b", 1, 2, 3));
	CHECK (kinds_are ("\xE2\x88\x9E", "error end"));

	/* invalid UTF-8 is one error token per byte */
	{
		MlLexResult lexed = ml_lex (ml_slice ("a\xFF" "b", 3));

		CHECK (lexed.result == ML_OK);
		CHECK (lexed.tokens.len == 4);
		CHECK (lexed.tokens.data [1].kind == ML_TOK_ERROR);
		CHECK (lexed.tokens.data [1].start == 1);
		CHECK (lexed.tokens.data [1].len == 1);
		MlTokenVec_free (&lexed.tokens);
	}

	/* the end token sits at the end of the text */
	CHECK (token_is ("ab", 1, 2, 0));
	CHECK (token_is ("", 0, 0, 0));
	CHECK (token_is ("  ", 0, 2, 0));
	CHECK (strcmp   (ml_token_kind_name (ML_TOK_COUNT), "unknown") == 0);
}

static void test_token_text (void)
{
	MlSlice     source = lit ("12 + foo");
	MlLexResult lexed  = ml_lex (source);

	CHECK (lexed.tokens.len == 4);
	CHECK (
		ml_slice_equal (
			ml_token_text (source, lexed.tokens.data [0]),
			lit ("12")
		)
	);
	CHECK (
		ml_slice_equal (
			ml_token_text (source, lexed.tokens.data [1]),
			lit ("+")
		)
	);
	CHECK (
		ml_slice_equal (
			ml_token_text (source, lexed.tokens.data [2]),
			lit ("foo")
		)
	);
	CHECK (ml_token_text (source, lexed.tokens.data [3]).len == 0);
	MlTokenVec_free (&lexed.tokens);
}

static void test_brackets (void)
{
	CHECK (kinds_are ("[1, 2]", "[ number , number ] end"));
	CHECK (
		kinds_are (
			"[[1,2],[3,4]]",
			"[ [ number , number ] , [ number , number ] ] end"
		)
	);
	CHECK (kinds_are ("A^T", "name ^ name end"));
}

void run_lexer_tests (void)
{
	test_basic ();
	test_numbers ();
	test_names ();
	test_unicode_spellings ();
	test_superscripts ();
	test_errors ();
	test_token_text ();
	test_brackets ();
}
