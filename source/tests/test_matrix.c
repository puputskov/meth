#include "test.h"

#include <meth/math/matrix.h>
#include <meth/math/vector.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* A matrix from integers, row by row. */
static MlMat make (MlArena *a, uint64_t rows, uint64_t cols, const int64_t *v)
{
	MlMat m = ml_mat_zero (a, rows, cols);

	for (uint64_t i = 0; i < rows * cols; i++)
	{
		ml_mat_set (&m, i / cols, i % cols, ml_rat_from_i64 (a, v [i]));
	}

	return m;
}

static int32_t is_text (MlMat m, const char *expected)
{
	MlStr   s;
	int32_t same;

	ml_str_init (&s);
	same = (
		   ml_mat_format (&s, m) == ML_OK
		&& ml_slice_equal (ml_str_view (s), lit (expected))
	);

	if (!same)
	{
		printf (
			"  matrix is \"%s\" (status %d), expected \"%s\"\n",
			ml_str_cstr (s),
			(int32_t)m.status,
			expected
		);
	}

	ml_str_free (&s);
	return same;
}

static int32_t rat_is (MlRatResult r, const char *expected)
{
	MlStr   s;
	int32_t same;

	ml_str_init (&s);
	same = (
		   r.status == ML_OK
		&& ml_rat_format (&s, r.value) == ML_OK
		&& ml_slice_equal (ml_str_view (s), lit (expected))
	);

	if (!same)
	{
		printf (
			"  number is \"%s\" (status %d), expected \"%s\"\n",
			ml_str_cstr (s),
			(int32_t)r.status,
			expected
		);
	}

	ml_str_free (&s);
	return same;
}

/* 1 when x times y is the identity of size n. */
static int32_t product_is_identity (MlArena *a, MlMat x, MlMat y, uint64_t n)
{
	return ml_mat_equal (a, ml_mat_mul (a, x, y), ml_mat_identity (a, n));
}

static void test_build (void)
{
	MlArena arena;
	MlMat   m;
	MlMat   id;

	ml_arena_init (&arena, 0);
	m = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 3, 4 });
	CHECK (is_text (m, "[[1, 2], [3, 4]]"));
	CHECK (ml_mat_ok (m) && ml_mat_is_square (m));

	id = ml_mat_identity (&arena, 3);
	CHECK (is_text (id, "[[1, 0, 0], [0, 1, 0], [0, 0, 1]]"));

	/* bad sizes */
	CHECK (ml_mat_zero (&arena, 0, 2).status == ML_ERR_INVALID);
	CHECK (ml_mat_zero (&arena, 2, 0).status == ML_ERR_INVALID);
	CHECK (ml_mat_zero (&arena, 65, 1).status == ML_ERR_LIMIT);
	CHECK (ml_mat_ok (ml_mat_zero (&arena, 64, 64)));

	/* cells out of range */
	CHECK (!ml_rat_ok (ml_mat_get (m, 2, 0)));
	ml_mat_set (&m, 5, 5, ml_rat_from_i64 (&arena, 9));
	CHECK (is_text (m, "[[1, 2], [3, 4]]"));

	ml_arena_free (&arena);
}

static int32_t typeset_is (MlMat m, const char *expected)
{
	MlStr   s;
	int32_t same;

	ml_str_init (&s);
	same = (
		   ml_mat_typeset (&s, m) == ML_OK
		&& ml_slice_equal (ml_str_view (s), lit (expected))
	);

	if (!same)
	{
		printf (
			"  typeset is \"%s\", expected \"%s\"\n",
			ml_str_cstr (s),
			expected
		);
	}

	ml_str_free (&s);
	return same;
}

