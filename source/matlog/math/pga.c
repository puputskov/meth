#include <matlog/math/pga.h>

#include <math.h>
#include <string.h>

/* ---- blades ------------------------------------------------------------ */

static uint32_t popcount (uint32_t x)
{
	uint32_t n = 0;

	while (x != 0)
	{
		n += x & 1;
		x >>= 1;
	}

	return n;
}

static uint32_t blades_of (int32_t dim)
{
	return 1u << (dim + 1);
}

/*
 * The sign of e_a e_b once its basis vectors are put in ascending order: every
 * vector of b that is lower than a vector of a has to pass it.
 */
static int32_t reorder_sign (uint32_t a, uint32_t b)
{
	uint32_t swaps = 0;

	for (uint32_t i = 0; i < 4; i++)
	{
		if ((a >> i) & 1)
		{
			swaps += popcount (b & ((1u << i) - 1));
		}
	}

	return ml_choose ((swaps & 1) != 0, -1, 1);
}

/* The sign of the geometric product of two blades; 0 when e0 meets itself. */
static int32_t geometric_sign (uint32_t a, uint32_t b)
{
	return ml_choose ((a & b & 1) != 0, 0, reorder_sign (a, b));
}

/* The sign of the outer product of two blades; 0 when they share a vector. */
static int32_t wedge_sign (uint32_t a, uint32_t b)
{
	return ml_choose ((a & b) != 0, 0, reorder_sign (a, b));
}

/* The grades whose reverse changes the sign: 2, 3, 6, 7. */
static int32_t reverse_sign (uint32_t mask)
{
	uint32_t g = popcount (mask);

	return ml_choose (((g * (g - 1) / 2) & 1) != 0, -1, 1);
}

/* The name of a blade: e12; the scalar has none. */
static void blade_name (char *buf, uint32_t mask)
{
	uint32_t n = 0;

	buf [n++] = 'e';

	for (uint32_t i = 0; i < 4; i++)
	{
		if ((mask >> i) & 1)
		{
			buf [n++] = (char)('0' + i);
		}
	}

	buf [n] = '\0';
}

/* ---- multivectors in the arena ----------------------------------------- */

static MlMv *mv_new (MlArena *arena, int32_t dim, int32_t approx)
{
	MlMv *m = ML_ARENA_NEW (arena, MlMv);

	if (m != NULL)
	{
		m->dim    = dim;
		m->approx = approx;

		for (uint32_t i = 0; i < ML_MV_MAX_BLADES; i++)
		{
			m->r [i] = ml_rat_zero (arena);
		}
	}

	return m;
}

int32_t ml_value_is_mv (MlValue v)
{
	return v.kind == ML_VALUE_MV;
}

static MlValue error_of (MlErrorKind kind)
{
	return ml_value_error (kind, 0, 0);
}

/* Is coefficient i zero? */
static int32_t is_zero (const MlMv *m, uint32_t i)
{
	return ml_choose (m->approx, m->d [i] == 0.0, ml_rat_is_zero (m->r [i]));
}

/* The multivector as doubles, in a new multivector. */
static MlMv *approximated (MlArena *arena, const MlMv *a)
{
	MlMv *m = mv_new (arena, a->dim, 1);

	for (uint32_t i = 0; m != NULL && i < ML_MV_MAX_BLADES; i++)
	{
		m->d [i] = ml_choose (
			a->approx,
			a->d [i],
			ml_rat_to_double (arena, a->r [i])
		);
	}

	return m;
}

/*
 * The value as a multivector of dimension dim: a number is a scalar, a
 * multivector is itself; NULL for anything else or when the dimensions differ.
 */
static const MlMv *as_mv (MlArena *arena, MlValue v, int32_t dim)
{
	const MlMv *m = NULL;

	if (v.kind == ML_VALUE_MV && (dim == 0 || v.mv->dim == dim))
	{
		m = v.mv;
	}

	else if (
		   (v.kind == ML_VALUE_EXACT || v.kind == ML_VALUE_APPROX)
		&& dim != 0
	)
	{
		MlMv *s = mv_new (arena, dim, v.kind == ML_VALUE_APPROX);

		if (s != NULL)
		{
			s->r [0] = ml_choose (
				v.kind == ML_VALUE_EXACT,
				v.exact,
				ml_rat_zero (arena)
			);
			s->d [0] = v.approx;
		}

		m = s;
	}

	return m;
}

