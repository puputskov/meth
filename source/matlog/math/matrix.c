#include <matlog/math/matrix.h>

#include <matlog/core/mem.h>

MlMat ml_mat_error (MlResult status)
{
	MlMat m =
	{
		.status = status,
		.rows   = 0,
		.cols   = 0,
		.cells  = NULL
	};

	return m;
}

MlMat ml_mat_zero (MlArena *arena, uint64_t rows, uint64_t cols)
{
	MlMat m = ml_mat_error (ML_OK);

	if (rows == 0 || cols == 0)
	{
		m = ml_mat_error (ML_ERR_INVALID);
	}

	else if (rows > ML_MAT_MAX_DIM || cols > ML_MAT_MAX_DIM)
	{
		m = ml_mat_error (ML_ERR_LIMIT);
	}

	else
	{
		m.cells = ML_ARENA_NEW_ARRAY (arena, MlRat, rows * cols);

		if (m.cells == NULL)
		{
			m = ml_mat_error (ML_ERR_NOMEM);
		}

		else
		{
			m.rows = rows;
			m.cols = cols;

			for (uint64_t i = 0; i < rows * cols; i++)
			{
				m.cells [i] = ml_rat_zero (arena);
			}
		}
	}

	return m;
}

MlMat ml_mat_identity (MlArena *arena, uint64_t n)
{
	MlMat m = ml_mat_zero (arena, n, n);

	for (uint64_t i = 0; i < n && m.status == ML_OK; i++)
	{
		m.cells [i * n + i] = ml_rat_from_i64 (arena, 1);
	}

	return m;
}

MlRat ml_mat_get (MlMat m, uint64_t row, uint64_t col)
{
	return ml_choose (
		m.status == ML_OK && row < m.rows && col < m.cols,
		m.cells [row * m.cols + col],
		ml_rat_error (ML_ERR_RANGE)
	);
}

void ml_mat_set (MlMat *m, uint64_t row, uint64_t col, MlRat value)
{
	if (m->status == ML_OK && row < m->rows && col < m->cols)
	{
		m->cells [row * m->cols + col] = value;
	}
}

int32_t ml_mat_ok (MlMat m)
{
	return m.status == ML_OK;
}

int32_t ml_mat_is_square (MlMat m)
{
	return m.status == ML_OK && m.rows == m.cols;
}

/*
 * The matrix with the status of its first failed cell, if any: a failed
 * number inside a cell would otherwise hide in the table.
 */
static MlMat checked (MlMat m)
{
	MlMat out = m;

	for (uint64_t i = 0; i < m.rows * m.cols && out.status == ML_OK; i++)
	{
		if (!ml_rat_ok (m.cells [i]))
		{
			out = ml_mat_error (ml_rat_status (m.cells [i]));
		}
	}

	return out;
}

int32_t ml_mat_equal (MlArena *arena, MlMat a, MlMat b)
{
	int32_t same = (
		   a.status == ML_OK
		&& b.status == ML_OK
		&& a.rows == b.rows
		&& a.cols == b.cols
	);

	for (uint64_t i = 0; i < a.rows * a.cols && same; i++)
	{
		same = ml_rat_cmp (arena, a.cells [i], b.cells [i]) == 0;
	}

	return same;
}

/* The first failed status of a and b, ML_OK when both are fine. */
static MlResult status_of (MlMat a, MlMat b)
{
	return ml_choose (
		a.status != ML_OK,
		a.status,
		b.status
	);
}

/* a + b or a - b, cell by cell. */
static MlMat combine (MlArena *arena, MlMat a, MlMat b, int32_t subtract)
{
	MlMat m = ml_mat_error (status_of (a, b));

	if (m.status == ML_OK && (a.rows != b.rows || a.cols != b.cols))
	{
		m = ml_mat_error (ML_ERR_INVALID);
	}

	else if (m.status == ML_OK)
	{
		m = ml_mat_zero (arena, a.rows, a.cols);

		for (uint64_t i = 0; i < a.rows * a.cols && m.status == ML_OK; i++)
		{
			m.cells [i] = ml_choose (
				subtract,
				ml_rat_sub (arena, a.cells [i], b.cells [i]),
				ml_rat_add (arena, a.cells [i], b.cells [i])
			);
		}

		m = checked (m);
	}

	return m;
}

