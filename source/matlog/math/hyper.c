#include <matlog/math/hyper.h>

#include <math.h>

#define PI 3.14159265358979323846

static MlValue error_of (MlErrorKind kind)
{
	return ml_value_error (kind, 0, 0);
}

int32_t ml_value_is_hyper (MlValue v)
{
	return v.kind == ML_VALUE_COMPLEX || v.kind == ML_VALUE_QUAT;
}

MlHyper ml_hyper_exact (MlRat a, MlRat b, MlRat c, MlRat d)
{
	MlHyper h;

	memset (&h, 0, sizeof h);
	h.approx = 0;
	h.r [0]  = a;
	h.r [1]  = b;
	h.r [2]  = c;
	h.r [3]  = d;
	return h;
}

MlHyper ml_hyper_approx (double a, double b, double c, double d)
{
	MlHyper h;

	memset (&h, 0, sizeof h);
	h.approx = 1;
	h.d [0]  = a;
	h.d [1]  = b;
	h.d [2]  = c;
	h.d [3]  = d;
	return h;
}

/* The parts as doubles. */
static MlHyper to_approx (MlArena *arena, MlHyper h)
{
	MlHyper out = h;

	if (!h.approx)
	{
		out = ml_hyper_approx (
			ml_rat_to_double (arena, h.r [0]),
			ml_rat_to_double (arena, h.r [1]),
			ml_rat_to_double (arena, h.r [2]),
			ml_rat_to_double (arena, h.r [3])
		);
	}

	return out;
}

MlHyper ml_hyper_of (MlArena *arena, MlValue v)
{
	MlRat   zero = ml_rat_zero (arena);
	MlHyper h    = ml_hyper_exact (zero, zero, zero, zero);

	if (v.kind == ML_VALUE_EXACT)
	{
		h.r [0] = v.exact;
	}

	else if (v.kind == ML_VALUE_APPROX)
	{
		h = ml_hyper_approx (v.approx, 0.0, 0.0, 0.0);
	}

	else if (ml_value_is_hyper (v))
	{
		h = v.hyper;
	}

	return h;
}

/* 1 when every part is a good number. */
static int32_t parts_ok (MlHyper h)
{
	int32_t ok = 1;

	for (uint64_t i = 0; i < 4; i++)
	{
		ok = ok
			 && ml_choose (h.approx, isfinite (h.d [i]), ml_rat_ok (h.r [i]));
	}

	return ok;
}

static MlValue parts_error (MlHyper h)
{
	MlErrorKind kind = ML_EVAL_TOO_LARGE;

	for (uint64_t i = 0; i < 4 && !h.approx; i++)
	{
		if (!ml_rat_ok (h.r [i]))
		{
			kind = ml_error_from_result (ml_rat_status (h.r [i]));
		}
	}

	for (uint64_t i = 0; i < 4 && h.approx; i++)
	{
		if (isnan (h.d [i]))
		{
			kind = ML_EVAL_NOT_REAL;
		}
	}

	return error_of (kind);
}

/* 1 when part i is zero. */
static int32_t is_zero (MlHyper h, uint64_t i)
{
	return ml_choose (h.approx, h.d [i] == 0.0, ml_rat_is_zero (h.r [i]));
}

MlValue ml_hyper_value (MlHyper h)
{
	MlValue v = error_of (ML_EVAL_NONE);

	if (!parts_ok (h))
	{
		v = parts_error (h);
	}

	else if (is_zero (h, 1) && is_zero (h, 2) && is_zero (h, 3))
	{
		v = ml_choose (
			h.approx,
			ml_value_approx (h.d [0]),
			ml_value_exact (h.r [0])
		);
	}

	else
	{
		v.kind = ml_choose (
			is_zero (h, 2) && is_zero (h, 3),
			ML_VALUE_COMPLEX,
			ML_VALUE_QUAT
		);
		v.hyper = h;
	}

	return v;
}

/* ---- exact parts ------------------------------------------------------- */

static MlHyper exact_add (MlArena *arena, MlHyper a, MlHyper b)
{
	return ml_hyper_exact (
		ml_rat_add (arena, a.r [0], b.r [0]),
		ml_rat_add (arena, a.r [1], b.r [1]),
		ml_rat_add (arena, a.r [2], b.r [2]),
		ml_rat_add (arena, a.r [3], b.r [3])
	);
}