/* The dimension two operands agree on, -1 when they are of different ones. */
static int32_t common_dim (MlValue a, MlValue b)
{
	int32_t da  = ml_choose (a.kind == ML_VALUE_MV, a.mv->dim, 0);
	int32_t db  = ml_choose (b.kind == ML_VALUE_MV, b.mv->dim, 0);
	int32_t dim = ml_choose (da != 0, da, db);

	return ml_choose (da != 0 && db != 0 && da != db, -1, dim);
}

/* A result: a plain number when only the scalar is left, else a multivector. */
static MlValue finish_mv (MlArena *arena, MlMv *m)
{
	MlValue  v        = ml_value_none ();
	int32_t  only     = 1;
	int32_t  good     = 1;
	uint32_t blades   = blades_of (m->dim);

	for (uint32_t i = 0; i < blades; i++)
	{
		only = only && (i == 0 || is_zero (m, i));
		good = good && ml_choose (
			m->approx,
			isfinite (m->d [i]) != 0,
			ml_rat_ok (m->r [i])
		);
	}

	if (!good)
	{
		v = error_of (ML_EVAL_DOMAIN);
	}

	else if (only)
	{
		v = ml_choose (
			m->approx,
			ml_value_approx (m->d [0]),
			ml_value_exact (m->r [0])
		);
	}

	else
	{
		v.kind = ML_VALUE_MV;
		v.mv   = m;
	}

	(void)arena;
	return v;
}

/* ---- the products and the sums ----------------------------------------- */

typedef enum Combine
{
	COMBINE_GEOMETRIC = 0,
	COMBINE_WEDGE,
	COMBINE_INNER
} Combine;

static int32_t combine_sign (Combine how, uint32_t i, uint32_t j)
{
	int32_t  s  = 0;
	uint32_t gi = popcount (i);
	uint32_t gj = popcount (j);
	uint32_t gd = ml_choose (gi > gj, gi - gj, gj - gi);

	if (how == COMBINE_WEDGE)
	{
		s = wedge_sign (i, j);
	}

	else
	{
		s = geometric_sign (i, j);

		if (how == COMBINE_INNER && popcount (i ^ j) != gd)
		{
			s = 0;
		}
	}

	return s;
}

/* Adds sign * x to coefficient k. */
static void add_term (
	MlArena *arena,
	MlMv    *out,
	uint32_t k,
	int32_t  sign,
	MlRat    x,
	double   d
)
{
	if (out->approx)
	{
		out->d [k] += (double)sign * d;
	}

	else
	{
		out->r [k] = ml_choose (
			sign > 0,
			ml_rat_add (arena, out->r [k], x),
			ml_rat_sub (arena, out->r [k], x)
		);
	}
}

/* a * b, a ^ b or the inner product, in a new multivector. */
static MlMv *combined (
	MlArena    *arena,
	const MlMv *a,
	const MlMv *b,
	Combine    how
)
{
	int32_t     approx = a->approx || b->approx;
	const MlMv  *x     = a;
	const MlMv  *y     = b;
	MlMv        *out   = mv_new (arena, a->dim, approx);
	uint32_t    blades = blades_of (a->dim);

	if (approx && !a->approx)
	{
		x = approximated (arena, a);
	}

	if (approx && !b->approx)
	{
		y = approximated (arena, b);
	}

	for (uint32_t i = 0; i < blades && out != NULL; i++)
	{
		for (uint32_t j = 0; j < blades; j++)
		{
			int32_t sign = combine_sign (how, i, j);

			if (sign != 0 && !is_zero (x, i) && !is_zero (y, j))
			{
				add_term (
					arena,
					out,
					i ^ j,
					sign,
					ml_choose (
						approx,
						ml_rat_zero (arena),
						ml_rat_mul (arena, x->r [i], y->r [j])
					),
					ml_choose (approx, x->d [i] * y->d [j], 0.0)
				);
			}
		}
	}

	return ml_choose (x != NULL && y != NULL, out, NULL);
}

