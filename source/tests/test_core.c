#include "test.h"

#include <matlog/core/err.h>
#include <matlog/core/mem.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>
#include <matlog/core/utf8.h>
#include <matlog/core/vec.h>

#include <stdint.h>
#include <string.h>

ML_VEC_DEFINE (IntVec, int32_t)

typedef struct Pair
{
	int32_t a;
	double  b;
} Pair;

ML_VEC_DEFINE (PairVec, Pair)

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static void test_slice (void)
{
	MlSlice s   = lit ("hello");
	MlSlice sub = ml_slice_sub (s, 1, 3);

	CHECK (s.len == 5);
	CHECK (ml_slice_equal  (sub, lit ("ell")));
	CHECK (ml_slice_equal  (ml_slice_sub (s, 3, 100), lit ("lo")));
	CHECK (ml_slice_sub    (s, 9, 1).len == 0);
	CHECK (ml_slice_cstr   (NULL).len == 0);
	CHECK (!ml_slice_equal (lit ("a"), lit ("b")));
	CHECK (ml_slice_equal  (lit (""), ml_slice (NULL, 0)));
}

static void test_arena (void)
{
	MlArena a;
	int32_t *xs;
	double  *d;
	char    *s;
	void    *big;

	ml_arena_init (&a, 128);

	CHECK (ml_arena_alloc (&a, 0, 1) == NULL);
	CHECK (ml_arena_alloc (&a, 8, 3) == NULL);

	xs = ML_ARENA_NEW_ARRAY (&a, int32_t, 10);
	CHECK (xs != NULL);
	for (int32_t i = 0; i < 10; i++)
	{
		CHECK (xs [i] == 0);
		xs [i] = i;
	}

	ml_arena_alloc (&a, 1, 1);
	d = ML_ARENA_NEW (&a, double);
	CHECK (d != NULL);
	CHECK (((uintptr_t)d % _Alignof (double)) == 0);

	s = ml_arena_strdup (&a, ml_slice ("hello world", 5));
	CHECK (s != NULL && strcmp (s, "hello") == 0);

	/* larger than a block, then the earlier data must still be intact */
	big = ml_arena_alloc (&a, 1000, 16);
	CHECK (big != NULL);
	CHECK (((uintptr_t)big % 16) == 0);
	memset (big, 0xAB, 1000);
	for (int32_t i = 0; i < 10; i++)
	{
		CHECK (xs [i] == i);
	}

	/* many small allocations spill across blocks */
	for (int32_t i = 0; i < 1000; i++)
	{
		CHECK (ml_arena_alloc (&a, 24, 8) != NULL);
	}

	ml_arena_reset (&a);
	CHECK (ml_arena_alloc (&a, 16, 8) != NULL);
	ml_arena_free (&a);
	CHECK (a.head == NULL);
}

static void test_vec (void)
{
	IntVec  v;
	PairVec pv;
	Pair    p;

	IntVec_init (&v);
	CHECK (v.len == 0);
	CHECK (IntVec_get (&v, 0) == 0);
	CHECK (IntVec_pop (&v) == ML_ERR_RANGE);

	for (int32_t i = 0; i < 100; i++)
	{
		CHECK (IntVec_push (&v, i) == ML_OK);
	}

	CHECK (v.len == 100);
	CHECK (IntVec_get (&v, 42) == 42);
	CHECK (IntVec_get (&v, 100) == 0);

	CHECK (IntVec_insert (&v, 0, -1) == ML_OK);
	CHECK (v.len == 101 && IntVec_get (&v, 0) == -1 && IntVec_get (&v, 1) == 0);
	CHECK (IntVec_insert (&v, 102, 7) == ML_ERR_RANGE);

	CHECK (IntVec_remove (&v, 0) == ML_OK);
	CHECK (v.len == 100 && IntVec_get (&v, 0) == 0);
	CHECK (IntVec_remove (&v, 100) == ML_ERR_RANGE);

	CHECK (IntVec_set (&v, 5, 555) == ML_OK);
	CHECK (IntVec_get (&v, 5) == 555);
	CHECK (IntVec_set (&v, 100, 1) == ML_ERR_RANGE);

	CHECK (IntVec_pop (&v) == ML_OK);
	CHECK (v.len == 99);

	IntVec_clear (&v);
	CHECK (v.len == 0);
	IntVec_free (&v);
	CHECK (v.data == NULL);

	/* struct elements travel by value */
	PairVec_init (&pv);
	p.a = 3;
	p.b = 1.5;
	CHECK (PairVec_push (&pv, p) == ML_OK);
	p.a = 9;
	CHECK (PairVec_get (&pv, 0).a == 3 && PairVec_get (&pv, 0).b == 1.5);
	CHECK (PairVec_get (&pv, 1).a == 0);
	PairVec_free (&pv);
}

