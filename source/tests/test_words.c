#include "test.h"

#include <matlog/math/eval.h>
#include <matlog/parse/words.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static MlWord find (const char *name)
{
	return ml_word_find (ml_slice_cstr (name));
}

static void test_kinds (void)
{
	CHECK (find ("include").kind == ML_WORD_KEYWORD);
	CHECK (find ("lambda").kind  == ML_WORD_KEYWORD);
	CHECK (find ("let").kind     == ML_WORD_KEYWORD);
	CHECK (find ("and").kind     == ML_WORD_OPERATOR);
	CHECK (find ("or").kind      == ML_WORD_OPERATOR);
	CHECK (find ("to").kind      == ML_WORD_OPERATOR);
	CHECK (find ("pi").kind      == ML_WORD_CONSTANT);
	CHECK (find ("e").kind       == ML_WORD_CONSTANT);
	CHECK (find ("true").kind    == ML_WORD_CONSTANT);
	CHECK (find ("false").kind   == ML_WORD_CONSTANT);
	CHECK (find ("inf").kind     == ML_WORD_SYMBOL);

	/* other names are no words, also near misses */
	CHECK (find ("").kind    == ML_WORD_NONE);
	CHECK (find ("x").kind   == ML_WORD_NONE);
	CHECK (find ("Pi").kind  == ML_WORD_NONE);
	CHECK (find ("pie").kind == ML_WORD_NONE);
	CHECK (find ("an").kind  == ML_WORD_NONE);
}

static void test_ids (void)
{
	/* the two spellings of pi are one word */
	CHECK (find ("pi").id         == ML_WORD_ID_PI);
	CHECK (find ("\xCF\x80").id   == ML_WORD_ID_PI);
	CHECK (find ("\xCF\x80").kind == ML_WORD_CONSTANT);
	CHECK (find ("x").id          == ML_WORD_ID_NONE);

	CHECK (ml_word_is (ml_slice_cstr ("and"), ML_WORD_ID_AND));
	CHECK (!ml_word_is (ml_slice_cstr ("and"), ML_WORD_ID_OR));
	CHECK (ml_word_is (ml_slice_cstr ("\xCF\x80"), ML_WORD_ID_PI));
	CHECK (!ml_word_is (ml_slice_cstr ("x"), ML_WORD_ID_E));

	/* the ASCII spelling of a word */
	CHECK (ml_slice_equal (ml_word_name (ML_WORD_ID_PI), lit ("pi")));
	CHECK (ml_slice_equal (ml_word_name (ML_WORD_ID_INF), lit ("inf")));
	CHECK (ml_word_name (ML_WORD_ID_NONE).len == 0);
}

static void test_rows (void)
{
	uint64_t listed = 0;

	/* every row finds itself, and the first row of an id is the ASCII one */
	for (uint64_t i = 0; i < ml_word_count (); i++)
	{
		MlSlice s = ml_word_spelling (i);
		MlWord  w = ml_word_at (i);

		CHECK (s.len > 0);
		CHECK (w.kind != ML_WORD_NONE && w.id != ML_WORD_ID_NONE);
		CHECK (ml_word_find (s).id == w.id);
		CHECK (ml_word_find (s).kind == w.kind);
		CHECK (ml_slice_equal (ml_word_name (w.id), s) || s.data [0] < 0);
		listed += (uint64_t)ml_word_listed (i);
	}

	/* the completion offers pi and e, not the Greek letter or the rest */
	CHECK (listed == 2);
	CHECK (ml_word_count () > 0);
	CHECK (ml_word_spelling (ml_word_count ()).len == 0);
	CHECK (ml_word_at (ml_word_count ()).kind == ML_WORD_NONE);
	CHECK (!ml_word_listed (ml_word_count ()));
}

/* 1 when the evaluator lists a function of this name. */
static int32_t is_listed_function (MlSlice name)
{
	int32_t found = 0;

	for (uint64_t i = 0; i < ml_function_name_count () && !found; i++)
	{
		found = ml_slice_equal (ml_function_name_at (i), name);
	}

	return found;
}

static void test_functions (void)
{
	/* the words of the engines are functions the evaluator knows, so the
	   spelling in the table cannot drift from the one in the builtin tables */
	for (uint64_t i = 0; i < ml_word_count (); i++)
	{
		MlWord w = ml_word_at (i);

		if (w.kind == ML_WORD_FUNCTION || w.kind == ML_WORD_SYMBOLIC)
		{
			CHECK (is_listed_function (ml_word_spelling (i)));
		}
	}

	/* the symbolic functions are listed by the evaluator from the table */
	CHECK (ml_word_kind_count (ML_WORD_SYMBOLIC) == 6);
	CHECK (
		ml_slice_equal (ml_word_kind_at (ML_WORD_SYMBOLIC, 0), lit ("simplify"))
	);
	CHECK (
		ml_slice_equal (ml_word_kind_at (ML_WORD_SYMBOLIC, 5), lit ("limit"))
	);
	CHECK (ml_word_kind_at (ML_WORD_SYMBOLIC, 6).len == 0);
	CHECK (is_listed_function (lit ("diff")));
	CHECK (ml_word_kind_count (ML_WORD_COMMAND) == 2);

	CHECK (find ("sin").kind == ML_WORD_FUNCTION);
	CHECK (find ("diff").kind == ML_WORD_SYMBOLIC);
	CHECK (find ("table").id == ML_WORD_ID_TABLE);
	CHECK (ML_WORD_IS (lit ("trace"), TRACE));
	CHECK (ml_slice_equal (ML_WORD_NAME (SQRT), lit ("sqrt")));
}

/* The very first look up after the names were released must still work. */
static void test_first_lookup (void)
{
	ml_words_release ();
	ml_eval_release ();
	ml_names_release ();
	CHECK (find ("sin").id == ML_WORD_ID_SIN);

	ml_words_release ();
	ml_eval_release ();
	ml_names_release ();
	CHECK (ML_WORD_IS (lit ("pi"), PI));

	ml_words_release ();
	ml_eval_release ();
	ml_names_release ();
	CHECK (is_listed_function (lit ("len")));
	CHECK (find ("zzz_not_a_word").kind == ML_WORD_NONE);
}

void run_words_tests (void);

void run_words_tests (void)
{
	test_kinds ();
	test_ids ();
	test_rows ();
	test_functions ();
	test_first_lookup ();
}