/* a + b or a - b (sign is 1 or -1), in a new multivector. */
static MlMv *summed (
	MlArena    *arena,
	const MlMv *a,
	const MlMv *b,
	int32_t    sign
)
{
	int32_t    approx = a->approx || b->approx;
	const MlMv *x     = ml_choose (
		approx && !a->approx,
		approximated (arena, a),
		a
	);
	const MlMv *y     = ml_choose (
		approx && !b->approx,
		approximated (arena, b),
		b
	);
	MlMv       *out   = mv_new (arena, a->dim, approx);
	uint32_t   blades = blades_of (a->dim);

	if (out != NULL && x != NULL && y != NULL)
	{
		for (uint32_t i = 0; i < blades; i++)
		{
			add_term (arena, out, i, 1, x->r [i], x->d [i]);
			add_term (arena, out, i, sign, y->r [i], y->d [i]);
		}
	}

	else
	{
		out = NULL;
	}

	return out;
}

typedef enum MapKind
{
	MAP_REVERSE = 0,
	MAP_DUAL,
	MAP_UNDUAL,
	MAP_NEGATE
} MapKind;

/* Every blade goes to another blade (or itself) with a sign. */
static MlMv *mapped (MlArena *arena, const MlMv *a, MapKind kind)
{
	MlMv     *out    = mv_new (arena, a->dim, a->approx);
	uint32_t blades  = blades_of (a->dim);
	uint32_t full    = blades - 1;

	for (uint32_t i = 0; out != NULL && i < blades; i++)
	{
		if (!is_zero (a, i))
		{
			uint32_t k    = i;
			int32_t  sign = 1;

			switch (kind)
			{
				case MAP_REVERSE:
				{
					sign = reverse_sign (i);
				} break;

				case MAP_DUAL:
				{
					k    = full ^ i;
					sign = reorder_sign (i, k);
				} break;

				case MAP_UNDUAL:
				{
					k    = full ^ i;
					sign = reorder_sign (k, i);
				} break;

				case MAP_NEGATE:
				default:
				{
					sign = -1;
				} break;
			}

			add_term (arena, out, k, sign, a->r [i], a->d [i]);
		}
	}

	return out;
}

/* The blades of the grade k. */
static MlMv *graded (MlArena *arena, const MlMv *a, uint32_t k)
{
	MlMv     *out    = mv_new (arena, a->dim, a->approx);
	uint32_t blades  = blades_of (a->dim);

	for (uint32_t i = 0; out != NULL && i < blades; i++)
	{
		if (popcount (i) == k)
		{
			add_term (arena, out, i, 1, a->r [i], a->d [i]);
		}
	}

	return out;
}

/* a * b when b was made, else NULL. */
static MlMv *self_product (MlArena *arena, const MlMv *a, const MlMv *b)
{
	return ml_choose (
		b != NULL,
		combined (arena, a, b, COMBINE_GEOMETRIC),
		NULL
	);
}

/* The multivector divided by a non zero scalar (a rational or a double). */
static MlMv *divided (MlArena *arena, const MlMv *a, MlRat r, double d)
{
	MlMv     *out    = mv_new (arena, a->dim, a->approx);
	uint32_t blades  = blades_of (a->dim);

	for (uint32_t i = 0; out != NULL && i < blades; i++)
	{
		out->r [i] = ml_choose (
			a->approx,
			ml_rat_zero (arena),
			ml_rat_div (arena, a->r [i], r)
		);
		out->d [i] = ml_choose (a->approx, a->d [i] / d, 0.0);
	}

	return out;
}

