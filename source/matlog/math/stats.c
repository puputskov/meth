#include <matlog/math/stats.h>

#include <math.h>

#include <matlog/core/mem.h>
#include <matlog/math/matrix.h>

static MlValue error_of (MlErrorKind kind)
{
	return ml_value_error (kind, 0, 0);
}

/* A rational as a value, or the error that made it fail. */
static MlValue rat_value (MlRat x)
{
	return ml_choose (
		ml_rat_ok (x),
		ml_value_exact (x),
		error_of (ml_choose (
			ml_rat_status (x) == ML_ERR_NOMEM,
			ML_EVAL_NO_MEMORY,
			ml_choose (
				ml_rat_status (x) == ML_ERR_DIVZERO,
				ML_EVAL_DIV_ZERO,
				ML_EVAL_TOO_LARGE
			)
		))
	);
}

/* The square root of a rational: exact when it is a rational, else approx. */
static MlValue root_value (MlArena *arena, MlRat x)
{
	MlRatSqrt s = ml_rat_sqrt_exact (arena, x);

	return ml_choose (
		s.exact,
		rat_value (s.value),
		ml_value_approx (sqrt (ml_rat_to_double (arena, x)))
	);
}

/* ---- data -------------------------------------------------------------- */

typedef struct Data
{
	MlErrorKind error; /* ML_EVAL_NONE when the data is usable */
	uint64_t    n;
	MlRat      *x;
	int32_t     column; /* 1: a column vector, 0: a row */
} Data;

/* The numbers of a vector value, as a list. */
static Data data_of (MlArena *arena, MlValue v)
{
	Data d = { .error = ML_EVAL_NONE, .n = 0, .x = NULL, .column = 1 };

	if (!ml_value_is_matrix (v))
	{
		d.error = ML_EVAL_TYPE;
	}

	else if (v.matrix.rows != 1 && v.matrix.cols != 1)
	{
		d.error = ML_EVAL_SIZE;
	}

	else
	{
		d.n      = v.matrix.rows * v.matrix.cols;
		d.column = v.matrix.cols == 1;
		d.x      = ML_ARENA_NEW_ARRAY (arena, MlRat, d.n);

		if (d.x == NULL)
		{
			d.error = ML_EVAL_NO_MEMORY;
		}

		for (uint64_t i = 0; i < d.n && d.error == ML_EVAL_NONE; i++)
		{
			d.x [i] = v.matrix.cells [i];
		}
	}

	return d;
}

/* The list as a vector of the same direction as the data it came from. */
static MlValue vector_value (
	MlArena     *arena,
	const MlRat *x,
	uint64_t     n,
	int32_t      column
)
{
	MlMat m = ml_mat_zero (
		arena,
		ml_choose (column, n, 1),
		ml_choose (column, 1, n)
	);

	for (uint64_t i = 0; i < n; i++)
	{
		m.cells [i] = x [i];
	}

	return ml_value_matrix (m);
}

/* A sorted copy of the data. */
static Data sorted_of (MlArena *arena, Data d)
{
	Data s = d;

	s.x = ML_ARENA_NEW_ARRAY (arena, MlRat, ml_choose (d.n > 0, d.n, 1));

	if (s.x == NULL)
	{
		s.error = ML_EVAL_NO_MEMORY;
	}

	for (uint64_t i = 0; i < d.n && s.error == ML_EVAL_NONE; i++)
	{
		uint64_t k = i;

		while (k > 0 && ml_rat_cmp (arena, s.x [k - 1], d.x [i]) > 0)
		{
			s.x [k] = s.x [k - 1];
			k--;
		}

		s.x [k] = d.x [i];
	}

	return s;
}

static MlRat count_rat (MlArena *arena, uint64_t n)
{
	return ml_rat_from_i64 (arena, (int64_t)n);
}

static MlRat sum_of (MlArena *arena, Data d)
{
	MlRat s = ml_rat_zero (arena);

	for (uint64_t i = 0; i < d.n; i++)
	{
		s = ml_rat_add (arena, s, d.x [i]);
	}

	return s;
}

static MlRat mean_of (MlArena *arena, Data d)
{
	return ml_rat_div (arena, sum_of (arena, d), count_rat (arena, d.n));
}