/* x y + u v and the like: the Hamilton product needs sums of products. */
static MlRat dot2 (
	MlArena *arena,
	MlRat    p,
	MlRat    q,
	MlRat    u,
	MlRat    v,
	int32_t  minus
)
{
	MlRat first  = ml_rat_mul (arena, p, q);
	MlRat second = ml_rat_mul (arena, u, v);

	return ml_choose (
		minus,
		ml_rat_sub (arena, first, second),
		ml_rat_add (arena, first, second)
	);
}

static MlHyper exact_mul (MlArena *arena, MlHyper a, MlHyper b)
{
	MlRat w = ml_rat_sub (
		arena,
		dot2 (arena, a.r [0], b.r [0], a.r [1], b.r [1], 1),
		dot2 (arena, a.r [2], b.r [2], a.r [3], b.r [3], 0)
	);
	MlRat x = ml_rat_add (
		arena,
		dot2 (arena, a.r [0], b.r [1], a.r [1], b.r [0], 0),
		dot2 (arena, a.r [2], b.r [3], a.r [3], b.r [2], 1)
	);
	MlRat y = ml_rat_add (
		arena,
		dot2 (arena, a.r [0], b.r [2], a.r [2], b.r [0], 0),
		dot2 (arena, a.r [3], b.r [1], a.r [1], b.r [3], 1)
	);
	MlRat z = ml_rat_add (
		arena,
		dot2 (arena, a.r [0], b.r [3], a.r [3], b.r [0], 0),
		dot2 (arena, a.r [1], b.r [2], a.r [2], b.r [1], 1)
	);

	return ml_hyper_exact (w, x, y, z);
}

static MlRat exact_norm2 (MlArena *arena, MlHyper a)
{
	return ml_rat_add (
		arena,
		dot2 (arena, a.r [0], a.r [0], a.r [1], a.r [1], 0),
		dot2 (arena, a.r [2], a.r [2], a.r [3], a.r [3], 0)
	);
}

static MlHyper exact_conj (MlArena *arena, MlHyper a)
{
	return ml_hyper_exact (
		a.r [0],
		ml_rat_neg (arena, a.r [1]),
		ml_rat_neg (arena, a.r [2]),
		ml_rat_neg (arena, a.r [3])
	);
}

/* The inverse conj (a) / |a|^2; the status is the division by zero. */
static MlHyper exact_inverse (MlArena *arena, MlHyper a)
{
	MlRat   n2 = exact_norm2 (arena, a);
	MlHyper c  = exact_conj (arena, a);

	return ml_hyper_exact (
		ml_rat_div (arena, c.r [0], n2),
		ml_rat_div (arena, c.r [1], n2),
		ml_rat_div (arena, c.r [2], n2),
		ml_rat_div (arena, c.r [3], n2)
	);
}

/* ---- approximate parts ------------------------------------------------- */

static MlHyper approx_mul (MlHyper a, MlHyper b)
{
	return ml_hyper_approx (
		a.d [0] * b.d [0] - a.d [1] * b.d [1] - a.d [2] * b.d [2]
			- a.d [3] * b.d [3],
		a.d [0] * b.d [1] + a.d [1] * b.d [0] + a.d [2] * b.d [3]
			- a.d [3] * b.d [2],
		a.d [0] * b.d [2] - a.d [1] * b.d [3] + a.d [2] * b.d [0]
			+ a.d [3] * b.d [1],
		a.d [0] * b.d [3] + a.d [1] * b.d [2] - a.d [2] * b.d [1]
			+ a.d [3] * b.d [0]
	);
}

static MlHyper approx_inverse (MlHyper a)
{
	double n2 = a.d [0] * a.d [0] + a.d [1] * a.d [1] + a.d [2] * a.d [2]
				+ a.d [3] * a.d [3];

	return ml_hyper_approx (
		a.d [0] / n2,
		-a.d [1] / n2,
		-a.d [2] / n2,
		-a.d [3] / n2
	);
}

/* ---- operations -------------------------------------------------------- */

/* 1 when a value is zero in every part. */
static int32_t all_zero (MlHyper h)
{
	return is_zero (h, 0) && is_zero (h, 1) && is_zero (h, 2) && is_zero (h, 3);
}