static void test_typeset (void)
{
	MlArena arena;
	MlMat   m;

	ml_arena_init (&arena, 0);

	/* one row: square brackets */
	m = make (&arena, 1, 3, (int64_t [3]){ 1, 2, 3 });
	CHECK (typeset_is (m, "[ 1 2 3 ]"));
	m = make (&arena, 1, 1, (int64_t [1]){ -7 });
	CHECK (typeset_is (m, "[ -7 ]"));

	/* two rows: the corners of a tall bracket */
	m = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 3, 4 });
	CHECK (
		typeset_is (
			m,
			"\xE2\x8E\xA1 1 2 \xE2\x8E\xA4\n"
			"\xE2\x8E\xA3 3 4 \xE2\x8E\xA6"
		)
	);

	/* three rows: a middle piece, and columns right aligned */
	m = make (&arena, 3, 2, (int64_t [6]){ 1, -20, 300, 4, 5, 6 });
	CHECK (
		typeset_is (
			m,
			"\xE2\x8E\xA1   1 -20 \xE2\x8E\xA4\n"
			"\xE2\x8E\xA2 300   4 \xE2\x8E\xA5\n"
			"\xE2\x8E\xA3   5   6 \xE2\x8E\xA6"
		)
	);

	/* fractions take their width too */
	m = ml_mat_zero (&arena, 2, 1);
	ml_mat_set (
		&m,
		0,
		0,
		ml_rat_make (
			&arena,
			ml_big_from_u64 (&arena, 3),
			ml_big_from_u64 (&arena, 2)
		)
	);
	ml_mat_set (&m, 1, 0, ml_rat_from_i64 (&arena, 4));
	CHECK (
		typeset_is (
			m,
			"\xE2\x8E\xA1 3/2 \xE2\x8E\xA4\n"
			"\xE2\x8E\xA3   4 \xE2\x8E\xA6"
		)
	);

	{
		MlStr unused;

		ml_str_init (&unused);
		CHECK (
			ml_mat_typeset (&unused, ml_mat_error (ML_ERR_INVALID))
			== ML_ERR_INVALID
		);
		ml_str_free (&unused);
	}

	ml_arena_free (&arena);
}

/* The text a step version records, or "" when recording failed. */
static int32_t steps_are (MlStepLog *log, const char *expected)
{
	int32_t same;

	ml_steplog_trim (log);
	same = (
		   log->status == ML_OK
		&& ml_slice_equal (ml_str_view (log->text), lit (expected))
	);

	if (!same)
	{
		printf (
			"  steps are:\n%s\n  expected:\n%s\n",
			ml_str_cstr (log->text),
			expected
		);
	}

	return same;
}