/* The sum of (x - mean)^power. */
static MlRat central_sum (MlArena *arena, Data d, int64_t power)
{
	MlRat mean = mean_of (arena, d);
	MlRat s    = ml_rat_zero (arena);

	for (uint64_t i = 0; i < d.n; i++)
	{
		s = ml_rat_add (
			arena,
			s,
			ml_rat_pow (arena, ml_rat_sub (arena, d.x [i], mean), power)
		);
	}

	return s;
}

/* The variance, divided by n - 1 (sample) or n. */
static MlRat variance_of (MlArena *arena, Data d, int32_t sample)
{
	return ml_rat_div (
		arena,
		central_sum (arena, d, 2),
		count_rat (arena, d.n - ml_choose (sample, 1, 0))
	);
}

/* The sum of (x - mx) (y - my). */
static MlRat cross_sum (MlArena *arena, Data a, Data b)
{
	MlRat ma = mean_of (arena, a);
	MlRat mb = mean_of (arena, b);
	MlRat s  = ml_rat_zero (arena);

	for (uint64_t i = 0; i < a.n; i++)
	{
		s = ml_rat_add (
			arena,
			s,
			ml_rat_mul (
				arena,
				ml_rat_sub (arena, a.x [i], ma),
				ml_rat_sub (arena, b.x [i], mb)
			)
		);
	}

	return s;
}

/* The value at probability p of sorted data, linear between neighbours. */
static MlValue quantile_at (MlArena *arena, Data sorted, MlRat p)
{
	MlValue r = error_of (ML_EVAL_DOMAIN);

	if (ml_rat_ok (p) && ml_rat_sign (p) >= 0
		&& ml_rat_cmp (arena, p, ml_rat_from_i64 (arena, 1)) <= 0)
	{
		MlRat       h = ml_rat_mul (arena, p, count_rat (arena, sorted.n - 1));
		MlBig       floor = ml_big_divmod (arena, h.num, h.den).quot;
		MlU64Result lo    = ml_big_to_u64 (floor);
		uint64_t    i     = ml_choose (lo.ok, lo.value, 0);
		uint64_t    j     = ml_choose (i + 1 < sorted.n, i + 1, i);
		MlRat frac = ml_rat_sub (arena, h, ml_rat_from_big (arena, floor));

		r = rat_value (ml_rat_add (
			arena,
			sorted.x [i],
			ml_rat_mul (
				arena,
				frac,
				ml_rat_sub (arena, sorted.x [j], sorted.x [i])
			)
		));
	}

	return r;
}

/* ---- one list ---------------------------------------------------------- */

