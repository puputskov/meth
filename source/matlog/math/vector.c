#include <matlog/math/vector.h>

MlMat ml_vec_from_i64 (MlArena *arena, uint64_t n, const int64_t *values)
{
	MlMat v = ml_mat_zero (arena, n, 1);

	for (uint64_t i = 0; i < n && v.status == ML_OK; i++)
	{
		ml_mat_set (&v, i, 0, ml_rat_from_i64 (arena, values [i]));
	}

	return v;
}

int32_t ml_vec_is_vector (MlMat v)
{
	return v.status == ML_OK && v.cols == 1;
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

/* ML_OK when a and b are vectors of one length, else why not. */
static MlResult pair_status (MlMat a, MlMat b)
{
	MlResult s = ml_choose (
		a.status != ML_OK,
		a.status,
		b.status
	);

	if (s == ML_OK && (a.cols != 1 || b.cols != 1 || a.rows != b.rows))
	{
		s = ML_ERR_INVALID;
	}

	return s;
}

MlRatResult ml_vec_dot (MlArena *arena, MlMat a, MlMat b)
{
	MlResult    s = pair_status (a, b);
	MlRatResult r = rat_result (ml_rat_error (s));

	if (s == ML_OK)
	{
		MlRat sum = ml_rat_zero (arena);

		for (uint64_t i = 0; i < a.rows; i++)
		{
			sum = ml_rat_add (
				arena,
				sum,
				ml_rat_mul (arena, a.cells [i], b.cells [i])
			);
		}

		r = rat_result (sum);
	}

	return r;
}

MlMat ml_vec_cross (MlArena *arena, MlMat a, MlMat b)
{
	MlResult s = pair_status (a, b);
	MlMat    c = ml_mat_error (s);

	if (s == ML_OK && a.rows != 3)
	{
		c = ml_mat_error (ML_ERR_INVALID);
	}

	else if (s == ML_OK)
	{
		c = ml_mat_zero (arena, 3, 1);

		for (uint64_t i = 0; i < 3 && c.status == ML_OK; i++)
		{
			uint64_t j = (i + 1) % 3;
			uint64_t k = (i + 2) % 3;

			ml_mat_set (
				&c,
				i,
				0,
				ml_rat_sub (
					arena,
					ml_rat_mul (arena, a.cells [j], b.cells [k]),
					ml_rat_mul (arena, a.cells [k], b.cells [j])
				)
			);
		}

		for (uint64_t i = 0; i < 3 && c.status == ML_OK; i++)
		{
			if (!ml_rat_ok (c.cells [i]))
			{
				c = ml_mat_error (ml_rat_status (c.cells [i]));
			}
		}
	}

	return c;
}

MlRatResult ml_vec_norm_squared (MlArena *arena, MlMat v)
{
	return ml_vec_dot (arena, v, v);
}

MlVecNorm ml_vec_norm (MlArena *arena, MlMat v)
{
	MlRatResult sq   = ml_vec_norm_squared (arena, v);
	MlVecNorm   norm =
	{
		.status = sq.status,
		.exact  = 0,
		.value  = sq.value
	};

	if (sq.status == ML_OK)
	{
		MlRatSqrt root = ml_rat_sqrt_exact (arena, sq.value);

		norm.exact = root.exact;
		norm.value = ml_choose (
			root.exact,
			root.value,
			sq.value
		);
	}

	return norm;
}
