#include <matlog/math/eval.h>

#include <math.h>

#include <matlog/math/vector.h>

/* ---- context ------------------------------------------------------------ */

/*
 * The evaluator does not recurse. It walks the tree with explicit stacks that
 * live on the heap, so the C stack use is the same for every expression:
 *
 *	frames   the nodes being evaluated, from the root down to the current one
 *	vals     the results of finished children, waiting for their parent
 *	scopes   the text the nodes belong to: the line, then the body of every
 *	         user function that is running
 *
 * A frame visits its children one at a time (step counts the finished ones),
 * and when the last one is done it combines the results on top of vals into
 * its own result. An error result ends its parent at once, so the first
 * error wins and the rest of the parent is not evaluated.
 */

typedef enum MlCallKind
{
	CALL_NEW = 0, /* not looked at yet */
	CALL_UNKNOWN,
	CALL_PRODUCT, /* a(2) with a a number: a times 2 */
	CALL_USER,
	CALL_BUILTIN
} MlCallKind;

typedef struct MlFrame
{
	uint64_t   node;  /* index in the tree of the frame's scope */
	uint64_t   scope; /* index in scopes */
	uint64_t   step;  /* children (or for a user call, phases) done */
	MlCallKind kind;  /* calls only */
} MlFrame;

/* The text a node belongs to: the line, or the body of a running function. */
typedef struct MlScope
{
	MlSlice       source;
	MlAst         ast;   /* a view; the line or the binding owns the nodes */
	MlBinding     func;  /* the running function, ML_BIND_NONE on the line */
	const MlValue *args; /* its arguments, one per parameter */
} MlScope;

ML_VEC_DEFINE (MlFrameVec, MlFrame)
ML_VEC_DEFINE (MlValueVec, MlValue)
ML_VEC_DEFINE (MlScopeVec, MlScope)

typedef struct MlEvalCtx
{
	MlArena     *arena;
	const MlEnv *env;
	uint64_t    snapshot;
	MlFrameVec  frames;
	MlValueVec  vals;
	MlScopeVec  scopes;
	int32_t     failed; /* a stack could not grow: out of memory */
} MlEvalCtx;

static MlValue error_at (MlErrorKind kind)
{
	return ml_value_error (kind, 0, 0);
}

/* ---- built in functions and constants ----------------------------------- */

typedef enum MlBuiltin
{
	BUILTIN_NONE = 0,
	BUILTIN_SQRT,
	BUILTIN_ABS,
	BUILTIN_SIN,
	BUILTIN_COS,
	BUILTIN_TAN,
	BUILTIN_ASIN,
	BUILTIN_ACOS,
	BUILTIN_ATAN,
	BUILTIN_LN,
	BUILTIN_LOG,
	BUILTIN_EXP,
	BUILTIN_FLOOR,
	BUILTIN_CEIL,
	BUILTIN_ROUND,
	BUILTIN_MIN,
	BUILTIN_MAX,
	BUILTIN_GCD,
	BUILTIN_LCM,
	/* matrices and vectors from here on */
	BUILTIN_DET,
	BUILTIN_INV,
	BUILTIN_TRANSPOSE,
	BUILTIN_TRACE,
	BUILTIN_RANK,
	BUILTIN_RREF,
	BUILTIN_SOLVE,
	BUILTIN_IDENTITY,
	BUILTIN_DOT,
	BUILTIN_CROSS,
	BUILTIN_NORM,
	BUILTIN_COUNT
} MlBuiltin;

/* Exact results of the real functions at 0 and 1. */
typedef enum MlSpecial
{
	SPECIAL_NONE = 0,
	F0_IS_0,
	F0_IS_1,
	F1_IS_0
} MlSpecial;

#define ANY_ARGS 1000000

typedef struct MlBuiltinInfo
{
	const char *name;
	uint64_t   min; /* fewest arguments */
	uint64_t   max;
	MlSpecial  at;
} MlBuiltinInfo;