static void test_steps (void)
{
	MlArena   arena;
	MlStepLog log;
	MlMat     a;
	MlMat     b;
	MlMat     x;

	ml_arena_init (&arena, 0);

	/* the product, cell by cell */
	ml_steplog_init (&log);
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 3, 4 });
	b = make (&arena, 2, 2, (int64_t [4]){ 5, 6, 7, 8 });
	x = ml_mat_mul_steps (&arena, a, b, &log);
	CHECK (is_text (x, "[[19, 22], [43, 50]]"));
	CHECK (
		steps_are (
			&log,
			"c11 = 1\xC2\xB7" "5 + 2\xC2\xB7" "7 = 19\n"
			"c12 = 1\xC2\xB7" "6 + 2\xC2\xB7" "8 = 22\n"
			"c21 = 3\xC2\xB7" "5 + 4\xC2\xB7" "7 = 43\n"
			"c22 = 3\xC2\xB7" "6 + 4\xC2\xB7" "8 = 50"
		)
	);
	ml_steplog_free (&log);

	/* the determinant: one elimination and the product of the pivots */
	ml_steplog_init (&log);
	CHECK (rat_is (ml_mat_det_steps (&arena, a, &log), "-2"));
	CHECK (
		steps_are (
			&log,
			"Eliminate below the diagonal:\n"
			"\xE2\x8E\xA1 1 2 \xE2\x8E\xA4\n"
			"\xE2\x8E\xA3 3 4 \xE2\x8E\xA6\n"
			"R2 \xE2\x86\x90 R2 \xE2\x88\x92 3\xC2\xB7R1\n"
			"\xE2\x8E\xA1 1  2 \xE2\x8E\xA4\n"
			"\xE2\x8E\xA3 0 -2 \xE2\x8E\xA6\n"
			"determinant = 1\xC2\xB7" "-2 = -2"
		)
	);
	ml_steplog_free (&log);

	/* a swap is named and changes the sign */
	ml_steplog_init (&log);
	a = make (&arena, 2, 2, (int64_t [4]){ 0, 1, 1, 0 });
	CHECK (rat_is (ml_mat_det_steps (&arena, a, &log), "-1"));
	CHECK (strstr (ml_str_cstr (log.text), "swap R2 and R1") != NULL);
	CHECK (
		strstr (ml_str_cstr (log.text), "determinant = -(1\xC2\xB7" "1) = -1")
		!= NULL
	);
	ml_steplog_free (&log);

	/* the reduction names every row operation */
	ml_steplog_init (&log);
	a = make (&arena, 2, 2, (int64_t [4]){ 2, 4, 1, 3 });
	CHECK (
		is_text (
			ml_mat_rref_steps (&arena, a, &log).matrix,
			"[[1, 0], [0, 1]]"
		)
	);
	CHECK (strstr (ml_str_cstr (log.text), "R1 \xE2\x86\x90 R1 / 2") != NULL);
	CHECK (
		strstr (
			ml_str_cstr (log.text),
			"R2 \xE2\x86\x90 R2 \xE2\x88\x92 1\xC2\xB7R1"
		) != NULL
	);
	ml_steplog_free (&log);

	/* a negative factor is added */
	ml_steplog_init (&log);
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 0, -2, 1 });
	CHECK (ml_mat_ok (ml_mat_rref_steps (&arena, a, &log).matrix));
	CHECK (
		strstr (ml_str_cstr (log.text), "R2 \xE2\x86\x90 R2 + 2\xC2\xB7R1")
		!= NULL
	);
	ml_steplog_free (&log);

	/* the inverse and the solution, and a singular matrix says why */
	ml_steplog_init (&log);
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 3, 4 });
	CHECK (ml_mat_ok (ml_mat_inverse_steps (&arena, a, &log)));
	CHECK (strstr (ml_str_cstr (log.text), "identity beside it") != NULL);
	CHECK (strstr (ml_str_cstr (log.text), "the answer") != NULL);
	ml_steplog_free (&log);
	ml_steplog_init (&log);
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 2, 4 });
	CHECK (ml_mat_inverse_steps (&arena, a, &log).status == ML_ERR_DIVZERO);
	CHECK (strstr (ml_str_cstr (log.text), "singular") != NULL);
	ml_steplog_free (&log);

	/* no log, no recording, same answer */
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 3, 4 });
	CHECK (
		ml_mat_equal (
			&arena,
			ml_mat_inverse_steps (&arena, a, NULL),
			ml_mat_inverse (&arena, a)
		)
	);
	CHECK (rat_is (ml_mat_det_steps (&arena, a, NULL), "-2"));

	ml_arena_free (&arena);
}