/* Is the multivector a scalar, up to rounding when it is approximate? */
static int32_t only_scalar (const MlMv *m)
{
	int32_t  only   = 1;
	double   big    = fabs (m->d [0]);
	uint32_t blades = blades_of (m->dim);

	for (uint32_t i = 1; i < blades; i++)
	{
		big = ml_choose (
			m->approx && fabs (m->d [i]) > big,
			fabs (m->d [i]),
			big
		);
	}

	for (uint32_t i = 1; i < blades; i++)
	{
		only = only && ml_choose (
			m->approx,
			fabs (m->d [i]) <= big * 1e-12,
			ml_rat_is_zero (m->r [i])
		);
	}

	return only;
}

/*
 * The inverse of a versor: reverse (a) / (a * reverse (a)). NULL with err set
 * when a * reverse (a) is not a non zero scalar.
 */
static MlMv *inverse_of (MlArena *arena, const MlMv *a, MlErrorKind *err)
{
	MlMv *rev  = mapped (arena, a, MAP_REVERSE);
	MlMv *prod = self_product (arena, a, rev);
	MlMv *out  = NULL;

	*err = ML_EVAL_NO_MEMORY;

	if (prod != NULL)
	{
		int32_t zero = is_zero (prod, 0);

		if (!only_scalar (prod))
		{
			*err = ML_EVAL_DOMAIN;
		}

		else if (zero)
		{
			*err = ML_EVAL_DIV_ZERO;
		}

		else
		{
			out  = divided (arena, rev, prod->r [0], prod->d [0]);
			*err = ML_EVAL_NONE;
		}
	}

	return out;
}

/* The scalar part as a number. */
static MlValue scalar_value (const MlMv *m)
{
	return ml_choose (
		m->approx,
		ml_value_approx (m->d [0]),
		ml_value_exact (m->r [0])
	);
}

/* sqrt (<a reverse (a)>), the Euclidean length: exact when it can be. */
static MlValue norm_of (MlArena *arena, const MlMv *a)
{
	MlMv    *rev  = mapped (arena, a, MAP_REVERSE);
	MlMv    *prod = self_product (arena, a, rev);
	MlValue v     = error_of (ML_EVAL_NO_MEMORY);

	if (prod != NULL && prod->approx)
	{
		v = ml_value_approx (sqrt (fabs (prod->d [0])));
	}

	else if (prod != NULL)
	{
		MlRat     size = ml_rat_abs (arena, prod->r [0]);
		MlRatSqrt root = ml_rat_sqrt_exact (arena, size);

		v = ml_choose (
			root.exact,
			ml_value_exact (root.value),
			ml_value_approx (sqrt (ml_rat_to_double (arena, size)))
		);
	}

	return v;
}

/* ---- the operations on values ------------------------------------------ */

static MlValue result_of (MlArena *arena, MlMv *m)
{
	return ml_choose (
		m != NULL,
		finish_mv (arena, m),
		error_of (ML_EVAL_NO_MEMORY)
	);
}

/* a to an integer power by repeated products; a negative one inverts first. */
static MlValue power_of (MlArena *arena, const MlMv *a, int64_t n)
{
	MlErrorKind err  = ML_EVAL_NONE;
	const MlMv  *base = a;
	MlMv        *acc  = mv_new (arena, a->dim, a->approx);
	uint64_t    k     = (uint64_t)ml_choose (n < 0, -n, n);

	if (acc != NULL)
	{
		acc->r [0] = ml_rat_from_i64 (arena, 1);
		acc->d [0] = 1.0;
	}

	if (n < 0)
	{
		base = inverse_of (arena, a, &err);
	}

	while (acc != NULL && base != NULL && k > 0)
	{
		if ((k & 1) != 0)
		{
			acc = combined (arena, acc, base, COMBINE_GEOMETRIC);
		}

		k >>= 1;

		if (k > 0 && acc != NULL)
		{
			base = combined (arena, base, base, COMBINE_GEOMETRIC);
		}
	}

	return ml_choose (
		base == NULL && err != ML_EVAL_NONE,
		error_of (err),
		result_of (arena, acc)
	);
}