static const MlBuiltinInfo builtins [BUILTIN_COUNT] =
{
	[BUILTIN_SQRT]  = { .name = "sqrt",  .min = 1, .max = 1 },
	[BUILTIN_ABS]   = { .name = "abs",   .min = 1, .max = 1 },
	[BUILTIN_SIN]   = { .name = "sin",   .min = 1, .max = 1, .at = F0_IS_0 },
	[BUILTIN_COS]   = { .name = "cos",   .min = 1, .max = 1, .at = F0_IS_1 },
	[BUILTIN_TAN]   = { .name = "tan",   .min = 1, .max = 1, .at = F0_IS_0 },
	[BUILTIN_ASIN]  = { .name = "asin",  .min = 1, .max = 1, .at = F0_IS_0 },
	[BUILTIN_ACOS]  = { .name = "acos",  .min = 1, .max = 1 },
	[BUILTIN_ATAN]  = { .name = "atan",  .min = 1, .max = 1, .at = F0_IS_0 },
	[BUILTIN_LN]    = { .name = "ln",    .min = 1, .max = 1, .at = F1_IS_0 },
	[BUILTIN_LOG]   = { .name = "log",   .min = 1, .max = 1, .at = F1_IS_0 },
	[BUILTIN_EXP]   = { .name = "exp",   .min = 1, .max = 1, .at = F0_IS_1 },
	[BUILTIN_FLOOR] = { .name = "floor", .min = 1, .max = 1 },
	[BUILTIN_CEIL]  = { .name = "ceil",  .min = 1, .max = 1 },
	[BUILTIN_ROUND] = { .name = "round", .min = 1, .max = 1 },
	[BUILTIN_MIN]   = { .name = "min",   .min = 2, .max = ANY_ARGS },
	[BUILTIN_MAX]   = { .name = "max",   .min = 2, .max = ANY_ARGS },
	[BUILTIN_GCD]   = { .name = "gcd",   .min = 2, .max = 2 },
	[BUILTIN_LCM]   = { .name = "lcm",   .min = 2, .max = 2 },
	[BUILTIN_DET]       = { .name = "det",       .min = 1, .max = 1 },
	[BUILTIN_INV]       = { .name = "inv",       .min = 1, .max = 1 },
	[BUILTIN_TRANSPOSE] = { .name = "transpose", .min = 1, .max = 1 },
	[BUILTIN_TRACE]     = { .name = "trace",     .min = 1, .max = 1 },
	[BUILTIN_RANK]      = { .name = "rank",      .min = 1, .max = 1 },
	[BUILTIN_RREF]      = { .name = "rref",      .min = 1, .max = 1 },
	[BUILTIN_SOLVE]     = { .name = "solve",     .min = 2, .max = 2 },
	[BUILTIN_IDENTITY]  = { .name = "identity",  .min = 1, .max = 1 },
	[BUILTIN_DOT]       = { .name = "dot",       .min = 2, .max = 2 },
	[BUILTIN_CROSS]     = { .name = "cross",     .min = 2, .max = 2 },
	[BUILTIN_NORM]      = { .name = "norm",      .min = 1, .max = 1 }
};

static MlBuiltin builtin_find (MlSlice name)
{
	MlBuiltin found = BUILTIN_NONE;

	for (uint64_t i = 1; i < BUILTIN_COUNT; i++)
	{
		if (ml_slice_equal (name, ml_slice_cstr (builtins [i].name)))
		{
			found = (MlBuiltin)i;
		}
	}

	return found;
}

/* The value of pi or e, ML_VALUE_NONE for any other name. */
static MlValue constant_find (MlSlice name)
{
	MlValue v = ml_value_none ();

	if (
		   ml_slice_equal (name, ml_slice_cstr ("pi"))
		|| ml_slice_equal (name, ml_slice_cstr ("\xCF\x80"))
	)
	{
		v = ml_value_approx (3.14159265358979323846);
	}

	else if (ml_slice_equal (name, ml_slice_cstr ("e")))
	{
		v = ml_value_approx (2.71828182845904523536);
	}

	return v;
}

/* ---- numeric helpers ---------------------------------------------------- */

static MlValue half_value (MlArena *arena)
{
	return ml_value_exact (
		ml_rat_make (
			arena,
			ml_big_from_u64 (arena, 1),
			ml_big_from_u64 (arena, 2)
		)
	);
}

static MlValue sqrt_value (MlArena *arena, MlValue a)
{
	return ml_value_pow (arena, a, half_value (arena));
}

static MlValue abs_value (MlArena *arena, MlValue a)
{
	MlValue r = a;

	if (a.kind == ML_VALUE_EXACT)
	{
		r = ml_value_exact (ml_rat_abs (arena, a.exact));
	}

	else if (a.kind == ML_VALUE_APPROX)
	{
		r = ml_value_approx (fabs (a.approx));
	}

	return r;
}

/* n! for an exact non negative integer. */
static MlValue factorial (MlArena *arena, MlValue a)
{
	MlI64Result n = ml_value_to_i64 (a);
	MlValue     r = a;

	if (a.kind == ML_VALUE_ERROR)
	{
		r = a;
	}

	else if (!n.ok)
	{
		r = error_at (ML_EVAL_NOT_INTEGER);
	}

	else if (n.value < 0)
	{
		r = error_at (ML_EVAL_DOMAIN);
	}

	/* 8200! has about 100000 bits; this keeps the loop short */
	else if (n.value > 20000)
	{
		r = error_at (ML_EVAL_TOO_LARGE);
	}

	else
	{
		MlBig    acc = ml_big_from_u64 (arena, 1);
		uint64_t i   = 2;

		/* four factors (each below 2^15) fit one step of 60 bits */
		while (i <= (uint64_t)n.value && ml_big_ok (acc))
		{
			uint64_t chunk = 1;

			for (uint64_t k = 0; k < 4 && i <= (uint64_t)n.value; k++)
			{
				chunk *= i;
				i++;
			}

			acc = ml_big_mul (arena, acc, ml_big_from_u64 (arena, chunk));
		}

		r = ml_value_exact (ml_rat_from_big (arena, acc));
	}

	return r;
}

