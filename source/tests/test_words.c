#include "test.h"

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

void run_words_tests (void);

void run_words_tests (void)
{
	test_kinds ();
	test_ids ();
	test_rows ();
}