MlValue ml_mv_apply (MlArena *arena, MlMvOp op, MlValue a, MlValue b)
{
	/* two plain numbers (a term that came out as a scalar) are the scalars of
	   the algebra of the plane: the same sums and products, a number again */
	int32_t     dim = common_dim (a, b);
	int32_t     use = ml_choose (dim == 0, 2, dim);
	const MlMv  *x  = ml_choose (dim >= 0, as_mv (arena, a, use), NULL);
	const MlMv  *y  = ml_choose (dim >= 0, as_mv (arena, b, use), NULL);
	MlMv        *m  = NULL;
	MlValue     v   = error_of (ML_EVAL_TYPE);

	if (x != NULL && y != NULL)
	{
		MlErrorKind err = ML_EVAL_NONE;

		switch (op)
		{
			case ML_MV_ADD:
			{
				m = summed (arena, x, y, 1);
				v = result_of (arena, m);
			} break;

			case ML_MV_SUB:
			{
				m = summed (arena, x, y, -1);
				v = result_of (arena, m);
			} break;

			case ML_MV_MUL:
			{
				m = combined (arena, x, y, COMBINE_GEOMETRIC);
				v = result_of (arena, m);
			} break;

			case ML_MV_DIV:
			{
				const MlMv *inv = inverse_of (arena, y, &err);

				v = ml_choose (
					inv != NULL,
					result_of (
						arena,
						combined (arena, x, inv, COMBINE_GEOMETRIC)
					),
					error_of (err)
				);
			} break;

			case ML_MV_WEDGE:
			{
				m = combined (arena, x, y, COMBINE_WEDGE);
				v = result_of (arena, m);
			} break;

			case ML_MV_INNER:
			{
				m = combined (arena, x, y, COMBINE_INNER);
				v = result_of (arena, m);
			} break;

			case ML_MV_JOIN:
			{
				MlMv *da = mapped (arena, x, MAP_DUAL);
				MlMv *db = mapped (arena, y, MAP_DUAL);
				MlMv *w  = ml_choose (
					da != NULL && db != NULL,
					combined (arena, da, db, COMBINE_WEDGE),
					NULL
				);

				m = ml_choose (w != NULL, mapped (arena, w, MAP_UNDUAL), NULL);
				v = result_of (arena, m);
			} break;

			case ML_MV_POW:
			default:
			{
				/* a multivector to a whole power; with a multivector as the
				   exponent the ^ is the outer product */
				MlI64Result n = ml_value_to_i64 (b);

				if (b.kind == ML_VALUE_MV || a.kind != ML_VALUE_MV)
				{
					m = combined (arena, x, y, COMBINE_WEDGE);
					v = result_of (arena, m);
				}

				else if (n.ok && n.value >= -256 && n.value <= 256)
				{
					v = power_of (arena, x, n.value);
				}
			} break;
		}
	}

	return v;
}

MlValue ml_mv_neg (MlArena *arena, MlValue a)
{
	return ml_choose (
		a.kind == ML_VALUE_MV,
		result_of (arena, mapped (arena, a.mv, MAP_NEGATE)),
		error_of (ML_EVAL_TYPE)
	);
}

MlValue ml_mv_function (MlArena *arena, MlMvFn fn, MlValue a)
{
	MlValue v = error_of (ML_EVAL_TYPE);

	if (a.kind != ML_VALUE_MV)
	{
		/* a number is its own reverse and its own scalar part */
		v = ml_choose (
			(fn == ML_MVFN_REVERSE || fn == ML_MVFN_SCALAR)
			&& (a.kind == ML_VALUE_EXACT || a.kind == ML_VALUE_APPROX),
			a,
			v
		);
	}

	else
	{
		MlErrorKind err = ML_EVAL_NONE;

		switch (fn)
		{
			case ML_MVFN_REVERSE:
			{
				v = result_of (arena, mapped (arena, a.mv, MAP_REVERSE));
			} break;

			case ML_MVFN_DUAL:
			{
				v = result_of (arena, mapped (arena, a.mv, MAP_DUAL));
			} break;

			case ML_MVFN_UNDUAL:
			{
				v = result_of (arena, mapped (arena, a.mv, MAP_UNDUAL));
			} break;

			case ML_MVFN_INVERSE:
			{
				const MlMv *inv = inverse_of (arena, a.mv, &err);

				v = ml_choose (
					inv != NULL,
					result_of (arena, (MlMv *)inv),
					error_of (err)
				);
			} break;

			case ML_MVFN_NORM:
			{
				v = norm_of (arena, a.mv);
			} break;

			case ML_MVFN_UNIT:
			{
				MlValue n = norm_of (arena, a.mv);

				v = ml_choose (
					n.kind == ML_VALUE_ERROR,
					n,
					ml_mv_apply (arena, ML_MV_DIV, a, n)
				);
			} break;

			case ML_MVFN_SCALAR:
			default:
			{
				v = scalar_value (a.mv);
			} break;
		}
	}

	return v;
}