static void test_utf8 (void)
{
	/* "π√x≤" : 2, 3, 1, 3 bytes */
	MlSlice s = lit (
		"\xCF\x80\xE2\x88\x9A" "x" "\xE2\x89\xA4"
	);
	MlDecoded             d;
	MlEncoded             e;
	static const uint32_t cps [] =
	{
		0,
		0x7F,
		0x80,
		0x7FF,
		0x800,
		0xFFFF,
		0x10000,
		0x10FFFF
	};

	CHECK (s.len == 9);
	CHECK (ml_utf8_valid (s));
	CHECK (ml_utf8_count (s) == 4);
	CHECK (ml_utf8_width (s) == 4);

	d = ml_utf8_decode (s);
	CHECK (d.size == 2 && d.cp == 0x03C0);
	d = ml_utf8_decode (ml_slice_sub (s, 2, 100));
	CHECK (d.size == 3 && d.cp == 0x221A);
	CHECK (ml_utf8_decode (ml_slice (s.data, 0)).size == 0);

	CHECK (ml_utf8_next (s, 0) == 2);
	CHECK (ml_utf8_next (s, 2) == 5);
	CHECK (ml_utf8_next (s, s.len) == s.len);
	CHECK (ml_utf8_prev (s, s.len) == 6);
	CHECK (ml_utf8_prev (s, 6) == 5);
	CHECK (ml_utf8_prev (s, 5) == 2);
	CHECK (ml_utf8_prev (s, 2) == 0);
	CHECK (ml_utf8_prev (s, 0) == 0);

	/* encode / decode round trip over the interesting boundaries */
	for (uint64_t i = 0; i < ml_count (cps); i++)
	{
		e = ml_utf8_encode (cps [i]);
		CHECK (e.size >= 1);
		d = ml_utf8_decode (ml_slice (e.bytes, e.size));
		CHECK (d.size == e.size && d.cp == cps [i]);
	}

	CHECK (ml_utf8_encode (0xD800).size == 0);
	CHECK (ml_utf8_encode (0x110000).size == 0);

	/*
	 * malformed: lone continuation, truncated, overlong, surrogate, too large
	 */
	CHECK (!ml_utf8_valid (ml_slice ("\x80", 1)));
	CHECK (!ml_utf8_valid (ml_slice ("\xE2\x88", 2)));
	CHECK (!ml_utf8_valid (ml_slice ("\xC0\xAF", 2)));
	CHECK (!ml_utf8_valid (ml_slice ("\xED\xA0\x80", 3)));
	CHECK (!ml_utf8_valid (ml_slice ("\xF4\x90\x80\x80", 4)));
	d = ml_utf8_decode (ml_slice ("\x80", 1));
	CHECK (d.size == 1 && d.cp == ML_CP_REPLACEMENT);
	/* a real U+FFFD is valid */
	CHECK (ml_utf8_valid (ml_slice ("\xEF\xBF\xBD", 3)));

	/* widths */
	CHECK (ml_cp_width ('a') == 1);
	CHECK (ml_cp_width   (0x0301) == 0);  /* combining acute */
	CHECK (ml_cp_width   (0x4E2D) == 2);  /* CJK */
	CHECK (ml_cp_width   (0x1F600) == 2); /* emoji */
	CHECK (ml_cp_width   ('\n') == 0);
	CHECK (ml_utf8_width (ml_slice ("e\xCC\x81", 3)) == 1);
}

static void test_str (void)
{
	MlStr s;

	ml_str_init (&s);
	CHECK (strcmp (ml_str_cstr (s), "") == 0);
	CHECK (s.len == 0);

	CHECK (ml_str_append (&s, lit ("hello")) == ML_OK);
	CHECK (ml_str_append (&s, lit (" world!")) == ML_OK);
	CHECK (strcmp        (ml_str_cstr (s), "hello world!") == 0);

	CHECK (ml_str_insert (&s, 0, lit (">> ")) == ML_OK);
	CHECK (ml_str_insert (&s, s.len, lit ("<")) == ML_OK);
	CHECK (strcmp        (ml_str_cstr (s), ">> hello world!<") == 0);
	CHECK (ml_str_insert (&s, s.len + 1, lit ("x")) == ML_ERR_RANGE);

	CHECK (ml_str_erase   (&s, 0, 3) == ML_OK);
	CHECK (ml_str_erase   (&s, 11, 100) == ML_OK);
	CHECK (strcmp         (ml_str_cstr (s), "hello world") == 0);
	CHECK (ml_str_erase   (&s, 99, 1) == ML_ERR_RANGE);
	CHECK (ml_slice_equal (ml_str_view (s), lit ("hello world")));

	ml_str_clear (&s);
	CHECK (s.len == 0 && strcmp (ml_str_cstr (s), "") == 0);

	CHECK (ml_str_append_cp (&s, 0x03C0) == ML_OK);
	CHECK (ml_str_append_cp (&s, 0x221A) == ML_OK);
	CHECK (s.len == 5 && ml_utf8_count (ml_str_view (s)) == 2);
	CHECK (ml_str_append_cp (&s, 0xD800) == ML_ERR_UTF8);

	ml_str_clear (&s);
	CHECK (ml_str_appendf (&s, "%d-%s-%05.1f", 42, "ab", 3.14159) == ML_OK);
	CHECK (strcmp         (ml_str_cstr (s), "42-ab-003.1") == 0);

	/* growth across many appends */
	ml_str_clear (&s);
	for (int32_t i = 0; i < 10000; i++)
	{
		CHECK (ml_str_append (&s, lit ("ab")) == ML_OK);
	}

	CHECK (s.len == 20000 && s.data [s.len] == '\0');

	ml_str_free (&s);
	CHECK (s.data == NULL && s.len == 0);
}

static void test_err (void)
{
	CHECK (strcmp        (ml_result_str (ML_OK), "ok") == 0);
	CHECK (ml_result_str (ML_ERR_NOMEM) != NULL);
	CHECK (strcmp        (ml_result_str ((MlResult)999), "unknown error") == 0);
}

void run_core_tests (void)
{
	test_slice ();
	test_arena ();
	test_vec ();
	test_utf8 ();
	test_str ();
	test_err ();
}