typedef enum MlRoundMode
{
	ROUND_FLOOR,
	ROUND_CEIL,
	ROUND_NEAREST
} MlRoundMode;

/* floor, ceil or round (halves away from zero) of an exact number. */
static MlValue round_exact (MlArena *arena, MlRat x, MlRoundMode mode)
{
	MlBig    one  = ml_big_from_u64 (arena, 1);
	MlBigDiv d    = ml_big_divmod (arena, x.num, x.den);
	MlBig    q    = d.quot;
	int32_t  frac = !ml_big_is_zero (d.rem);

	if (mode == ROUND_FLOOR && x.num.negative && frac)
	{
		q = ml_big_sub (arena, q, one);
	}

	else if (mode == ROUND_CEIL && !x.num.negative && frac)
	{
		q = ml_big_add (arena, q, one);
	}

	else if (mode == ROUND_NEAREST)
	{
		MlRat t = ml_rat_add (
			arena,
			ml_rat_abs (arena, x),
			half_value (arena).exact
		);

		q = ml_big_divmod (arena, t.num, t.den).quot;

		if (x.num.negative)
		{
			q = ml_big_neg (arena, q);
		}
	}

	return ml_value_exact (ml_rat_from_big (arena, q));
}

static MlValue round_value (MlArena *arena, MlValue a, MlRoundMode mode)
{
	MlValue r = a;

	if (a.kind == ML_VALUE_EXACT)
	{
		r = round_exact (arena, a.exact, mode);
	}

	else if (a.kind == ML_VALUE_APPROX)
	{
		r = ml_value_approx (
			ml_choose (
				mode == ROUND_FLOOR,
				floor (a.approx),
				ml_choose (
					mode == ROUND_CEIL,
					ceil (a.approx),
					round (a.approx)
				)
			)
		);
	}

	return r;
}

static double real_apply (MlBuiltin id, double x)
{
	double y = 0.0;

	if (id == BUILTIN_SIN)
	{
		y = sin (x);
	}

	else if (id == BUILTIN_COS)
	{
		y = cos (x);
	}

	else if (id == BUILTIN_TAN)
	{
		y = tan (x);
	}

	else if (id == BUILTIN_ASIN)
	{
		y = asin (x);
	}

	else if (id == BUILTIN_ACOS)
	{
		y = acos (x);
	}

	else if (id == BUILTIN_ATAN)
	{
		y = atan (x);
	}

	else if (id == BUILTIN_LN)
	{
		y = log (x);
	}

	else if (id == BUILTIN_LOG)
	{
		y = log10 (x);
	}

	else
	{
		y = exp (x);
	}

	return y;
}

static int32_t is_exactly (MlArena *arena, MlValue a, int64_t n)
{
	return (
		   a.kind == ML_VALUE_EXACT
		&& ml_value_cmp (arena, a, ml_value_from_i64 (arena, n)) == 0
	);
}

/* sin cos tan asin acos atan ln log exp: exact at 0 and 1 where it is known. */
static MlValue real_function (MlArena *arena, MlBuiltin id, MlValue a)
{
	MlSpecial at = builtins [id].at;
	double    x  = ml_value_to_double (arena, a);
	MlValue   r;

	if (a.kind == ML_VALUE_ERROR)
	{
		r = a;
	}

	else if (is_exactly (arena, a, 0) && at == F0_IS_0)
	{
		r = ml_value_from_i64 (arena, 0);
	}

	else if (is_exactly (arena, a, 0) && at == F0_IS_1)
	{
		r = ml_value_from_i64 (arena, 1);
	}

	else if (is_exactly (arena, a, 1) && at == F1_IS_0)
	{
		r = ml_value_from_i64 (arena, 0);
	}

	else if ((id == BUILTIN_LN || id == BUILTIN_LOG) && x <= 0.0)
	{
		r = error_at (ML_EVAL_DOMAIN);
	}

	else if ((id == BUILTIN_ASIN || id == BUILTIN_ACOS) && fabs (x) > 1.0)
	{
		r = error_at (ML_EVAL_DOMAIN);
	}

	else
	{
		r = ml_value_approx (real_apply (id, x));
	}

	return r;
}

static MlValue min_max (
	MlArena       *arena,
	MlBuiltin     id,
	const MlValue *args,
	uint64_t      argc
)
{
	MlValue r = args [0];

	for (uint64_t i = 1; i < argc; i++)
	{
		int32_t order = ml_value_cmp (arena, args [i], r);

		if (
			ml_choose (
				id == BUILTIN_MAX,
				order > 0,
				order < 0
			)
		)
		{
			r = args [i];
		}
	}

	return r;
}