static MlValue list_function (
	MlArena *arena,
	MlStat   which,
	MlValue  arg,
	MlValue  extra
)
{
	Data    d = data_of (arena, arg);
	MlValue r = error_of (d.error);

	if (d.error == ML_EVAL_NONE && d.n == 0)
	{
		r = error_of (ML_EVAL_DOMAIN);
	}

	else if (d.error == ML_EVAL_NONE)
	{
		Data s = sorted_of (arena, d);

		if (s.error != ML_EVAL_NONE)
		{
			r = error_of (s.error);
		}

		else if (which == ML_STAT_SUM)
		{
			r = rat_value (sum_of (arena, d));
		}

		else if (which == ML_STAT_LENGTH)
		{
			r = rat_value (count_rat (arena, d.n));
		}

		else if (which == ML_STAT_MEAN)
		{
			r = rat_value (mean_of (arena, d));
		}

		else if (which == ML_STAT_MEDIAN)
		{
			r = quantile_at (
				arena,
				s,
				ml_rat_make (
					arena,
					ml_big_from_u64 (arena, 1),
					ml_big_from_u64 (arena, 2)
				)
			);
		}

		else if (which == ML_STAT_MODE)
		{
			uint64_t best = 0;
			uint64_t run  = 1;
			uint64_t top  = 0;

			for (uint64_t i = 1; i <= s.n; i++)
			{
				if (i < s.n && ml_rat_cmp (arena, s.x [i], s.x [i - 1]) == 0)
				{
					run++;
				}

				else
				{
					if (run > top)
					{
						top  = run;
						best = i - 1;
					}

					run = 1;
				}
			}

			r = rat_value (s.x [best]);
		}

		else if (which == ML_STAT_VAR || which == ML_STAT_STD)
		{
			r = ml_choose (
				d.n < 2,
				error_of (ML_EVAL_DOMAIN),
				ml_choose (
					which == ML_STAT_VAR,
					rat_value (variance_of (arena, d, 1)),
					root_value (arena, variance_of (arena, d, 1))
				)
			);
		}

		else if (which == ML_STAT_PVAR)
		{
			r = rat_value (variance_of (arena, d, 0));
		}

		else if (which == ML_STAT_PSTD)
		{
			r = root_value (arena, variance_of (arena, d, 0));
		}

		else if (which == ML_STAT_IQR)
		{
			MlRat q1 = ml_rat_make (
				arena,
				ml_big_from_u64 (arena, 1),
				ml_big_from_u64 (arena, 4)
			);
			MlRat q3 = ml_rat_make (
				arena,
				ml_big_from_u64 (arena, 3),
				ml_big_from_u64 (arena, 4)
			);

			r = ml_value_sub (
				arena,
				quantile_at (arena, s, q3),
				quantile_at (arena, s, q1)
			);
		}

		else if (which == ML_STAT_QUANTILE)
		{
			r = ml_choose (
				extra.kind == ML_VALUE_EXACT,
				quantile_at (arena, s, extra.exact),
				error_of (ML_EVAL_TYPE)
			);
		}

		else if (which == ML_STAT_SORT)
		{
			r = vector_value (arena, s.x, s.n, d.column);
		}

		else if (which == ML_STAT_CUMSUM)
		{
			MlRat *run = ML_ARENA_NEW_ARRAY (arena, MlRat, d.n);
			MlRat  sum = ml_rat_zero (arena);

			for (uint64_t i = 0; i < d.n && run != NULL; i++)
			{
				sum     = ml_rat_add (arena, sum, d.x [i]);
				run [i] = sum;
			}

			r = ml_choose (
				run != NULL,
				vector_value (arena, run, d.n, d.column),
				error_of (ML_EVAL_NO_MEMORY)
			);
		}

		else if (which == ML_STAT_KURT)
		{
			MlRat m2 = ml_rat_div (
				arena,
				central_sum (arena, d, 2),
				count_rat (arena, d.n)
			);
			MlRat m4 = ml_rat_div (
				arena,
				central_sum (arena, d, 4),
				count_rat (arena, d.n)
			);

			r = ml_choose (
				ml_rat_is_zero (m2),
				error_of (ML_EVAL_DIV_ZERO),
				rat_value (ml_rat_sub (
					arena,
					ml_rat_div (arena, m4, ml_rat_mul (arena, m2, m2)),
					ml_rat_from_i64 (arena, 3)
				))
			);
		}

		else
		{
			/* skewness: m3 / m2^(3/2), a root, so approximate */
			MlRat m2 = ml_rat_div (
				arena,
				central_sum (arena, d, 2),
				count_rat (arena, d.n)
			);
			MlRat m3 = ml_rat_div (
				arena,
				central_sum (arena, d, 3),
				count_rat (arena, d.n)
			);

			r = ml_choose (
				ml_rat_is_zero (m2),
				error_of (ML_EVAL_DIV_ZERO),
				ml_choose (
					ml_rat_is_zero (m3),
					rat_value (m3),
					ml_value_approx (
						ml_rat_to_double (arena, m3)
						/ (ml_rat_to_double (arena, m2)
						   * sqrt (ml_rat_to_double (arena, m2)))
					)
				)
			);
		}
	}

	return r;
}

/* ---- two lists --------------------------------------------------------- */