/* h to a whole power by repeated squaring, 1/h for a negative one. */
static MlValue power_whole (MlArena *arena, MlHyper h, int64_t n)
{
	MlValue r = error_of (ML_EVAL_NONE);

	if (all_zero (h) && n <= 0)
	{
		r = error_of (
			ml_choose (n == 0, ML_EVAL_ZERO_POW_ZERO, ML_EVAL_DIV_ZERO)
		);
	}

	else if (n > 10000 || n < -10000)
	{
		r = error_of (ML_EVAL_TOO_LARGE);
	}

	else
	{
		uint64_t e    = (uint64_t)ml_choose (n < 0, -n, n);
		MlHyper  base = h;
		MlHyper  acc  = ml_choose (
			h.approx,
			ml_hyper_approx (1.0, 0.0, 0.0, 0.0),
			ml_hyper_exact (
				ml_rat_from_i64 (arena, 1),
				ml_rat_zero (arena),
				ml_rat_zero (arena),
				ml_rat_zero (arena)
			)
		);

		while (e > 0)
		{
			if (e & 1)
			{
				acc = ml_choose (
					h.approx,
					approx_mul (acc, base),
					exact_mul (arena, acc, base)
				);
			}

			base = ml_choose (
				h.approx,
				approx_mul (base, base),
				exact_mul (arena, base, base)
			);
			e >>= 1;
		}

		if (n < 0)
		{
			acc = ml_choose (
				h.approx,
				approx_inverse (acc),
				exact_inverse (arena, acc)
			);
		}

		r = ml_hyper_value (acc);
	}

	return r;
}

/* Complex functions through doubles: the parts a + b i of z. */
static MlHyper complex_exp (double a, double b)
{
	return ml_hyper_approx (exp (a) * cos (b), exp (a) * sin (b), 0.0, 0.0);
}

static MlHyper complex_ln (double a, double b)
{
	return ml_hyper_approx (log (hypot (a, b)), atan2 (b, a), 0.0, 0.0);
}

/* z^w for complex z and w, the principal value. */
static MlValue complex_pow (MlArena *arena, MlHyper z, MlHyper w)
{
	MlHyper za = to_approx (arena, z);
	MlHyper wa = to_approx (arena, w);
	MlValue r  = error_of (ML_EVAL_NONE);

	if (za.d [0] == 0.0 && za.d [1] == 0.0)
	{
		r = error_of (ml_choose (
			wa.d [0] > 0.0,
			ML_EVAL_NONE,
			ml_choose (
				wa.d [0] == 0.0 && wa.d [1] == 0.0,
				ML_EVAL_ZERO_POW_ZERO,
				ML_EVAL_DIV_ZERO
			)
		));

		if (r.error == ML_EVAL_NONE)
		{
			r = ml_value_exact (ml_rat_zero (arena));
		}
	}

	else
	{
		MlHyper ln   = complex_ln (za.d [0], za.d [1]);
		MlHyper prod = approx_mul (wa, ln);

		r = ml_hyper_value (complex_exp (prod.d [0], prod.d [1]));
	}

	return r;
}

MlValue ml_hyper_apply (MlArena *arena, MlHyperOp op, MlValue a, MlValue b)
{
	MlHyper x      = ml_hyper_of (arena, a);
	MlHyper y      = ml_hyper_of (arena, b);
	int32_t approx = x.approx || y.approx;
	int32_t quat   = a.kind == ML_VALUE_QUAT || b.kind == ML_VALUE_QUAT;
	MlValue r      = error_of (ML_EVAL_NONE);

	if (a.kind == ML_VALUE_ERROR)
	{
		r = a;
	}

	else if (b.kind == ML_VALUE_ERROR)
	{
		r = b;
	}

	else if (!((a.kind == ML_VALUE_EXACT || a.kind == ML_VALUE_APPROX
				|| ml_value_is_hyper (a))
			   && (b.kind == ML_VALUE_EXACT || b.kind == ML_VALUE_APPROX
				   || ml_value_is_hyper (b))))
	{
		r = error_of (ML_EVAL_TYPE);
	}

	else if (op == ML_HYPER_POW)
	{
		MlI64Result n = ml_value_to_i64 (b);

		if (n.ok)
		{
			r = power_whole (arena, x, n.value);
		}

		else if (b.kind == ML_VALUE_EXACT && ml_rat_is_integer (b.exact))
		{
			r = error_of (ML_EVAL_TOO_LARGE);
		}

		else
		{
			r = ml_choose (
				quat || b.kind == ML_VALUE_QUAT,
				error_of (ML_EVAL_TYPE),
				complex_pow (arena, x, y)
			);
		}
	}

	else if (approx)
	{
		MlHyper p = to_approx (arena, x);
		MlHyper q = to_approx (arena, y);

		if (op == ML_HYPER_ADD)
		{
			r = ml_hyper_value (ml_hyper_approx (
				p.d [0] + q.d [0],
				p.d [1] + q.d [1],
				p.d [2] + q.d [2],
				p.d [3] + q.d [3]
			));
		}

		else if (op == ML_HYPER_MUL)
		{
			r = ml_hyper_value (approx_mul (p, q));
		}

		else
		{
			r = ml_choose (
				all_zero (q),
				error_of (ML_EVAL_DIV_ZERO),
				ml_hyper_value (approx_mul (p, approx_inverse (q)))
			);
		}
	}

	else if (op == ML_HYPER_ADD)
	{
		r = ml_hyper_value (exact_add (arena, x, y));
	}

	else if (op == ML_HYPER_MUL)
	{
		r = ml_hyper_value (exact_mul (arena, x, y));
	}

	else
	{
		r = ml_choose (
			all_zero (y),
			error_of (ML_EVAL_DIV_ZERO),
			ml_hyper_value (exact_mul (arena, x, exact_inverse (arena, y)))
		);
	}

	return r;
}