MlMat ml_mat_add (MlArena *arena, MlMat a, MlMat b)
{
	return combine (arena, a, b, 0);
}

MlMat ml_mat_sub (MlArena *arena, MlMat a, MlMat b)
{
	return combine (arena, a, b, 1);
}

MlMat ml_mat_mul (MlArena *arena, MlMat a, MlMat b)
{
	MlMat m = ml_mat_error (status_of (a, b));

	if (m.status == ML_OK && a.cols != b.rows)
	{
		m = ml_mat_error (ML_ERR_INVALID);
	}

	else if (m.status == ML_OK)
	{
		m = ml_mat_zero (arena, a.rows, b.cols);

		for (uint64_t i = 0; i < a.rows && m.status == ML_OK; i++)
		{
			for (uint64_t j = 0; j < b.cols; j++)
			{
				MlRat sum = ml_rat_zero (arena);

				for (uint64_t k = 0; k < a.cols; k++)
				{
					sum = ml_rat_add (
						arena,
						sum,
						ml_rat_mul (
							arena,
							a.cells [i * a.cols + k],
							b.cells [k * b.cols + j]
						)
					);
				}

				m.cells [i * b.cols + j] = sum;
			}
		}

		m = checked (m);
	}

	return m;
}

MlMat ml_mat_scale (MlArena *arena, MlMat a, MlRat k)
{
	MlMat m = ml_mat_error (a.status);

	if (m.status == ML_OK && !ml_rat_ok (k))
	{
		m = ml_mat_error (ml_rat_status (k));
	}

	else if (m.status == ML_OK)
	{
		m = ml_mat_zero (arena, a.rows, a.cols);

		for (uint64_t i = 0; i < a.rows * a.cols && m.status == ML_OK; i++)
		{
			m.cells [i] = ml_rat_mul (arena, a.cells [i], k);
		}

		m = checked (m);
	}

	return m;
}

MlMat ml_mat_neg (MlArena *arena, MlMat a)
{
	return ml_mat_scale (arena, a, ml_rat_from_i64 (arena, -1));
}

MlMat ml_mat_transpose (MlArena *arena, MlMat a)
{
	MlMat m = ml_mat_error (a.status);

	if (m.status == ML_OK)
	{
		m = ml_mat_zero (arena, a.cols, a.rows);

		for (uint64_t i = 0; i < a.rows && m.status == ML_OK; i++)
		{
			for (uint64_t j = 0; j < a.cols; j++)
			{
				m.cells [j * a.rows + i] = a.cells [i * a.cols + j];
			}
		}
	}

	return m;
}

static MlRatResult rat_result (MlRat value)
{
	MlRatResult r =
	{
		.status = ml_rat_status (value),
		.value  = value
	};

	return r;
}

MlRatResult ml_mat_trace (MlArena *arena, MlMat a)
{
	MlRatResult r = rat_result (ml_rat_error (ML_OK));

	if (a.status != ML_OK)
	{
		r = rat_result (ml_rat_error (a.status));
	}

	else if (a.rows != a.cols)
	{
		r = rat_result (ml_rat_error (ML_ERR_INVALID));
	}

	else
	{
		MlRat sum = ml_rat_zero (arena);

		for (uint64_t i = 0; i < a.rows; i++)
		{
			sum = ml_rat_add (arena, sum, a.cells [i * a.cols + i]);
		}

		r = rat_result (sum);
	}

	return r;
}

/* Swaps two rows of m in place. */
static void swap_rows (MlMat m, uint64_t a, uint64_t b)
{
	for (uint64_t j = 0; j < m.cols; j++)
	{
		MlRat t = m.cells [a * m.cols + j];

		m.cells [a * m.cols + j] = m.cells [b * m.cols + j];
		m.cells [b * m.cols + j] = t;
	}
}

/* The first row from `from` down whose cell in column col is not zero. */
static uint64_t find_pivot (MlMat m, uint64_t from, uint64_t col)
{
	uint64_t row = m.rows;

	for (uint64_t i = from; i < m.rows && row == m.rows; i++)
	{
		if (!ml_rat_is_zero (m.cells [i * m.cols + col]))
		{
			row = i;
		}
	}

	return row;
}