static MlValue gcd_lcm (MlArena *arena, MlBuiltin id, MlValue a, MlValue b)
{
	int32_t whole = (
		   a.kind == ML_VALUE_EXACT
		&& b.kind == ML_VALUE_EXACT
		&& ml_rat_is_integer (a.exact)
		&& ml_rat_is_integer (b.exact)
	);
	MlValue r;

	if (!whole)
	{
		r = error_at (ML_EVAL_NOT_INTEGER);
	}

	else
	{
		MlBig g = ml_big_gcd (arena, a.exact.num, b.exact.num);

		if (id == BUILTIN_GCD || ml_big_is_zero (g))
		{
			r = ml_value_exact (ml_rat_from_big (arena, g));
		}

		else
		{
			MlBig product = ml_big_abs (
				arena,
				ml_big_mul (arena, a.exact.num, b.exact.num)
			);

			r = ml_value_exact (
				ml_rat_from_big (arena, ml_big_divmod (arena, product, g).quot)
			);
		}
	}

	return r;
}

/* A number result of a matrix function, or its failure as an error. */
static MlValue from_rat_result (MlRatResult r)
{
	return ml_choose (
		r.status == ML_OK,
		ml_value_exact (r.value),
		ml_value_matrix (ml_mat_error (r.status))
	);
}

/* The length of a vector: exact when it is a rational, else approximate. */
static MlValue norm_value (MlArena *arena, MlMat v)
{
	MlVecNorm n = ml_vec_norm (arena, v);

	return ml_choose (
		n.status != ML_OK,
		ml_value_matrix (ml_mat_error (n.status)),
		ml_choose (
			n.exact,
			ml_value_exact (n.value),
			ml_value_approx (sqrt (ml_rat_to_double (arena, n.value)))
		)
	);
}

/* identity (n) for a whole number n. */
static MlValue identity_value (MlArena *arena, MlValue n)
{
	MlI64Result size = ml_value_to_i64 (n);

	return ml_choose (
		size.ok && size.value >= 1 && size.value <= ML_MAT_MAX_DIM,
		ml_value_matrix (ml_mat_identity (arena, (uint64_t)size.value)),
		ml_choose (
			size.ok,
			ml_value_error (ML_EVAL_DOMAIN, 0, 0),
			ml_value_error (ML_EVAL_NOT_INTEGER, 0, 0)
		)
	);
}

/* det, inv, transpose, trace, rank, rref, solve, identity, dot, cross, norm */
static MlValue matrix_function (
	MlArena       *arena,
	MlBuiltin     id,
	const MlValue *args,
	uint64_t      argc
)
{
	MlValue r;
	int32_t shaped = 1;

	for (uint64_t i = 0; i < argc; i++)
	{
		/* identity takes a number, everything else matrices */
		shaped = shaped && (
			ml_choose (
				id == BUILTIN_IDENTITY,
				ml_value_is_exact (args [i]),
				ml_value_is_matrix (args [i])
			)
		);
	}

	if (!shaped)
	{
		r = error_at (ML_EVAL_TYPE);
	}

	else if (id == BUILTIN_IDENTITY)
	{
		r = identity_value (arena, args [0]);
	}

	else if (id == BUILTIN_DET)
	{
		r = from_rat_result (ml_mat_det (arena, args [0].matrix));
	}

	else if (id == BUILTIN_INV)
	{
		r = ml_value_matrix (ml_mat_inverse (arena, args [0].matrix));
	}

	else if (id == BUILTIN_TRANSPOSE)
	{
		r = ml_value_matrix (ml_mat_transpose (arena, args [0].matrix));
	}

	else if (id == BUILTIN_TRACE)
	{
		r = from_rat_result (ml_mat_trace (arena, args [0].matrix));
	}

	else if (id == BUILTIN_RANK)
	{
		r = ml_value_from_i64 (
			arena,
			(int64_t)ml_mat_rref (arena, args [0].matrix).rank
		);
	}

	else if (id == BUILTIN_RREF)
	{
		r = ml_value_matrix (ml_mat_rref (arena, args [0].matrix).matrix);
	}

	else if (id == BUILTIN_SOLVE)
	{
		r = ml_value_matrix (
			ml_mat_solve (arena, args [0].matrix, args [1].matrix)
		);
	}

	else if (id == BUILTIN_DOT)
	{
		r = from_rat_result (
			ml_vec_dot (arena, args [0].matrix, args [1].matrix)
		);
	}

	else if (id == BUILTIN_CROSS)
	{
		r = ml_value_matrix (
			ml_vec_cross (arena, args [0].matrix, args [1].matrix)
		);
	}

	else
	{
		r = norm_value (arena, args [0].matrix);
	}

	return r;
}