MlValue ml_mv_grade (MlArena *arena, MlValue a, MlValue k)
{
	MlI64Result n = ml_value_to_i64 (k);
	MlValue     v = error_of (ML_EVAL_TYPE);

	if (a.kind == ML_VALUE_MV && n.ok)
	{
		v = ml_choose (
			n.value >= 0 && n.value <= a.mv->dim + 1,
			result_of (arena, graded (arena, a.mv, (uint32_t)n.value)),
			ml_value_exact (ml_rat_zero (arena))
		);
	}

	return v;
}

/* ---- blades by name and objects ---------------------------------------- */

/* Reads e12: the mask of the basis vectors and the sign of their order. */
static int32_t parse_blade (
	int32_t  dim,
	MlSlice  name,
	uint32_t *mask,
	int32_t  *sign
)
{
	int32_t ok = dim >= 2 && dim <= 3 && name.len >= 2 && name.len <= 5;

	*mask = 0;
	*sign = 1;
	ok    = ok && name.data [0] == 'e';

	for (uint64_t i = 1; ok && i < name.len; i++)
	{
		int32_t d = (int32_t)name.data [i] - '0';

		ok = d >= 0 && d <= dim && ((*mask >> d) & 1) == 0;

		if (ok)
		{
			/* the vectors already seen that are higher than this one */
			*sign  = ml_choose (
				(popcount (*mask >> d) & 1) != 0,
				-*sign,
				*sign
			);
			*mask |= 1u << d;
		}
	}

	return ok;
}

int32_t ml_mv_is_blade (int32_t dim, MlSlice name)
{
	uint32_t mask;
	int32_t  sign;

	return parse_blade (dim, name, &mask, &sign);
}

MlValue ml_mv_blade (MlArena *arena, int32_t dim, MlSlice name)
{
	MlValue  v    = ml_value_none ();
	uint32_t mask = 0;
	int32_t  sign = 1;

	if (parse_blade (dim, name, &mask, &sign))
	{
		MlMv *m = mv_new (arena, dim, 0);

		if (m != NULL)
		{
			m->r [mask] = ml_rat_from_i64 (arena, sign);
			v.kind      = ML_VALUE_MV;
			v.mv        = m;
		}
	}

	return v;
}

static int32_t number_double (MlArena *arena, MlValue v, double *out)
{
	int32_t ok = v.kind == ML_VALUE_EXACT || v.kind == ML_VALUE_APPROX;

	*out = ml_choose (
		v.kind == ML_VALUE_EXACT,
		ml_rat_to_double (arena, v.exact),
		v.approx
	);

	return ok;
}

/* The sum of coef [i] * the blade named names [i], i < n. */
static MlValue combination (
	MlArena         *arena,
	int32_t         dim,
	const char *const *names,
	const MlValue   *coef,
	uint64_t        n
)
{
	MlValue sum = ml_value_exact (ml_rat_zero (arena));

	for (uint64_t i = 0; i < n && sum.kind != ML_VALUE_ERROR; i++)
	{
		MlValue blade = ml_mv_blade (arena, dim, ml_slice_cstr (names [i]));

		sum = ml_mv_apply (
			arena,
			ML_MV_ADD,
			sum,
			ml_mv_apply (arena, ML_MV_MUL, coef [i], blade)
		);
	}

	return sum;
}