/* row `dst` minus factor times row `src`. */
static void subtract_row (
	MlArena  *arena,
	MlMat    m,
	uint64_t dst,
	uint64_t src,
	MlRat    factor
)
{
	for (uint64_t j = 0; j < m.cols; j++)
	{
		m.cells [dst * m.cols + j] = ml_rat_sub (
			arena,
			m.cells [dst * m.cols + j],
			ml_rat_mul (arena, factor, m.cells [src * m.cols + j])
		);
	}
}

/* A copy of m. */
static MlMat copy_of (MlArena *arena, MlMat m)
{
	MlMat c = ml_mat_zero (arena, m.rows, m.cols);

	for (uint64_t i = 0; i < m.rows * m.cols && c.status == ML_OK; i++)
	{
		c.cells [i] = m.cells [i];
	}

	return c;
}

MlRatResult ml_mat_det (MlArena *arena, MlMat a)
{
	MlRatResult r = rat_result (ml_rat_error (ML_OK));

	if (a.status != ML_OK)
	{
		r = rat_result (ml_rat_error (a.status));
	}

	else if (a.rows != a.cols)
	{
		r = rat_result (ml_rat_error (ML_ERR_INVALID));
	}

	else
	{
		MlMat   w      = copy_of (arena, a);
		MlRat   det    = ml_rat_from_i64 (arena, 1);
		int32_t zero   = 0;
		int32_t negate = 0;

		for (uint64_t c = 0; c < w.cols && w.status == ML_OK && !zero; c++)
		{
			uint64_t p = find_pivot (w, c, c);

			if (p == w.rows)
			{
				zero = 1;
			}

			else
			{
				MlRat pivot;

				if (p != c)
				{
					swap_rows (w, p, c);
					negate = !negate;
				}

				pivot = w.cells [c * w.cols + c];
				det   = ml_rat_mul (arena, det, pivot);

				for (uint64_t i = c + 1; i < w.rows; i++)
				{
					subtract_row (
						arena,
						w,
						i,
						c,
						ml_rat_div (arena, w.cells [i * w.cols + c], pivot)
					);
				}
			}
		}

		w = checked (w);
		r = rat_result (
			ml_choose (
				w.status != ML_OK,
				ml_rat_error (w.status),
				ml_choose (
					zero,
					ml_rat_zero (arena),
					ml_choose (
						negate,
						ml_rat_neg (arena, det),
						det
					)
				)
			)
		);
	}

	return r;
}

/*
 * Reduces the first `pivot_cols` columns of w to reduced row echelon form in
 * place (the columns after them are carried along). Returns the rank.
 */
static uint64_t reduce (MlArena *arena, MlMat w, uint64_t pivot_cols)
{
	uint64_t rank = 0;

	for (uint64_t c = 0; c < pivot_cols && rank < w.rows; c++)
	{
		uint64_t p = find_pivot (w, rank, c);

		if (p < w.rows)
		{
			MlRat pivot;

			swap_rows (w, p, rank);
			pivot = w.cells [rank * w.cols + c];

			for (uint64_t j = 0; j < w.cols; j++)
			{
				w.cells [rank * w.cols + j] = ml_rat_div (
					arena,
					w.cells [rank * w.cols + j],
					pivot
				);
			}

			for (uint64_t i = 0; i < w.rows; i++)
			{
				if (i != rank)
				{
					subtract_row (
						arena,
						w,
						i,
						rank,
						w.cells [i * w.cols + c]
					);
				}
			}

			rank++;
		}
	}

	return rank;
}

MlRref ml_mat_rref (MlArena *arena, MlMat a)
{
	MlRref out =
	{
		.matrix = ml_mat_error (a.status),
		.rank   = 0
	};

	if (a.status == ML_OK)
	{
		out.matrix = copy_of (arena, a);
	}

	if (out.matrix.status == ML_OK)
	{
		out.rank   = reduce (arena, out.matrix, out.matrix.cols);
		out.matrix = checked (out.matrix);
	}

	return out;
}