static MlValue pair_function (
	MlArena *arena,
	MlStat   which,
	MlValue  a,
	MlValue  b
)
{
	Data    x = data_of (arena, a);
	Data    y = data_of (arena, b);
	MlValue r
		= error_of (ml_choose (x.error != ML_EVAL_NONE, x.error, y.error));

	if (x.error == ML_EVAL_NONE && y.error == ML_EVAL_NONE)
	{
		if (x.n != y.n)
		{
			r = error_of (ML_EVAL_SIZE);
		}

		else if (x.n < 2)
		{
			r = error_of (ML_EVAL_DOMAIN);
		}

		else
		{
			MlRat sxy = cross_sum (arena, x, y);
			MlRat sxx = central_sum (arena, x, 2);
			MlRat syy = central_sum (arena, y, 2);
			MlRat n1  = count_rat (arena, x.n - 1);

			if (which == ML_STAT_COV)
			{
				r = rat_value (ml_rat_div (arena, sxy, n1));
			}

			else if (ml_rat_is_zero (sxx)
					 || (which != ML_STAT_LINFIT && ml_rat_is_zero (syy)))
			{
				r = error_of (ML_EVAL_DIV_ZERO);
			}

			else if (which == ML_STAT_LINFIT)
			{
				MlRat slope     = ml_rat_div (arena, sxy, sxx);
				MlRat intercept = ml_rat_sub (
					arena,
					mean_of (arena, y),
					ml_rat_mul (arena, slope, mean_of (arena, x))
				);
				MlRat pair [2] = { intercept, slope };

				r = vector_value (arena, pair, 2, 1);
			}

			else
			{
				MlRat r2 = ml_rat_div (
					arena,
					ml_rat_mul (arena, sxy, sxy),
					ml_rat_mul (arena, sxx, syy)
				);

				if (which == ML_STAT_RSQ)
				{
					r = rat_value (r2);
				}

				else
				{
					r = root_value (arena, r2);

					if (ml_rat_sign (sxy) < 0)
					{
						r = ml_value_neg (arena, r);
					}
				}
			}
		}
	}

	return r;
}

/* ---- fits -------------------------------------------------------------- */

/* The x that makes A x closest to b: solve (A^T A) x = A^T b. */
static MlValue least_squares (MlArena *arena, MlMat a, MlMat b)
{
	MlMat at = ml_mat_transpose (arena, a);

	return ml_value_matrix (ml_mat_solve (
		arena,
		ml_mat_mul (arena, at, a),
		ml_mat_mul (arena, at, b)
	));
}

static MlValue polynomial_fit (
	MlArena *arena,
	MlValue  xs,
	MlValue  ys,
	MlValue  degree
)
{
	Data        x = data_of (arena, xs);
	Data        y = data_of (arena, ys);
	MlI64Result n = ml_value_to_i64 (degree);
	MlValue     r
		= error_of (ml_choose (x.error != ML_EVAL_NONE, x.error, y.error));

	if (x.error == ML_EVAL_NONE && y.error == ML_EVAL_NONE)
	{
		if (!n.ok)
		{
			r = error_of (ML_EVAL_NOT_INTEGER);
		}

		else if (x.n != y.n)
		{
			r = error_of (ML_EVAL_SIZE);
		}

		else if (n.value < 0 || (uint64_t)n.value + 1 > x.n || n.value > 12)
		{
			r = error_of (ML_EVAL_DOMAIN);
		}

		else
		{
			uint64_t cols = (uint64_t)n.value + 1;
			MlMat    v    = ml_mat_zero (arena, x.n, cols);
			MlMat    b    = ml_mat_zero (arena, x.n, 1);

			for (uint64_t i = 0; i < x.n; i++)
			{
				MlRat power = ml_rat_from_i64 (arena, 1);

				for (uint64_t k = 0; k < cols; k++)
				{
					ml_mat_set (&v, i, k, power);
					power = ml_rat_mul (arena, power, x.x [i]);
				}

				ml_mat_set (&b, i, 0, y.x [i]);
			}

			r = least_squares (arena, v, b);
		}
	}

	return r;
}

/* ---- ranges and histograms -------------------------------------------- */