MlValue ml_hyper_neg (MlArena *arena, MlValue a)
{
	MlHyper h = ml_hyper_of (arena, a);

	return ml_hyper_value (ml_choose (
		h.approx,
		ml_hyper_approx (-h.d [0], -h.d [1], -h.d [2], -h.d [3]),
		ml_hyper_exact (
			ml_rat_neg (arena, h.r [0]),
			ml_rat_neg (arena, h.r [1]),
			ml_rat_neg (arena, h.r [2]),
			ml_rat_neg (arena, h.r [3])
		)
	));
}

MlValue ml_hyper_sqrt_negative (MlArena *arena, MlRat negative)
{
	MlRat     magnitude = ml_rat_neg (arena, negative);
	MlRatSqrt root      = ml_rat_sqrt_exact (arena, magnitude);
	MlRat     zero      = ml_rat_zero (arena);

	return ml_hyper_value (ml_choose (
		root.exact,
		ml_hyper_exact (zero, root.value, zero, zero),
		ml_hyper_approx (
			0.0,
			sqrt (ml_rat_to_double (arena, magnitude)),
			0.0,
			0.0
		)
	));
}

MlValue ml_hyper_pow_negative (MlArena *arena, MlValue base, MlValue power)
{
	MlHyper z
		= ml_hyper_approx (ml_value_to_double (arena, base), 0.0, 0.0, 0.0);
	MlHyper w
		= ml_hyper_approx (ml_value_to_double (arena, power), 0.0, 0.0, 0.0);

	return complex_pow (arena, z, w);
}

/* ---- functions --------------------------------------------------------- */

/* sqrt (|a|^2) of the parts: exact when it is a rational. */
static MlValue modulus (MlArena *arena, MlHyper h)
{
	MlValue r;

	if (h.approx)
	{
		r = ml_value_approx (sqrt (
			h.d [0] * h.d [0] + h.d [1] * h.d [1] + h.d [2] * h.d [2]
			+ h.d [3] * h.d [3]
		));
	}

	else
	{
		MlRat     n2   = exact_norm2 (arena, h);
		MlRatSqrt root = ml_rat_sqrt_exact (arena, n2);

		r = ml_choose (
			root.exact,
			ml_value_exact (root.value),
			ml_value_approx (sqrt (ml_rat_to_double (arena, n2)))
		);
	}

	return r;
}

/* The vector part of a quaternion as a column of three numbers. */
static MlValue vector_part (MlArena *arena, MlHyper h)
{
	MlValue r = error_of (ML_EVAL_NOT_EXACT);

	if (!h.approx)
	{
		MlMat m = ml_mat_zero (arena, 3, 1);

		for (uint64_t i = 0; i < 3; i++)
		{
			m.cells [i] = h.r [i + 1];
		}

		r = ml_value_matrix (m);
	}

	return r;
}