static void test_arithmetic (void)
{
	MlArena arena;
	MlMat   a;
	MlMat   b;
	MlMat   c;

	ml_arena_init (&arena, 0);
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 3, 4 });
	b = make (&arena, 2, 2, (int64_t [4]){ 5, 6, 7, 8 });

	CHECK (is_text (ml_mat_add (&arena, a, b), "[[6, 8], [10, 12]]"));
	CHECK (is_text (ml_mat_sub (&arena, a, b), "[[-4, -4], [-4, -4]]"));
	CHECK (is_text (ml_mat_mul (&arena, a, b), "[[19, 22], [43, 50]]"));
	CHECK (is_text (ml_mat_mul (&arena, b, a), "[[23, 34], [31, 46]]"));
	CHECK (is_text (ml_mat_neg (&arena, a), "[[-1, -2], [-3, -4]]"));
	CHECK (
		is_text (
			ml_mat_scale (&arena, a, ml_rat_make (
				&arena,
				ml_big_from_u64 (&arena, 1),
				ml_big_from_u64 (&arena, 2)
			)),
			"[[1/2, 1], [3/2, 2]]"
		)
	);
	CHECK (is_text (ml_mat_transpose (&arena, a), "[[1, 3], [2, 4]]"));

	/* non square: 2 x 3 times 3 x 2 */
	c = make (&arena, 2, 3, (int64_t [6]){ 1, 2, 3, 4, 5, 6 });
	CHECK (is_text (ml_mat_transpose (&arena, c), "[[1, 4], [2, 5], [3, 6]]"));
	CHECK (
		is_text (
			ml_mat_mul (&arena, c, ml_mat_transpose (&arena, c)),
			"[[14, 32], [32, 77]]"
		)
	);

	/* size errors, and an error passes through a chain */
	CHECK (ml_mat_add (&arena, a, c).status == ML_ERR_INVALID);
	CHECK (ml_mat_sub (&arena, a, c).status == ML_ERR_INVALID);
	CHECK (ml_mat_mul (&arena, c, c).status == ML_ERR_INVALID);
	CHECK (
		ml_mat_mul (
			&arena,
			ml_mat_add (&arena, a, c),
			b
		).status == ML_ERR_INVALID
	);
	CHECK (ml_mat_transpose (&arena, ml_mat_error (ML_ERR_NOMEM)).status
		== ML_ERR_NOMEM);

	/* equality */
	CHECK (ml_mat_equal (&arena, a, a));
	CHECK (!ml_mat_equal (&arena, a, b));
	CHECK (!ml_mat_equal (&arena, a, c));

	ml_arena_free (&arena);
}

static void test_det_trace (void)
{
	MlArena arena;
	MlMat   a;

	ml_arena_init (&arena, 0);
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 3, 4 });
	CHECK (rat_is (ml_mat_det (&arena, a), "-2"));
	CHECK (rat_is (ml_mat_trace (&arena, a), "5"));

	/* a row swap changes the sign */
	a = make (&arena, 3, 3, (int64_t [9]){ 0, 2, 1, 1, 0, 3, 4, 1, 0 });
	CHECK (rat_is (ml_mat_det (&arena, a), "25"));

	a = make (&arena, 3, 3, (int64_t [9]){ 2, 0, 0, 0, 3, 0, 0, 0, 4 });
	CHECK (rat_is (ml_mat_det (&arena, a), "24"));

	/* singular */
	a = make (&arena, 3, 3, (int64_t [9]){ 1, 2, 3, 4, 5, 6, 7, 8, 9 });
	CHECK (rat_is (ml_mat_det (&arena, a), "0"));

	CHECK (rat_is (ml_mat_det (&arena, ml_mat_identity (&arena, 5)), "1"));

	/* not square */
	a = make (&arena, 2, 3, (int64_t [6]){ 1, 2, 3, 4, 5, 6 });
	CHECK (ml_mat_det (&arena, a).status == ML_ERR_INVALID);
	CHECK (ml_mat_trace (&arena, a).status == ML_ERR_INVALID);
	CHECK (
		ml_mat_det (&arena, ml_mat_error (ML_ERR_LIMIT)).status
		== ML_ERR_LIMIT
	);

	ml_arena_free (&arena);
}