static MlValue range_value (MlArena *arena, const MlValue *args, uint64_t argc)
{
	MlValue r = error_of (ML_EVAL_TYPE);
	int32_t nums
		= args [0].kind == ML_VALUE_EXACT && args [1].kind == ML_VALUE_EXACT;

	nums = nums && (argc < 3 || args [2].kind == ML_VALUE_EXACT);

	if (nums)
	{
		MlRat step
			= ml_choose (argc > 2, args [2].exact, ml_rat_from_i64 (arena, 1));
		MlRat    at   = args [0].exact;
		MlRat    stop = args [1].exact;
		int32_t  up   = ml_rat_sign (step) > 0;
		MlRat    list [ML_MAT_MAX_DIM];
		uint64_t n = 0;

		if (ml_rat_is_zero (step))
		{
			r = error_of (ML_EVAL_DOMAIN);
		}

		else
		{
			while (n <= ML_MAT_MAX_DIM && ml_rat_ok (at)
				   && ml_rat_cmp (arena, at, stop) * ml_choose (up, 1, -1) <= 0)
			{
				if (n < ML_MAT_MAX_DIM)
				{
					list [n] = at;
				}

				n++;
				at = ml_rat_add (arena, at, step);
			}

			r = ml_choose (
				n > ML_MAT_MAX_DIM,
				error_of (ML_EVAL_SIZE),
				ml_choose (
					n == 0,
					error_of (ML_EVAL_DOMAIN),
					vector_value (arena, list, n, 1)
				)
			);
		}
	}

	return r;
}

static MlValue histogram_value (MlArena *arena, MlValue arg, MlValue bins)
{
	Data        d = data_of (arena, arg);
	MlI64Result k = ml_value_to_i64 (bins);
	MlValue     r = error_of (d.error);

	if (d.error == ML_EVAL_NONE)
	{
		if (!k.ok)
		{
			r = error_of (ML_EVAL_NOT_INTEGER);
		}

		else if (d.n == 0 || k.value < 1 || k.value > (int64_t)ML_MAT_MAX_DIM)
		{
			r = error_of (ML_EVAL_DOMAIN);
		}

		else
		{
			Data  s      = sorted_of (arena, d);
			MlRat low    = s.x [0];
			MlRat span   = ml_rat_sub (arena, s.x [s.n - 1], low);
			MlMat counts = ml_mat_zero (arena, (uint64_t)k.value, 1);

			for (uint64_t i = 0; i < d.n; i++)
			{
				uint64_t bin = 0;

				if (!ml_rat_is_zero (span))
				{
					MlRat pos = ml_rat_mul (
						arena,
						ml_rat_div (
							arena,
							ml_rat_sub (arena, d.x [i], low),
							span
						),
						ml_rat_from_i64 (arena, k.value)
					);
					MlBig fl = ml_big_divmod (arena, pos.num, pos.den).quot;
					MlU64Result u = ml_big_to_u64 (fl);

					bin = ml_choose (
						u.ok && u.value < (uint64_t)k.value,
						u.value,
						(uint64_t)k.value - 1
					);
				}

				ml_mat_set (
					&counts,
					bin,
					0,
					ml_rat_add (
						arena,
						ml_mat_get (counts, bin, 0),
						ml_rat_from_i64 (arena, 1)
					)
				);
			}

			r = ml_value_matrix (counts);
		}
	}

	return r;
}

/* ---- counting and distributions --------------------------------------- */

/* n k as whole numbers, 0 <= k <= n <= 2000. */
static int32_t whole_pair (MlValue a, MlValue b, int64_t *n, int64_t *k)
{
	MlI64Result x = ml_value_to_i64 (a);
	MlI64Result y = ml_value_to_i64 (b);

	*n = x.value;
	*k = y.value;
	return x.ok && y.ok && x.value >= 0 && y.value >= 0 && y.value <= x.value
		   && x.value <= 2000;
}

/* n (n - 1) ... (n - k + 1) divided by k! when choose is 1. */
static MlRat arrange (MlArena *arena, int64_t n, int64_t k, int32_t choose)
{
	MlRat r = ml_rat_from_i64 (arena, 1);

	for (int64_t i = 1; i <= k; i++)
	{
		r = ml_rat_mul (arena, r, ml_rat_from_i64 (arena, n - k + i));

		if (choose)
		{
			r = ml_rat_div (arena, r, ml_rat_from_i64 (arena, i));
		}
	}

	return r;
}

/* A number argument as a double, 0 when it is none. */
static int32_t number_of (MlArena *arena, MlValue v, double *out)
{
	int32_t ok = v.kind == ML_VALUE_EXACT || v.kind == ML_VALUE_APPROX;

	*out = ml_choose (ok, ml_value_to_double (arena, v), 0.0);
	return ok;
}