static MlValue builtin_apply (
	MlArena       *arena,
	MlBuiltin     id,
	const MlValue *args,
	uint64_t      argc
)
{
	MlValue r;
	int32_t has_matrix = 0;

	for (uint64_t i = 0; i < argc; i++)
	{
		has_matrix = has_matrix || ml_value_is_matrix (args [i]);
	}

	if (id >= BUILTIN_DET)
	{
		r = matrix_function (arena, id, args, argc);
	}

	else if (has_matrix)
	{
		/* the number functions do not take matrices */
		r = error_at (ML_EVAL_TYPE);
	}

	else if (id == BUILTIN_SQRT)
	{
		r = sqrt_value (arena, args [0]);
	}

	else if (id == BUILTIN_ABS)
	{
		r = abs_value (arena, args [0]);
	}

	else if (id >= BUILTIN_SIN && id <= BUILTIN_EXP)
	{
		r = real_function (arena, id, args [0]);
	}

	else if (id == BUILTIN_FLOOR)
	{
		r = round_value (arena, args [0], ROUND_FLOOR);
	}

	else if (id == BUILTIN_CEIL)
	{
		r = round_value (arena, args [0], ROUND_CEIL);
	}

	else if (id == BUILTIN_ROUND)
	{
		r = round_value (arena, args [0], ROUND_NEAREST);
	}

	else if (id == BUILTIN_MIN || id == BUILTIN_MAX)
	{
		r = min_max (arena, id, args, argc);
	}

	else
	{
		r = gcd_lcm (arena, id, args [0], args [1]);
	}

	return r;
}

static MlValue binary_apply (
	MlArena    *arena,
	MlNodeKind kind,
	MlValue    a,
	MlValue    b
)
{
	MlValue r;

	if (kind == ML_NODE_ADD)
	{
		r = ml_value_add (arena, a, b);
	}

	else if (kind == ML_NODE_SUB)
	{
		r = ml_value_sub (arena, a, b);
	}

	else if (kind == ML_NODE_MUL)
	{
		r = ml_value_mul (arena, a, b);
	}

	else if (kind == ML_NODE_DIV)
	{
		r = ml_value_div (arena, a, b);
	}

	else
	{
		r = ml_value_pow (arena, a, b);
	}

	return r;
}

/* ---- names -------------------------------------------------------------- */

typedef struct MlParamHit
{
	int32_t  found;
	uint64_t index;
} MlParamHit;

static MlParamHit param_find (const MlScope *sc, MlSlice name)
{
	MlParamHit hit =
	{
		.found = 0,
		.index = 0
	};
	uint64_t   n   = ml_binding_param_count (sc->func);

	for (uint64_t i = 0; i < n && !hit.found; i++)
	{
		if (ml_slice_equal (ml_binding_param (sc->func, i), name))
		{
			hit.found = 1;
			hit.index = i;
		}
	}

	return hit;
}

/* The value a name stands for: parameter, variable or constant. */
static MlValue name_value (MlEvalCtx *cx, const MlScope *sc, MlSlice name)
{
	MlParamHit hit = param_find (sc, name);
	MlValue    v;

	if (hit.found)
	{
		v = sc->args [hit.index];
	}

	else
	{
		MlBinding b = ml_env_lookup (cx->env, name, cx->snapshot);

		if (b.kind == ML_BIND_VAR)
		{
			v = b.value;
		}

		else if (b.kind == ML_BIND_FUNC)
		{
			v = error_at (ML_EVAL_NOT_VALUE);
		}

		else
		{
			v = constant_find (name);

			if (v.kind == ML_VALUE_NONE)
			{
				v = error_at (ML_EVAL_UNDEFINED_NAME);
			}
		}
	}

	return v;
}

/* ---- calls -------------------------------------------------------------- */

static MlCallKind call_kind (MlEvalCtx *cx, const MlScope *sc, MlSlice name)
{
	MlCallKind kind = CALL_UNKNOWN;
	MlBinding  b    = ml_env_lookup (cx->env, name, cx->snapshot);

	if (param_find (sc, name).found || b.kind == ML_BIND_VAR)
	{
		kind = CALL_PRODUCT;
	}

	else if (b.kind == ML_BIND_FUNC)
	{
		kind = CALL_USER;
	}

	else if (builtin_find (name) != BUILTIN_NONE)
	{
		kind = CALL_BUILTIN;
	}

	else if (constant_find (name).kind != ML_VALUE_NONE)
	{
		kind = CALL_PRODUCT;
	}

	return kind;
}

/* ---- leaves ------------------------------------------------------------- */

static MlValue eval_superscript (MlEvalCtx *cx, MlSlice text)
{
	MlStr   digits;
	MlValue r;

	ml_str_init (&digits);

	if (ml_superscript_to_ascii (&digits, text) == ML_OK)
	{
		r = ml_value_exact (
			ml_rat_parse_decimal (cx->arena, ml_str_view (digits))
		);
	}

	else
	{
		r = error_at (ML_EVAL_SYNTAX);
	}

	ml_str_free (&digits);
	return r;
}

static MlValue leaf_value (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      idx,
	MlNode        node
)
{
	MlSlice text = ml_ast_text (&sc->ast, sc->source, idx);
	MlValue v;

	if (node.kind == ML_NODE_NUMBER)
	{
		v = ml_value_exact (ml_rat_parse_decimal (cx->arena, text));
	}

	else if (node.kind == ML_NODE_SUPERSCRIPT)
	{
		v = eval_superscript (cx, text);
	}

	else if (node.kind == ML_NODE_NAME)
	{
		v = name_value (cx, sc, text);
	}

	else
	{
		v = error_at (ML_EVAL_SYNTAX);
	}

	return v;
}