static void test_inverse (void)
{
	MlArena arena;
	MlMat   a;
	MlMat   inv;

	ml_arena_init (&arena, 0);
	a   = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 3, 4 });
	inv = ml_mat_inverse (&arena, a);
	CHECK (is_text (inv, "[[-2, 1], [3/2, -1/2]]"));
	CHECK (product_is_identity (&arena, a, inv, 2));

	/* a 3 x 3 whose inverse needs a row swap */
	a   = make (&arena, 3, 3, (int64_t [9]){ 0, 2, 1, 1, 0, 3, 4, 1, 0 });
	inv = ml_mat_inverse (&arena, a);
	CHECK (ml_mat_ok (inv));
	CHECK (product_is_identity (&arena, a, inv, 3));
	CHECK (product_is_identity (&arena, inv, a, 3));

	/* singular and non square */
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 2, 4 });
	CHECK (ml_mat_inverse (&arena, a).status == ML_ERR_DIVZERO);
	a = make (&arena, 2, 3, (int64_t [6]){ 1, 2, 3, 4, 5, 6 });
	CHECK (ml_mat_inverse (&arena, a).status == ML_ERR_INVALID);

	ml_arena_free (&arena);
}

static void test_power (void)
{
	MlArena arena;
	MlMat   a;

	ml_arena_init (&arena, 0);
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 1, 1, 0 });

	CHECK (is_text (ml_mat_pow (&arena, a, 0), "[[1, 0], [0, 1]]"));
	CHECK (is_text (ml_mat_pow (&arena, a, 1), "[[1, 1], [1, 0]]"));
	CHECK (is_text (ml_mat_pow (&arena, a, 10), "[[89, 55], [55, 34]]"));
	CHECK (is_text (ml_mat_pow (&arena, a, -1), "[[0, 1], [1, -1]]"));
	CHECK (
		product_is_identity (
			&arena,
			ml_mat_pow (&arena, a, -3),
			ml_mat_pow (&arena, a, 3),
			2
		)
	);

	/* the identity to any power is the identity, a singular one has no
	   negative power */
	CHECK (
		is_text (
			ml_mat_pow (&arena, ml_mat_identity (&arena, 2), 1000000),
			"[[1, 0], [0, 1]]"
		)
	);
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 2, 4 });
	CHECK (ml_mat_pow (&arena, a, -1).status == ML_ERR_DIVZERO);
	CHECK (ml_mat_ok (ml_mat_pow (&arena, a, 3)));
	a = make (&arena, 2, 3, (int64_t [6]){ 1, 2, 3, 4, 5, 6 });
	CHECK (ml_mat_pow (&arena, a, 2).status == ML_ERR_INVALID);

	ml_arena_free (&arena);
}

static void test_rref_solve (void)
{
	MlArena arena;
	MlMat   a;
	MlMat   b;
	MlRref  r;

	ml_arena_init (&arena, 0);

	a = make (&arena, 3, 3, (int64_t [9]){ 1, 2, 3, 4, 5, 6, 7, 8, 9 });
	r = ml_mat_rref (&arena, a);
	CHECK (r.rank == 2);
	CHECK (is_text (r.matrix, "[[1, 0, -1], [0, 1, 2], [0, 0, 0]]"));

	r = ml_mat_rref (&arena, ml_mat_identity (&arena, 3));
	CHECK (r.rank == 3);

	a = make (&arena, 2, 4, (int64_t [8]){ 0, 0, 1, 2, 0, 0, 2, 4 });
	r = ml_mat_rref (&arena, a);
	CHECK (r.rank == 1);
	CHECK (is_text (r.matrix, "[[0, 0, 1, 2], [0, 0, 0, 0]]"));

	/* x + 2y = 5, 3x + 4y = 6 */
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 3, 4 });
	b = make (&arena, 2, 1, (int64_t [2]){ 5, 6 });
	CHECK (is_text (ml_mat_solve (&arena, a, b), "[[-4], [9/2]]"));

	/* several right hand sides at once */
	b = make (&arena, 2, 2, (int64_t [4]){ 1, 0, 0, 1 });
	CHECK (is_text (ml_mat_solve (&arena, a, b), "[[-2, 1], [3/2, -1/2]]"));

	/* singular, wrong sizes */
	a = make (&arena, 2, 2, (int64_t [4]){ 1, 2, 2, 4 });
	b = make (&arena, 2, 1, (int64_t [2]){ 1, 2 });
	CHECK (ml_mat_solve (&arena, a, b).status == ML_ERR_DIVZERO);
	b = make (&arena, 3, 1, (int64_t [3]){ 1, 2, 3 });
	CHECK (ml_mat_solve (&arena, a, b).status == ML_ERR_INVALID);

	ml_arena_free (&arena);
}