/* The columns first to first + count of m. */
static MlMat columns_of (
	MlArena  *arena,
	MlMat    m,
	uint64_t first,
	uint64_t count
)
{
	MlMat c = ml_mat_zero (arena, m.rows, count);

	for (uint64_t i = 0; i < m.rows && c.status == ML_OK; i++)
	{
		for (uint64_t j = 0; j < count; j++)
		{
			c.cells [i * count + j] = m.cells [i * m.cols + first + j];
		}
	}

	return c;
}

/* [a | b]: the columns of a, then those of b. */
static MlMat augment (MlArena *arena, MlMat a, MlMat b)
{
	MlMat m = ml_mat_zero (arena, a.rows, a.cols + b.cols);

	for (uint64_t i = 0; i < a.rows && m.status == ML_OK; i++)
	{
		for (uint64_t j = 0; j < a.cols; j++)
		{
			m.cells [i * m.cols + j] = a.cells [i * a.cols + j];
		}

		for (uint64_t j = 0; j < b.cols; j++)
		{
			m.cells [i * m.cols + a.cols + j] = b.cells [i * b.cols + j];
		}
	}

	return m;
}

MlMat ml_mat_solve (MlArena *arena, MlMat a, MlMat b)
{
	MlMat x = ml_mat_error (status_of (a, b));

	if (x.status == ML_OK && (a.rows != a.cols || b.rows != a.rows))
	{
		x = ml_mat_error (ML_ERR_INVALID);
	}

	else if (x.status == ML_OK)
	{
		MlMat    w    = augment (arena, a, b);
		uint64_t rank = 0;

		if (w.status == ML_OK)
		{
			rank = reduce (arena, w, a.cols);
			x    = ml_choose (
				rank == a.cols,
				checked (columns_of (arena, w, a.cols, b.cols)),
				ml_mat_error (ML_ERR_DIVZERO)
			);
		}

		else
		{
			x = w;
		}

		x = ml_choose (
			x.status == ML_OK,
			checked (x),
			x
		);
	}

	return x;
}

MlMat ml_mat_inverse (MlArena *arena, MlMat a)
{
	MlMat inv = ml_mat_error (a.status);

	if (a.status == ML_OK && a.rows != a.cols)
	{
		inv = ml_mat_error (ML_ERR_INVALID);
	}

	else if (a.status == ML_OK)
	{
		inv = ml_mat_solve (arena, a, ml_mat_identity (arena, a.rows));
	}

	return inv;
}

MlMat ml_mat_pow (MlArena *arena, MlMat a, int64_t exponent)
{
	MlMat    result = ml_mat_error (a.status);
	MlMat    base   = a;
	uint64_t n      = ml_choose (
		exponent < 0,
		(uint64_t)(-(exponent + 1)) + 1,
		(uint64_t)exponent
	);

	if (a.status == ML_OK && a.rows != a.cols)
	{
		result = ml_mat_error (ML_ERR_INVALID);
	}

	else if (a.status == ML_OK)
	{
		result = ml_mat_identity (arena, a.rows);
		base   = ml_choose (
			exponent < 0,
			ml_mat_inverse (arena, a),
			a
		);
	}

	while (n > 0 && result.status == ML_OK && base.status == ML_OK)
	{
		if (n & 1)
		{
			result = ml_mat_mul (arena, result, base);
		}

		n >>= 1;

		if (n > 0)
		{
			base = ml_mat_mul (arena, base, base);
		}
	}

	return ml_choose (
		base.status != ML_OK,
		base,
		result
	);
}

MlResult ml_mat_format (MlStr *out, MlMat m)
{
	MlResult r = m.status;

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr ("["));
	}

	for (uint64_t i = 0; i < m.rows && r == ML_OK; i++)
	{
		r = ml_str_append (
			out,
			ml_slice_cstr (
				ml_choose (
					i > 0,
					", [",
					"["
				)
			)
		);

		for (uint64_t j = 0; j < m.cols && r == ML_OK; j++)
		{
			if (j > 0)
			{
				r = ml_str_append (out, ml_slice_cstr (", "));
			}

			if (r == ML_OK)
			{
				r = ml_rat_format (out, m.cells [i * m.cols + j]);
			}
		}

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_slice_cstr ("]"));
		}
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr ("]"));
	}

	return r;
}