static MlValue point_of (MlArena *arena, const MlValue *a, uint64_t argc)
{
	static const char *const names2 [] = { "e12", "e20", "e01" };
	static const char *const names3 [] = { "e123", "e032", "e013", "e021" };
	MlValue                  c [4];

	/* the origin e12 (e123) with the coordinates on the three (four) blades */
	c [0] = ml_value_from_i64 (arena, 1);

	for (uint64_t i = 0; i < argc; i++)
	{
		c [i + 1] = a [i];
	}

	return ml_choose (
		argc == 2,
		combination (arena, 2, names2, c, 3),
		combination (arena, 3, names3, c, 4)
	);
}

static MlValue translator_of (MlArena *arena, const MlValue *a, uint64_t argc)
{
	static const char *const names2 [] = { "e01", "e02" };
	static const char *const names3 [] = { "e01", "e02", "e03" };
	/* minus half the distance: m * x * reverse (m) then moves x by it */
	MlValue                  half      = ml_value_div (
		arena,
		ml_value_from_i64 (arena, -1),
		ml_value_from_i64 (arena, 2)
	);
	MlValue                  c [3];
	MlValue                  sum;

	for (uint64_t i = 0; i < argc; i++)
	{
		c [i] = ml_value_mul (arena, a [i], half);
	}

	sum = ml_choose (
		argc == 2,
		combination (arena, 2, names2, c, 2),
		combination (arena, 3, names3, c, 3)
	);

	return ml_mv_apply (arena, ML_MV_ADD, ml_value_from_i64 (arena, 1), sum);
}

static MlValue rotor_of (MlArena *arena, const MlValue *a, uint64_t argc)
{
	double angle = 0.0;
	double n [3] = { 0.0, 0.0, 1.0 };
	int32_t ok   = number_double (arena, a [0], &angle);
	MlValue v    = error_of (ML_EVAL_TYPE);

	for (uint64_t i = 1; i < argc; i++)
	{
		ok = ok && number_double (arena, a [i], &n [i - 1]);
	}

	if (ok && a [0].kind == ML_VALUE_EXACT && ml_rat_is_zero (a [0].exact))
	{
		v = ml_value_from_i64 (arena, 1);
	}

	else if (ok)
	{
		static const char *const names2 [] = { "e12" };
		static const char *const names3 [] = { "e23", "e31", "e12" };
		double length = sqrt (n [0] * n [0] + n [1] * n [1] + n [2] * n [2]);
		double s      = sin (angle / 2.0);
		MlValue c [3];

		/* a rotation by angle counter clockwise about the axis: exp (-angle/2
		   times the unit bivector of the plane) */
		if (argc == 1)
		{
			c [0] = ml_value_approx (-s);
			v     = ml_mv_apply (
				arena,
				ML_MV_ADD,
				ml_value_approx (cos (angle / 2.0)),
				combination (arena, 2, names2, c, 1)
			);
		}

		else if (length > 0.0)
		{
			for (uint64_t i = 0; i < 3; i++)
			{
				c [i] = ml_value_approx (-s * n [i] / length);
			}

			v = ml_mv_apply (
				arena,
				ML_MV_ADD,
				ml_value_approx (cos (angle / 2.0)),
				combination (arena, 3, names3, c, 3)
			);
		}

		else
		{
			v = error_of (ML_EVAL_DOMAIN);
		}
	}

	return v;
}