static MlMat vec3 (MlArena *a, int64_t x, int64_t y, int64_t z)
{
	int64_t v [3] = { x, y, z };

	return ml_vec_from_i64 (a, 3, v);
}

static MlMat vec2 (MlArena *a, int64_t x, int64_t y)
{
	int64_t v [2] = { x, y };

	return ml_vec_from_i64 (a, 2, v);
}

static void test_vectors (void)
{
	MlArena   arena;
	MlMat     a;
	MlMat     b;
	MlMat     short_v;
	MlMat     id3;
	MlVecNorm n;
	MlStr     s;

	ml_arena_init (&arena, 0);
	ml_str_init (&s);
	a = vec3 (&arena, 1, 2, 3);
	b = vec3 (&arena, 4, 5, 6);

	CHECK (is_text (a, "[[1], [2], [3]]"));
	CHECK (ml_vec_is_vector (a));
	CHECK (!ml_vec_is_vector (ml_mat_identity (&arena, 2)));
	CHECK (rat_is (ml_vec_dot (&arena, a, b), "32"));
	CHECK (is_text (ml_vec_cross (&arena, a, b), "[[-3], [6], [-3]]"));
	CHECK (rat_is (ml_vec_norm_squared (&arena, a), "14"));

	/* the cross product is perpendicular to both, and anticommutative */
	CHECK (
		rat_is (
			ml_vec_dot (&arena, ml_vec_cross (&arena, a, b), a),
			"0"
		)
	);
	CHECK (
		ml_mat_equal (
			&arena,
			ml_vec_cross (&arena, b, a),
			ml_mat_neg (&arena, ml_vec_cross (&arena, a, b))
		)
	);

	/* a rational length is exact, any other comes as the square */
	n = ml_vec_norm (&arena, vec2 (&arena, 3, 4));
	CHECK (n.status == ML_OK && n.exact);
	CHECK (ml_rat_format (&s, n.value) == ML_OK);
	CHECK (strcmp (ml_str_cstr (s), "5") == 0);
	n = ml_vec_norm (&arena, a);
	CHECK (n.status == ML_OK && !n.exact);
	ml_str_clear (&s);
	CHECK (ml_rat_format (&s, n.value) == ML_OK);
	CHECK (strcmp (ml_str_cstr (s), "14") == 0);

	/* a matrix times a vector is a vector */
	CHECK (
		is_text (
			ml_mat_mul (
				&arena,
				make (&arena, 2, 3, (int64_t [6]){ 1, 0, 2, 0, 1, 3 }),
				a
			),
			"[[7], [11]]"
		)
	);

	/* wrong shapes */
	short_v = vec2 (&arena, 1, 2);
	id3     = ml_mat_identity (&arena, 3);
	CHECK (ml_vec_dot (&arena, a, short_v).status == ML_ERR_INVALID);
	CHECK (ml_vec_dot (&arena, a, id3).status == ML_ERR_INVALID);
	CHECK (
		ml_vec_cross (&arena, short_v, short_v).status == ML_ERR_INVALID
	);
	CHECK (ml_vec_norm (&arena, id3).status == ML_ERR_INVALID);
	CHECK (
		ml_vec_norm (&arena, ml_mat_error (ML_ERR_NOMEM)).status
		== ML_ERR_NOMEM
	);

	ml_str_free (&s);
	ml_arena_free (&arena);
}

void run_matrix_tests (void);

void run_matrix_tests (void)
{
	test_build ();
	test_typeset ();
	test_steps ();
	test_arithmetic ();
	test_det_trace ();
	test_inverse ();
	test_power ();
	test_rref_solve ();
	test_vectors ();
}