/* The standard normal quantile (Acklam), refined by one step. */
static double normal_quantile (double p)
{
	static const double a [6]
		= { -3.969683028665376e+01, 2.209460984245205e+02,
			-2.759285104469687e+02, 1.383577518672690e+02,
			-3.066479806614716e+01, 2.506628277459239e+00 };
	static const double b [5] = { -5.447609879822406e+01,
								  1.615858368580409e+02,
								  -1.556989798598866e+02,
								  6.680131188771972e+01,
								  -1.328068155288572e+01 };
	static const double c [6]
		= { -7.784894002430293e-03, -3.223964580411365e-01,
			-2.400758277161838e+00, -2.549732539343734e+00,
			4.374664141464968e+00,  2.938163982698783e+00 };
	static const double d [4] = { 7.784695709041462e-03,
								  3.224671290700398e-01,
								  2.445134137142996e+00,
								  3.754408661907416e+00 };
	double              x;
	double              q;
	double              s;

	if (p < 0.02425)
	{
		q = sqrt (-2.0 * log (p));
		x = (((((c [0] * q + c [1]) * q + c [2]) * q + c [3]) * q + c [4]) * q
			 + c [5])
			/ ((((d [0] * q + d [1]) * q + d [2]) * q + d [3]) * q + 1.0);
	}

	else if (p > 1.0 - 0.02425)
	{
		q = sqrt (-2.0 * log (1.0 - p));
		x = -(((((c [0] * q + c [1]) * q + c [2]) * q + c [3]) * q + c [4]) * q
			  + c [5])
			/ ((((d [0] * q + d [1]) * q + d [2]) * q + d [3]) * q + 1.0);
	}

	else
	{
		q = p - 0.5;
		s = q * q;
		x = (((((a [0] * s + a [1]) * s + a [2]) * s + a [3]) * s + a [4]) * s
			 + a [5])
			* q
			/ (((((b [0] * s + b [1]) * s + b [2]) * s + b [3]) * s + b [4]) * s
			   + 1.0);
	}

	/* one Halley step against the exact cdf */
	q = 0.5 * erfc (-x / sqrt (2.0)) - p;
	s = q * sqrt (2.0 * 3.14159265358979323846) * exp (x * x / 2.0);
	return x - s / (1.0 + x * s / 2.0);
}

static MlValue normal_function (
	MlArena       *arena,
	MlStat         which,
	const MlValue *args,
	uint64_t       argc
)
{
	double  x     = 0.0;
	double  mu    = 0.0;
	double  sigma = 1.0;
	int32_t ok    = number_of (arena, args [0], &x);

	ok = ok && (argc < 2 || number_of (arena, args [1], &mu));
	ok = ok && (argc < 3 || number_of (arena, args [2], &sigma));

	return ml_choose (
		!ok,
		error_of (ML_EVAL_TYPE),
		ml_choose (
			sigma <= 0.0
				|| (which == ML_STAT_NORMAL_INV && (x <= 0.0 || x >= 1.0)),
			error_of (ML_EVAL_DOMAIN),
			ml_choose (
				which == ML_STAT_NORMAL_PDF,
				ml_value_approx (
					exp (-0.5 * ((x - mu) / sigma) * ((x - mu) / sigma))
					/ (sigma * sqrt (2.0 * 3.14159265358979323846))
				),
				ml_choose (
					which == ML_STAT_NORMAL_CDF,
					ml_value_approx (
						0.5 * erfc (-((x - mu) / sigma) / sqrt (2.0))
					),
					ml_value_approx (mu + sigma * normal_quantile (x))
				)
			)
		)
	);
}

static MlValue binomial_pdf (MlArena *arena, const MlValue *args)
{
	int64_t n = 0;
	int64_t k = 0;
	MlValue r = error_of (ML_EVAL_NOT_INTEGER);

	if (whole_pair (args [0], args [1], &n, &k))
	{
		MlRat one = ml_rat_from_i64 (arena, 1);

		if (args [2].kind == ML_VALUE_EXACT)
		{
			MlRat p = args [2].exact;

			r = ml_choose (
				ml_rat_sign (p) < 0 || ml_rat_cmp (arena, p, one) > 0,
				error_of (ML_EVAL_DOMAIN),
				rat_value (ml_rat_mul (
					arena,
					arrange (arena, n, k, 1),
					ml_rat_mul (
						arena,
						ml_rat_pow (arena, p, k),
						ml_rat_pow (arena, ml_rat_sub (arena, one, p), n - k)
					)
				))
			);
		}

		else
		{
			double p = 0.0;

			r = ml_choose (
				!number_of (arena, args [2], &p),
				error_of (ML_EVAL_TYPE),
				ml_choose (
					p < 0.0 || p > 1.0,
					error_of (ML_EVAL_DOMAIN),
					ml_value_approx (
						ml_rat_to_double (arena, arrange (arena, n, k, 1))
						* pow (p, (double)k) * pow (1.0 - p, (double)(n - k))
					)
				)
			);
		}
	}

	return r;
}