/* ---- the machine -------------------------------------------------------- */

static void push_value (MlEvalCtx *cx, MlValue v)
{
	if (MlValueVec_push (&cx->vals, v) != ML_OK)
	{
		cx->failed = 1;
	}
}

/*
 * Starts evaluating a node. Past the depth limit no frame is made; the child
 * "evaluates" to the depth error, which then ends its parent like any error.
 */
static void push_frame (MlEvalCtx *cx, uint64_t node, uint64_t scope)
{
	MlFrame f =
	{
		.node  = node,
		.scope = scope,
		.step  = 0,
		.kind  = CALL_NEW
	};

	if (cx->frames.len >= ML_EVAL_MAX_DEPTH)
	{
		push_value (cx, error_at (ML_EVAL_DEPTH));
	}

	else if (MlFrameVec_push (&cx->frames, f) != ML_OK)
	{
		cx->failed = 1;
	}
}

static MlFrame top_frame (const MlEvalCtx *cx)
{
	return cx->frames.data [cx->frames.len - 1];
}

static MlValue top_value (const MlEvalCtx *cx)
{
	return MlValueVec_get (&cx->vals, cx->vals.len - 1);
}

static int32_t top_is_error (const MlEvalCtx *cx)
{
	return cx->vals.len > 0 && top_value (cx).kind == ML_VALUE_ERROR;
}

/* The top frame is done: its operands on vals are replaced by its result. */
static void finish (MlEvalCtx *cx, uint64_t operands, MlValue v, MlNode node)
{
	cx->vals.len -= operands;
	push_value (cx, ml_value_locate (v, node.start, node.len));
	cx->frames.len--;
}

static void set_step (MlEvalCtx *cx, uint64_t step)
{
	cx->frames.data [cx->frames.len - 1].step = step;
}

/* ---- operators ---- */

static int32_t is_operator (MlNodeKind kind)
{
	return (
		   kind == ML_NODE_LIST
		|| kind == ML_NODE_NEG
		|| kind == ML_NODE_FACT
		|| kind == ML_NODE_SQRT
		|| (kind >= ML_NODE_ADD && kind <= ML_NODE_POW)
	);
}

/*
 * A list of numbers is a vector, a column. A list of vectors of one length is
 * a matrix whose rows they are, so [[1, 2], [3, 4]] is the matrix with the
 * rows (1 2) and (3 4).
 */
static MlValue list_value (MlArena *arena, const MlValue *items, uint64_t n)
{
	int32_t numbers = 1;
	int32_t columns = 1;
	int32_t approx  = 0;
	MlMat   m       = ml_mat_error (ML_ERR_INVALID);
	MlValue r;

	for (uint64_t i = 0; i < n; i++)
	{
		numbers = numbers && ml_value_is_exact (items [i]);
		approx  = approx || items [i].kind == ML_VALUE_APPROX;
		columns = columns && (
			   ml_value_is_matrix (items [i])
			&& items [i].matrix.cols == 1
			&& items [i].matrix.rows == items [0].matrix.rows
		);
	}

	if (numbers)
	{
		m = ml_mat_zero (arena, n, 1);

		for (uint64_t i = 0; i < n && m.status == ML_OK; i++)
		{
			ml_mat_set (&m, i, 0, items [i].exact);
		}
	}

	else if (columns)
	{
		m = ml_mat_zero (arena, n, items [0].matrix.rows);

		for (uint64_t i = 0; i < n && m.status == ML_OK; i++)
		{
			for (uint64_t j = 0; j < m.cols; j++)
			{
				ml_mat_set (&m, i, j, items [i].matrix.cells [j]);
			}
		}
	}

	r = ml_value_matrix (m);

	/* approximations are named, other mixtures are a matter of sizes */
	return ml_choose (
		approx,
		error_at (ML_EVAL_NOT_EXACT),
		ml_choose (
			r.kind == ML_VALUE_ERROR && r.error == ML_EVAL_SIZE,
			error_at (ML_EVAL_TYPE),
			r
		)
	);
}

/* The result of an operator whose n operands are on top of vals. */
static MlValue operator_apply (MlEvalCtx *cx, MlNodeKind kind, uint64_t n)
{
	MlValue a = MlValueVec_get (&cx->vals, cx->vals.len - n);
	MlValue r;

	if (kind == ML_NODE_LIST)
	{
		r = list_value (cx->arena, &cx->vals.data [cx->vals.len - n], n);
	}

	else if (kind == ML_NODE_NEG)
	{
		r = ml_value_neg (cx->arena, a);
	}

	else if (
		   ml_value_is_matrix (a)
		&& (kind == ML_NODE_FACT || kind == ML_NODE_SQRT)
	)
	{
		r = error_at (ML_EVAL_TYPE);
	}

	else if (kind == ML_NODE_FACT)
	{
		r = factorial (cx->arena, a);
	}

	else if (kind == ML_NODE_SQRT)
	{
		r = sqrt_value (cx->arena, a);
	}

	else
	{
		r = binary_apply (cx->arena, kind, a, top_value (cx));
	}

	return r;
}