MlValue ml_hyper_function (MlArena *arena, MlHyperFn which, MlValue a)
{
	MlHyper h    = ml_hyper_of (arena, a);
	MlHyper pa   = to_approx (arena, h);
	int32_t quat = a.kind == ML_VALUE_QUAT;
	MlValue r    = error_of (ML_EVAL_TYPE);
	MlRat   zero = ml_rat_zero (arena);

	if (a.kind != ML_VALUE_EXACT && a.kind != ML_VALUE_APPROX
		&& !ml_value_is_hyper (a))
	{
		r = ml_choose (a.kind == ML_VALUE_ERROR, a, error_of (ML_EVAL_TYPE));
	}

	else if (which == ML_HFN_RE)
	{
		r = ml_hyper_value (ml_choose (
			h.approx,
			ml_hyper_approx (h.d [0], 0.0, 0.0, 0.0),
			ml_hyper_exact (h.r [0], zero, zero, zero)
		));
	}

	else if (which == ML_HFN_IM)
	{
		r = ml_choose (
			quat,
			vector_part (arena, h),
			ml_hyper_value (ml_choose (
				h.approx,
				ml_hyper_approx (h.d [1], 0.0, 0.0, 0.0),
				ml_hyper_exact (h.r [1], zero, zero, zero)
			))
		);
	}

	else if (which == ML_HFN_CONJ)
	{
		r = ml_hyper_value (ml_choose (
			h.approx,
			ml_hyper_approx (h.d [0], -h.d [1], -h.d [2], -h.d [3]),
			exact_conj (arena, h)
		));
	}

	else if (which == ML_HFN_ABS)
	{
		r = modulus (arena, h);
	}

	else if (quat)
	{
		/* the rest are functions of a complex number only */
		r = error_of (ML_EVAL_TYPE);
	}

	else if (which == ML_HFN_ARG)
	{
		r = ml_choose (
			is_zero (h, 1) && !h.approx && ml_rat_sign (h.r [0]) > 0,
			ml_value_exact (zero),
			ml_choose (
				pa.d [0] == 0.0 && pa.d [1] == 0.0,
				error_of (ML_EVAL_DOMAIN),
				ml_value_approx (atan2 (pa.d [1], pa.d [0]))
			)
		);
	}

	else if (which == ML_HFN_EXP)
	{
		r = ml_hyper_value (complex_exp (pa.d [0], pa.d [1]));
	}

	else if (which == ML_HFN_LN)
	{
		r = ml_choose (
			pa.d [0] == 0.0 && pa.d [1] == 0.0,
			error_of (ML_EVAL_DOMAIN),
			ml_hyper_value (complex_ln (pa.d [0], pa.d [1]))
		);
	}

	else if (which == ML_HFN_SIN)
	{
		r = ml_hyper_value (ml_hyper_approx (
			sin (pa.d [0]) * cosh (pa.d [1]),
			cos (pa.d [0]) * sinh (pa.d [1]),
			0.0,
			0.0
		));
	}

	else if (which == ML_HFN_COS)
	{
		r = ml_hyper_value (ml_hyper_approx (
			cos (pa.d [0]) * cosh (pa.d [1]),
			-sin (pa.d [0]) * sinh (pa.d [1]),
			0.0,
			0.0
		));
	}

	else
	{
		double mod = hypot (pa.d [0], pa.d [1]);
		double re  = sqrt ((mod + pa.d [0]) / 2.0);
		double im  = sqrt ((mod - pa.d [0]) / 2.0);

		r = ml_hyper_value (
			ml_hyper_approx (re, ml_choose (pa.d [1] < 0.0, -im, im), 0.0, 0.0)
		);
	}

	return r;
}

MlValue ml_hyper_quat (MlArena *arena, const MlValue *args)
{
	int32_t exact = 1;
	int32_t ok    = 1;
	MlValue r;

	for (uint64_t i = 0; i < 4; i++)
	{
		ok = ok
			 && (args [i].kind == ML_VALUE_EXACT
				 || args [i].kind == ML_VALUE_APPROX);
		exact = exact && args [i].kind == ML_VALUE_EXACT;
	}

	if (!ok)
	{
		r = error_of (ML_EVAL_TYPE);
	}

	else if (exact)
	{
		r = ml_hyper_value (ml_hyper_exact (
			args [0].exact,
			args [1].exact,
			args [2].exact,
			args [3].exact
		));
	}

	else
	{
		r = ml_hyper_value (ml_hyper_approx (
			ml_value_to_double (arena, args [0]),
			ml_value_to_double (arena, args [1]),
			ml_value_to_double (arena, args [2]),
			ml_value_to_double (arena, args [3])
		));
	}

	return r;
}

MlValue ml_hyper_polar (MlArena *arena, MlValue r, MlValue theta)
{
	MlValue out = error_of (ML_EVAL_TYPE);

	if ((r.kind == ML_VALUE_EXACT || r.kind == ML_VALUE_APPROX)
		&& (theta.kind == ML_VALUE_EXACT || theta.kind == ML_VALUE_APPROX))
	{
		double m = ml_value_to_double (arena, r);
		double t = ml_value_to_double (arena, theta);

		out = ml_hyper_value (
			ml_hyper_approx (m * cos (t), m * sin (t), 0.0, 0.0)
		);
	}

	return out;
}