MlValue ml_mv_make (
	MlArena       *arena,
	MlMvMake      what,
	const MlValue *args,
	uint64_t      argc
)
{
	static const char *const line_names [] = { "e1", "e2", "e0" };
	static const char *const plane_names [] = { "e1", "e2", "e3", "e0" };
	MlValue                  v = error_of (ML_EVAL_TYPE);
	int32_t                  ok = 1;

	for (uint64_t i = 0; i < argc; i++)
	{
		ok = ok && (
			   args [i].kind == ML_VALUE_EXACT
			|| args [i].kind == ML_VALUE_APPROX
		);
	}

	if (ok)
	{
		switch (what)
		{
			case ML_MVMAKE_POINT:
			{
				v = ml_choose (
					argc == 2 || argc == 3,
					point_of (arena, args, argc),
					v
				);
			} break;

			case ML_MVMAKE_LINE:
			{
				v = ml_choose (
					argc == 3,
					combination (arena, 2, line_names, args, 3),
					v
				);
			} break;

			case ML_MVMAKE_PLANE:
			{
				v = ml_choose (
					argc == 4,
					combination (arena, 3, plane_names, args, 4),
					v
				);
			} break;

			case ML_MVMAKE_TRANSLATOR:
			{
				v = ml_choose (
					argc == 2 || argc == 3,
					translator_of (arena, args, argc),
					v
				);
			} break;

			case ML_MVMAKE_ROTOR:
			default:
			{
				v = ml_choose (
					argc == 1 || argc == 4,
					rotor_of (arena, args, argc),
					v
				);
			} break;
		}
	}

	return v;
}

/* ---- text and copies --------------------------------------------------- */

static MlResult exact_coefficient (
	MlStr    *out,
	MlArena  *arena,
	MlRat    x,
	uint32_t mask
)
{
	MlRat    magnitude = ml_rat_abs (arena, x);
	MlRat    one       = ml_rat_from_i64 (arena, 1);
	MlResult r         = ML_OK;

	if (mask != 0 && ml_rat_cmp (arena, magnitude, one) == 0)
	{
		r = ML_OK;
	}

	else if (mask != 0 && !ml_rat_is_integer (magnitude))
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

MlResult ml_mv_format (MlStr *out, MlArena *arena, const MlMv *m)
{
	MlResult r      = ML_OK;
	int32_t  first  = 1;
	uint32_t blades = blades_of (m->dim);
	uint32_t top    = (uint32_t)m->dim + 1;
	double   eps    = 0.0;

	if (m->approx)
	{
		for (uint32_t i = 0; i < blades; i++)
		{
			eps = ml_choose (fabs (m->d [i]) > eps, fabs (m->d [i]), eps);
		}

		eps = eps * 1e-12;
		r   = ml_str_append (out, ml_slice_cstr ("\xE2\x89\x88 "));
	}

	/* by grade, then by blade */
	for (uint32_t g = 0; g <= top && r == ML_OK; g++)
	{
		for (uint32_t i = 0; i < blades && r == ML_OK; i++)
		{
			int32_t negative = ml_choose (
				m->approx,
				m->d [i] < 0.0,
				ml_rat_sign (m->r [i]) < 0
			);
			int32_t zero     = ml_choose (
				m->approx,
				fabs (m->d [i]) <= eps,
				ml_rat_is_zero (m->r [i])
			);

			if (popcount (i) == g && !zero)
			{
				char name [8];

				blade_name (name, i);

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

				if (r == ML_OK && m->approx)
				{
					r = ml_choose (
						i != 0 && fabs (m->d [i]) == 1.0,
						ML_OK,
						ml_str_appendf (out, "%.*g", 9, fabs (m->d [i]))
					);
				}

				else if (r == ML_OK)
				{
					r = exact_coefficient (out, arena, m->r [i], i);
				}

				r = ml_choose (
					r == ML_OK && i != 0,
					ml_str_append (out, ml_slice_cstr (name)),
					r
				);
				first = 0;
			}
		}
	}

	if (first && r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr ("0"));
	}

	return r;
}

MlValue ml_mv_copy (MlArena *arena, MlValue v)
{
	MlValue out = v;
	MlMv    *m  = ml_choose (
		v.kind == ML_VALUE_MV,
		mv_new (arena, v.mv->dim, v.mv->approx),
		NULL
	);

	if (m != NULL)
	{
		for (uint32_t i = 0; i < ML_MV_MAX_BLADES; i++)
		{
			m->r [i] = ml_choose (
				v.mv->approx,
				ml_rat_zero (arena),
				ml_rat_copy (arena, v.mv->r [i])
			);
			m->d [i] = v.mv->d [i];
		}

		out.mv = m;
	}

	else if (v.kind == ML_VALUE_MV)
	{
		out = error_of (ML_EVAL_NO_MEMORY);
	}

	return out;
}