/*
 * Is the right operand of the power at f the bare name T, with a matrix as the
 * base (on top of vals)? A^T is the transpose, and T is not looked up.
 */
static int32_t is_transpose (const MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc    = &cx->scopes.data [f.scope];
	uint64_t      right = ml_ast_child (&sc->ast, f.node, 1);
	int32_t       is_t  = (
		   node.kind == ML_NODE_POW
		&& f.step == 1
		&& ml_ast_node (&sc->ast, right).kind == ML_NODE_NAME
		&& ml_slice_equal (
			ml_ast_text (&sc->ast, sc->source, right),
			ml_slice_cstr ("T")
		)
	);

	return is_t && ml_value_is_matrix (top_value (cx));
}

static void step_operator (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc   = &cx->scopes.data [f.scope];
	uint64_t      want = ml_choose (
		node.kind == ML_NODE_LIST,
		ml_ast_child_count (&sc->ast, f.node),
		ml_choose (
			node.kind >= ML_NODE_ADD && node.kind <= ML_NODE_POW,
			2,
			1
		)
	);

	if (f.step > 0 && top_is_error (cx))
	{
		finish (cx, f.step, top_value (cx), node);
	}

	else if (is_transpose (cx, f, node))
	{
		finish (
			cx,
			1,
			ml_value_matrix (
				ml_mat_transpose (cx->arena, top_value (cx).matrix)
			),
			node
		);
	}

	else if (f.step == want)
	{
		finish (cx, want, operator_apply (cx, node.kind, want), node);
	}

	else
	{
		set_step (cx, f.step + 1);
		push_frame (cx, ml_ast_child (&sc->ast, f.node, f.step), f.scope);
	}
}

/* ---- calls ---- */

/* Looks at the callee and either ends the call with an error or prepares it. */
static void call_begin (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc     = &cx->scopes.data [f.scope];
	uint64_t      callee = ml_ast_child (&sc->ast, f.node, 0);
	MlNode        cnode  = ml_ast_node (&sc->ast, callee);
	MlSlice       name   = ml_ast_text (&sc->ast, sc->source, callee);
	uint64_t      argc   = ml_ast_child_count (&sc->ast, f.node) - 1;
	MlCallKind    kind   = ml_choose (
		cnode.kind == ML_NODE_NAME,
		call_kind (cx, sc, name),
		CALL_UNKNOWN
	);
	MlValue       r      = ml_value_none ();

	if (kind == CALL_UNKNOWN)
	{
		r = ml_value_error (
			ml_choose (
				cnode.kind == ML_NODE_NAME,
				ML_EVAL_UNDEFINED_NAME,
				ML_EVAL_NOT_FUNCTION
			),
			cnode.start,
			cnode.len
		);
	}

	else if (kind == CALL_USER)
	{
		MlBinding b = ml_env_lookup (cx->env, name, cx->snapshot);

		if (argc != ml_binding_param_count (b))
		{
			r = error_at (ML_EVAL_ARITY);
		}
	}

	else if (kind == CALL_BUILTIN)
	{
		MlBuiltin id = builtin_find (name);

		if (argc < builtins [id].min || argc > builtins [id].max)
		{
			r = error_at (ML_EVAL_ARITY);
		}
	}

	else if (argc != 1)
	{
		r = error_at (ML_EVAL_NOT_FUNCTION);
	}

	else
	{
		/* the number to multiply, as the first operand of the product */
		r = ml_value_locate (
			name_value (cx, sc, name),
			cnode.start,
			cnode.len
		);
	}

	if (r.kind == ML_VALUE_ERROR)
	{
		finish (cx, 0, r, node);
	}

	else
	{
		cx->frames.data [cx->frames.len - 1].kind = kind;

		if (r.kind != ML_VALUE_NONE)
		{
			push_value (cx, r);
			set_step (cx, 1);
		}
	}
}

/* All arguments are on vals: run the body of the function in a new scope. */
static void call_invoke (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc    = &cx->scopes.data [f.scope];
	uint64_t      argc  = ml_ast_child_count (&sc->ast, f.node) - 1;
	MlBinding     b     = ml_env_lookup (
		cx->env,
		ml_ast_text (&sc->ast, sc->source, ml_ast_child (&sc->ast, f.node, 0)),
		cx->snapshot
	);
	MlValue       *args = NULL;
	MlScope       rec;

	if (argc > 0)
	{
		args = ML_ARENA_NEW_ARRAY (cx->arena, MlValue, argc);
	}

	if (argc > 0 && args == NULL)
	{
		finish (cx, argc, error_at (ML_EVAL_NO_MEMORY), node);
	}

	else
	{
		for (uint64_t i = 0; i < argc; i++)
		{
			args [i] = cx->vals.data [cx->vals.len - argc + i];
		}

		cx->vals.len -= argc;
		rec.source    = b.source;
		rec.ast       = b.ast;
		rec.func      = b;
		rec.args      = args;

		if (MlScopeVec_push (&cx->scopes, rec) != ML_OK)
		{
			cx->failed = 1;
		}

		else
		{
			set_step (cx, argc + 1);
			push_frame (cx, b.body, cx->scopes.len - 1);
		}
	}
}