MlValue ml_hyper_rotate (MlArena *arena, MlValue q, MlValue v)
{
	MlValue out = error_of (ML_EVAL_TYPE);

	if ((q.kind == ML_VALUE_EXACT || ml_value_is_hyper (q))
		&& ml_value_is_matrix (v) && v.matrix.rows == 3 && v.matrix.cols == 1)
	{
		MlHyper a = ml_hyper_of (arena, q);

		if (a.approx)
		{
			out = error_of (ML_EVAL_NOT_EXACT);
		}

		else if (all_zero (a))
		{
			out = error_of (ML_EVAL_DIV_ZERO);
		}

		else
		{
			MlHyper p = ml_hyper_exact (
				ml_rat_zero (arena),
				v.matrix.cells [0],
				v.matrix.cells [1],
				v.matrix.cells [2]
			);
			MlHyper t = exact_mul (
				arena,
				exact_mul (arena, a, p),
				exact_inverse (arena, a)
			);

			out = vector_part (arena, t);
		}
	}

	return out;
}

/* ---- text -------------------------------------------------------------- */

/* The magnitude of a coefficient: "2", "(3/4)", or nothing for a 1 on a unit.
 */
static MlResult exact_coefficient (
	MlStr   *out,
	MlArena *arena,
	MlRat    x,
	uint64_t part
)
{
	MlRat    magnitude = ml_rat_abs (arena, x);
	MlResult r         = ML_OK;

	if (part > 0
		&& ml_rat_cmp (arena, magnitude, ml_rat_from_i64 (arena, 1)) == 0)
	{
		r = ML_OK;
	}

	else if (part > 0 && !ml_rat_is_integer (magnitude))
	{
		r = ml_str_append (out, ml_slice_cstr ("("));
		r = ml_choose (r == ML_OK, ml_rat_format (out, magnitude), r);
		r = ml_choose (r == ML_OK, ml_str_append (out, ml_slice_cstr (")")), r);
	}

	else
	{
		r = ml_rat_format (out, magnitude);
	}

	return r;
}

static const char *const unit_names [4] = { "", "i", "j", "k" };

/* Appends the terms of a sum of parts: the sign, the coefficient, the unit. */
static MlResult format_terms (MlStr *out, MlArena *arena, MlHyper h, double eps)
{
	MlResult r     = ML_OK;
	int32_t  first = 1;

	for (uint64_t p = 0; p < 4 && r == ML_OK; p++)
	{
		int32_t negative
			= ml_choose (h.approx, h.d [p] < 0.0, ml_rat_sign (h.r [p]) < 0);
		int32_t zero = ml_choose (
			h.approx,
			fabs (h.d [p]) <= eps,
			ml_rat_is_zero (h.r [p])
		);

		if (!zero)
		{
			if (first)
			{
				r = ml_choose (
					negative,
					ml_str_append (out, ml_slice_cstr ("-")),
					ML_OK
				);
			}

			else
			{
				r = ml_str_append (
					out,
					ml_slice_cstr (ml_choose (negative, " - ", " + "))
				);
			}

			if (r == ML_OK && h.approx)
			{
				/* a unit with the coefficient 1 shows no number */
				r = ml_choose (
					p > 0 && fabs (h.d [p]) == 1.0,
					ML_OK,
					ml_str_appendf (out, "%.*g", 9, fabs (h.d [p]))
				);
			}

			else if (r == ML_OK)
			{
				r = exact_coefficient (out, arena, h.r [p], p);
			}

			r = ml_choose (
				r == ML_OK,
				ml_str_append (out, ml_slice_cstr (unit_names [p])),
				r
			);
			first = 0;
		}
	}

	if (first && r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr ("0"));
	}

	return r;
}

MlResult ml_hyper_format (MlStr *out, MlArena *arena, MlHyper h)
{
	MlResult r   = ML_OK;
	double   eps = 0.0;

	if (h.approx)
	{
		double big = 0.0;

		for (uint64_t p = 0; p < 4; p++)
		{
			big = ml_choose (fabs (h.d [p]) > big, fabs (h.d [p]), big);
		}

		eps = big * 1e-12;
		r   = ml_str_append (out, ml_slice_cstr ("\xE2\x89\x88 "));
	}

	return ml_choose (r == ML_OK, format_terms (out, arena, h, eps), r);
}