static MlValue poisson_pdf (MlArena *arena, const MlValue *args)
{
	MlI64Result k      = ml_value_to_i64 (args [0]);
	double      lambda = 0.0;

	return ml_choose (
		!k.ok || k.value < 0,
		error_of (ML_EVAL_NOT_INTEGER),
		ml_choose (
			!number_of (arena, args [1], &lambda),
			error_of (ML_EVAL_TYPE),
			ml_choose (
				lambda < 0.0,
				error_of (ML_EVAL_DOMAIN),
				ml_value_approx (exp (
					(double)k.value * log (lambda) - lambda
					- lgamma ((double)k.value + 1.0)
				))
			)
		)
	);
}

/* ---- entry points ------------------------------------------------------ */

MlValue ml_stats_extreme (MlArena *arena, MlValue v, int32_t is_max)
{
	Data    d = data_of (arena, v);
	MlValue r = error_of (d.error);

	if (d.error == ML_EVAL_NONE)
	{
		Data s = sorted_of (arena, d);

		r = ml_choose (
			d.n == 0,
			error_of (ML_EVAL_DOMAIN),
			ml_choose (
				s.error != ML_EVAL_NONE,
				error_of (s.error),
				rat_value (s.x [ml_choose (is_max, s.n - 1, 0)])
			)
		);
	}

	return r;
}

MlValue ml_stats_apply (
	MlArena       *arena,
	MlStat         which,
	const MlValue *args,
	uint64_t       argc
)
{
	MlValue r = error_of (ML_EVAL_ARITY);
	int64_t n = 0;
	int64_t k = 0;

	if (which == ML_STAT_COV || which == ML_STAT_CORR || which == ML_STAT_LINFIT
		|| which == ML_STAT_RSQ)
	{
		r = pair_function (arena, which, args [0], args [1]);
	}

	else if (which == ML_STAT_POLYFIT)
	{
		r = polynomial_fit (arena, args [0], args [1], args [2]);
	}

	else if (which == ML_STAT_LSTSQ)
	{
		r = ml_choose (
			ml_value_is_matrix (args [0]) && ml_value_is_matrix (args [1]),
			least_squares (arena, args [0].matrix, args [1].matrix),
			error_of (ML_EVAL_TYPE)
		);
	}

	else if (which == ML_STAT_RANGE)
	{
		r = range_value (arena, args, argc);
	}

	else if (which == ML_STAT_HISTOGRAM)
	{
		r = histogram_value (arena, args [0], args [1]);
	}

	else if (which == ML_STAT_BINOMIAL || which == ML_STAT_PERM)
	{
		r = ml_choose (
			whole_pair (args [0], args [1], &n, &k),
			rat_value (arrange (arena, n, k, which == ML_STAT_BINOMIAL)),
			error_of (ML_EVAL_NOT_INTEGER)
		);
	}

	else if (which == ML_STAT_BINOMIAL_PDF)
	{
		r = binomial_pdf (arena, args);
	}

	else if (which == ML_STAT_POISSON_PDF)
	{
		r = poisson_pdf (arena, args);
	}

	else if (which == ML_STAT_NORMAL_PDF || which == ML_STAT_NORMAL_CDF
			 || which == ML_STAT_NORMAL_INV)
	{
		r = normal_function (arena, which, args, argc);
	}

	else if (which == ML_STAT_ZSCORE)
	{
		r = ml_value_div (
			arena,
			ml_value_sub (arena, args [0], args [1]),
			args [2]
		);
	}

	else
	{
		r = list_function (
			arena,
			which,
			args [0],
			ml_choose (argc > 1, args [1], ml_value_none ())
		);
	}

	return r;
}