/* The body is done: its result is on vals. */
static void call_return (MlEvalCtx *cx, MlNode node)
{
	MlValue r = top_value (cx);

	cx->vals.len--;
	cx->scopes.len--;

	/* the body is text of another line: point at the call instead */
	if (r.kind == ML_VALUE_ERROR)
	{
		r = ml_value_error (r.error, node.start, node.len);
	}

	finish (cx, 0, r, node);
}

static void step_call (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc   = &cx->scopes.data [f.scope];
	uint64_t      argc = ml_ast_child_count (&sc->ast, f.node) - 1;

	if (f.kind == CALL_NEW)
	{
		call_begin (cx, f, node);
	}

	else if (f.kind == CALL_USER && f.step == argc + 1)
	{
		call_return (cx, node);
	}

	else if (f.step > 0 && top_is_error (cx))
	{
		finish (cx, f.step, top_value (cx), node);
	}

	else if (f.kind == CALL_USER && f.step == argc)
	{
		call_invoke (cx, f, node);
	}

	else if (f.kind == CALL_BUILTIN && f.step == argc)
	{
		MlSlice name = ml_ast_text (
			&sc->ast,
			sc->source,
			ml_ast_child (&sc->ast, f.node, 0)
		);

		finish (
			cx,
			argc,
			builtin_apply (
				cx->arena,
				builtin_find (name),
				&cx->vals.data [cx->vals.len - argc],
				argc
			),
			node
		);
	}

	else if (f.kind == CALL_PRODUCT && f.step == 2)
	{
		finish (
			cx,
			2,
			ml_value_mul (
				cx->arena,
				MlValueVec_get (&cx->vals, cx->vals.len - 2),
				top_value (cx)
			),
			node
		);
	}

	else
	{
		/* arguments are the children from 1; a product has one, at step 1 */
		uint64_t child = ml_choose (
			f.kind == CALL_PRODUCT,
			f.step,
			f.step + 1
		);

		set_step (cx, f.step + 1);
		push_frame (cx, ml_ast_child (&sc->ast, f.node, child), f.scope);
	}
}

static void eval_step (MlEvalCtx *cx)
{
	MlFrame       f    = top_frame (cx);
	const MlScope *sc  = &cx->scopes.data [f.scope];
	MlNode        node = ml_ast_node (&sc->ast, f.node);

	if (node.kind == ML_NODE_CALL)
	{
		step_call (cx, f, node);
	}

	else if (is_operator (node.kind))
	{
		step_operator (cx, f, node);
	}

	else
	{
		finish (cx, 0, leaf_value (cx, sc, f.node, node), node);
	}
}

/* ---- statements --------------------------------------------------------- */

MlValue ml_eval (
	MlArena     *arena,
	const MlEnv *env,
	uint64_t    snapshot,
	MlSlice     source,
	const MlAst *ast
)
{
	MlEvalCtx cx =
	{
		.arena    = arena,
		.env      = env,
		.snapshot = snapshot,
		.failed   = 0
	};
	MlNode    root = ml_ast_node (ast, ast->root);
	MlValue   r    = ml_value_none ();

	if (root.kind != ML_NODE_NIL && root.kind != ML_NODE_FUNC_DEF)
	{
		MlScope line =
		{
			.source = source,
			.ast    = *ast,
			.args   = NULL
		};

		memset (&line.func, 0, sizeof line.func);
		cx.failed = MlScopeVec_push (&cx.scopes, line) != ML_OK;

		if (!cx.failed)
		{
			push_frame (
				&cx,
				ml_choose (
					root.kind == ML_NODE_ASSIGN,
					ml_ast_child (ast, ast->root, 1),
					ast->root
				),
				0
			);
		}

		while (cx.frames.len > 0 && !cx.failed)
		{
			eval_step (&cx);
		}

		r = ml_choose (
			cx.failed,
			error_at (ML_EVAL_NO_MEMORY),
			MlValueVec_get (&cx.vals, 0)
		);
	}

	MlFrameVec_free (&cx.frames);
	MlValueVec_free (&cx.vals);
	MlScopeVec_free (&cx.scopes);
	return r;
}

MlResult ml_eval_commit (
	MlEnv       *env,
	MlSlice     source,
	const MlAst *ast,
	MlValue     value
)
{
	MlResult r    = ML_OK;
	MlNode   root = ml_ast_node (ast, ast->root);

	if (
		   root.kind == ML_NODE_ASSIGN
		&& (ml_value_is_number (value) || ml_value_is_matrix (value))
	)
	{
		r = ml_env_define_var (
			env,
			ml_ast_text (ast, source, ml_ast_child (ast, ast->root, 0)),
			value
		);
	}

	else if (root.kind == ML_NODE_FUNC_DEF)
	{
		r = ml_env_define_func (env, source, ast);
	}

	return r;
}
