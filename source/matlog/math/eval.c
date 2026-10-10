#include <matlog/math/eval.h>

#include <matlog/core/heap.h>
#include <matlog/core/names.h>
#include <matlog/math/hyper.h>
#include <matlog/math/listfn.h>
#include <matlog/math/pga.h>
#include <matlog/math/stats.h>
#include <matlog/math/vector.h>
#include <matlog/parse/words.h>
#include <matlog/sym/expr.h>

#include <math.h>

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
	CALL_BUILTIN,
	CALL_IF,      /* if (c, a, b): only the branch that is taken runs */
	/* map filter fold and sum (i, a, b, e): the function is not a value */
	CALL_HOF,
	/* simplify expand subs diff integrate: the arguments are formulas */
	CALL_SYMBOLIC
} MlCallKind;

typedef struct MlFrame
{
	uint64_t   node;  /* index in the tree of the frame's scope */
	uint64_t   scope; /* index in scopes */
	uint64_t   step;  /* children (or for a user call, phases) done */
	MlCallKind kind;  /* calls only */
	uint64_t   mark;  /* locals there were at the start of a seq or a lambda */
	uint64_t   index; /* map, filter, fold, sum: the item being done */
	uint64_t   vstart; /* those: vals when the call began */
} MlFrame;

/* The text a node belongs to: the line, or the body of a running function. */
typedef struct MlScope
{
	MlSlice       source;
	MlAst         ast;   /* a view; the line or the binding owns the nodes */
	MlBinding     func;  /* the running function, ML_BIND_NONE on the line */
	const MlValue *args; /* its arguments, one per parameter */
} MlScope;

/* A name as written in the tree: its characters and its interned number. */
typedef struct MlRef
{
	MlSlice text;
	MlName  id;
} MlRef;

static MlRef ref_of (const MlScope *sc, uint64_t node)
{
	MlRef r = {
		.text = ml_ast_text (&sc->ast, sc->source, node),
		.id   = ml_ast_name (&sc->ast, node)
	};

	return r;
}

/* 1 when the name is the word (ML_WORD_ID_<w>). */
#define REF_IS(ref, w) (ml_word_of ((ref).id).id == ML_WORD_ID_##w)

/* A local name of a let: visible in the statements after it, in its scope. */
typedef struct MlLocal
{
	uint64_t scope;
	MlSlice  name;
	MlName   id;
	MlValue  value;
} MlLocal;

ML_VEC_DEFINE (MlFrameVec, MlFrame)
ML_VEC_DEFINE (MlValueVec, MlValue)
ML_VEC_DEFINE (MlScopeVec, MlScope)
ML_VEC_DEFINE (MlLocalVec, MlLocal)

typedef struct MlEvalCtx
{
	MlArena     *arena;
	const MlEnv *env;
	uint64_t    snapshot;
	MlFrameVec  frames;
	MlValueVec  vals;
	MlScopeVec  scopes;
	MlLocalVec  locals;
	int32_t     failed; /* a stack could not grow: out of memory */
	uint64_t    steps;  /* nodes evaluated so far, see ML_EVAL_MAX_STEPS */
	MlStepLog   *log;    /* steps of symbolic calls, NULL: none wanted */
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
	BUILTIN_DIM,
	BUILTIN_IF,
	BUILTIN_NOT,
	BUILTIN_APPROX,
	BUILTIN_STEPS,
	BUILTIN_MAP,
	BUILTIN_FILTER,
	BUILTIN_FOLD,
	/* projective geometric algebra (math/pga.h) */
	BUILTIN_MV_FIRST,
	BUILTIN_MV_DUAL = BUILTIN_MV_FIRST,
	BUILTIN_MV_UNDUAL,
	BUILTIN_MV_UNIT,
	BUILTIN_MV_SCALAR,
	BUILTIN_MV_GRADE,
	BUILTIN_MV_INNER,
	BUILTIN_MV_JOIN,
	BUILTIN_MV_MEET,
	BUILTIN_MV_POINT,
	BUILTIN_MV_LINE,
	BUILTIN_MV_PLANE,
	BUILTIN_MV_TRANSLATOR,
	BUILTIN_MV_ROTOR,
	BUILTIN_MV_LAST = BUILTIN_MV_ROTOR,
	/* complex numbers and quaternions (math/hyper.h) */
	BUILTIN_RE,
	BUILTIN_IM,
	BUILTIN_CONJ,
	BUILTIN_ARG,
	BUILTIN_QUAT,
	BUILTIN_POLAR,
	BUILTIN_ROTATE,
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
	/* statistics (math/stats.h) from here on */
#define BUILTIN_STAT_ENUM(id, name, lo, hi) BUILTIN_STAT_##id,
	ML_STAT_LIST (BUILTIN_STAT_ENUM)
#undef BUILTIN_STAT_ENUM
	/* lists (math/listfn.h) from here on */
#define BUILTIN_LISTFN_ENUM(id, name, lo, hi) BUILTIN_LISTFN_##id,
	ML_LISTFN_LIST (BUILTIN_LISTFN_ENUM)
#undef BUILTIN_LISTFN_ENUM
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

static const MlBuiltinInfo builtins [BUILTIN_COUNT] = {
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
	[BUILTIN_MIN]   = { .name = "min",   .min = 1, .max = ANY_ARGS },
	[BUILTIN_MAX]   = { .name = "max",   .min = 1, .max = ANY_ARGS },
	[BUILTIN_GCD]   = { .name = "gcd",   .min = 2, .max = 2 },
	[BUILTIN_LCM]   = { .name = "lcm",   .min = 2, .max = 2 },
	[BUILTIN_DIM]   = { .name = "dim",   .min = 1, .max = 1 },
	[BUILTIN_IF]    = { .name = "if",    .min = 3, .max = 3 },
	[BUILTIN_NOT]   = { .name = "not",   .min = 1, .max = 1 },
	[BUILTIN_APPROX] = { .name = "approx", .min = 1, .max = 1 },
	[BUILTIN_STEPS]  = { .name = "steps",  .min = 1, .max = 1 },
	[BUILTIN_MAP]    = { .name = "map",    .min = 2, .max = 2 },
	[BUILTIN_FILTER] = { .name = "filter", .min = 2, .max = 2 },
	[BUILTIN_FOLD]   = { .name = "fold",   .min = 3, .max = 3 },
	[BUILTIN_RE]     = { .name = "re",     .min = 1, .max = 1 },
	[BUILTIN_IM]     = { .name = "im",     .min = 1, .max = 1 },
	[BUILTIN_MV_DUAL]       = { .name = "dual",       .min = 1, .max = 1 },
	[BUILTIN_MV_UNDUAL]     = { .name = "undual",     .min = 1, .max = 1 },
	[BUILTIN_MV_UNIT]       = { .name = "unit",       .min = 1, .max = 1 },
	[BUILTIN_MV_SCALAR]     = { .name = "scalar",     .min = 1, .max = 1 },
	[BUILTIN_MV_GRADE]      = { .name = "grade",      .min = 2, .max = 2 },
	[BUILTIN_MV_INNER]      = { .name = "inner",      .min = 2, .max = 2 },
	[BUILTIN_MV_JOIN]       = { .name = "join",       .min = 2, .max = 2 },
	[BUILTIN_MV_MEET]       = { .name = "meet",       .min = 2, .max = 2 },
	[BUILTIN_MV_POINT]      = { .name = "point",      .min = 2, .max = 3 },
	[BUILTIN_MV_LINE]       = { .name = "line",       .min = 3, .max = 3 },
	[BUILTIN_MV_PLANE]      = { .name = "plane",      .min = 4, .max = 4 },
	[BUILTIN_MV_TRANSLATOR] = { .name = "translator", .min = 2, .max = 3 },
	[BUILTIN_MV_ROTOR]      = { .name = "rotor",      .min = 1, .max = 4 },
	[BUILTIN_CONJ]   = { .name = "conj",   .min = 1, .max = 1 },
	[BUILTIN_ARG]    = { .name = "arg",    .min = 1, .max = 1 },
	[BUILTIN_QUAT]   = { .name = "quat",   .min = 4, .max = 4 },
	[BUILTIN_POLAR]  = { .name = "polar",  .min = 2, .max = 2 },
	[BUILTIN_ROTATE] = { .name = "rotate", .min = 2, .max = 2 },
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
	[BUILTIN_NORM]      = { .name = "norm",      .min = 1, .max = 1 },
#define BUILTIN_STAT_INFO(id, nm, lo, hi) \
	[BUILTIN_STAT_##id] = { .name = nm, .min = lo, .max = hi },
	ML_STAT_LIST (BUILTIN_STAT_INFO)
#undef BUILTIN_STAT_INFO
#define BUILTIN_LISTFN_INFO(id, nm, lo, hi) \
	[BUILTIN_LISTFN_##id] = { .name = nm, .min = lo, .max = hi },
	ML_LISTFN_LIST (BUILTIN_LISTFN_INFO)
#undef BUILTIN_LISTFN_INFO
};

/*
 * The built in function of a name. The names of the table are interned the
 * first time one is asked for, builtin_of_name is indexed by name number, so
 * a look up is one hash of the name and one index. Made again when the names
 * were released.
 */
static MlBuiltin *builtin_of_name;
static uint64_t  builtin_of_name_len;
static uint64_t  builtin_built_for;

void ml_eval_release (void)
{
	ml_heap_free (builtin_of_name);
	builtin_of_name     = NULL;
	builtin_of_name_len = 0;
	builtin_built_for   = 0;
}

static void builtins_ready (void)
{
	if (builtin_built_for != ml_names_generation ())
	{
		MlName top = 0;

		ml_eval_release ();

		for (uint64_t i = 1; i < BUILTIN_COUNT; i++)
		{
			MlName n = ml_name (ml_slice_cstr (builtins [i].name));

			top = ml_choose (n > top, n, top);
		}

		builtin_of_name = (MlBuiltin *)ml_heap_alloc_zero (
			((uint64_t)top + 1) * sizeof (MlBuiltin)
		);

		if (builtin_of_name != NULL)
		{
			builtin_of_name_len = (uint64_t)top + 1;

			for (uint64_t i = 1; i < BUILTIN_COUNT; i++)
			{
				builtin_of_name [ml_name (ml_slice_cstr (builtins [i].name))]
					= (MlBuiltin)i;
			}
		}

		builtin_built_for = ml_names_generation ();
	}
}

static MlBuiltin builtin_of (MlName n)
{
	builtins_ready ();

	return ml_choose (
		n < builtin_of_name_len,
		builtin_of_name [n],
		BUILTIN_NONE
	);
}

uint64_t ml_function_name_count (void)
{
	return (BUILTIN_COUNT - 1) + ml_word_kind_count (ML_WORD_SYMBOLIC);
}

MlSlice ml_function_name_at (uint64_t index)
{
	MlSlice name = ml_slice_cstr ("");

	if (index < BUILTIN_COUNT - 1)
	{
		name = ml_slice_cstr (builtins [index + 1].name);
	}

	else if (index < ml_function_name_count ())
	{
		name = ml_word_kind_at (
			ML_WORD_SYMBOLIC,
			index - (BUILTIN_COUNT - 1)
		);
	}

	return name;
}

/* The value of pi or e, ML_VALUE_NONE for any other name. */
static MlValue constant_of (MlName name)
{
	MlValue v = ml_value_none ();

	switch (ml_word_of (name).id)
	{
		case ML_WORD_ID_TRUE:
		{
			v = ml_value_bool (1);
		} break;

		case ML_WORD_ID_FALSE:
		{
			v = ml_value_bool (0);
		} break;

		case ML_WORD_ID_PI:
		{
			v = ml_value_approx (3.14159265358979323846);
		} break;

		case ML_WORD_ID_E:
		{
			v = ml_value_approx (2.71828182845904523536);
		} break;

		default:
		{
		} break;
	}

	return v;
}

/*
 * The imaginary units i, j and k as values, ML_VALUE_NONE for any other name.
 * They are looked up after variables and functions (a variable i hides it)
 * and only where a value is needed: the symbolic functions still read them
 * as symbols, so diff (k x, k) works.
 */
static MlValue hyper_unit_find (MlArena *arena, MlSlice name)
{
	MlValue v    = ml_value_none ();
	MlRat   zero = ml_rat_zero (arena);
	MlRat   one  = ml_rat_from_i64 (arena, 1);

	if (ml_slice_equal (name, ml_slice_cstr ("i")))
	{
		v = ml_hyper_value (arena, ml_hyper_exact (zero, one, zero, zero));
	}

	else if (ml_slice_equal (name, ml_slice_cstr ("j")))
	{
		v = ml_hyper_value (arena, ml_hyper_exact (zero, zero, one, zero));
	}

	else if (ml_slice_equal (name, ml_slice_cstr ("k")))
	{
		v = ml_hyper_value (arena, ml_hyper_exact (zero, zero, zero, one));
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

	switch (id)
	{
		case BUILTIN_SIN:
		{
			y = sin (x);
		} break;

		case BUILTIN_COS:
		{
			y = cos (x);
		} break;

		case BUILTIN_TAN:
		{
			y = tan (x);
		} break;

		case BUILTIN_ASIN:
		{
			y = asin (x);
		} break;

		case BUILTIN_ACOS:
		{
			y = acos (x);
		} break;

		case BUILTIN_ATAN:
		{
			y = atan (x);
		} break;

		case BUILTIN_LN:
		{
			y = log (x);
		} break;

		case BUILTIN_LOG:
		{
			y = log10 (x);
		} break;

		default:
		{
			y = exp (x);
		} break;
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

/* ---- quantities --------------------------------------------------------- */

/* The square root of a quantity: every dimension exponent even, root exact. */
static MlValue quantity_sqrt (MlArena *arena, MlValue a)
{
	MlValue   r    = error_at (ML_EVAL_DIMENSION_MISMATCH);
	MlDim     half = ml_dim_none ();
	int32_t   even = 1;
	MlRatSqrt root;

	for (int32_t i = 0; i < ML_DIM_COUNT; i++)
	{
		even       = even && a.dim.e [i] % 2 == 0;
		half.e [i] = a.dim.e [i] / 2;
	}

	if (even)
	{
		r = error_at (ML_EVAL_NOT_REAL);
	}

	if (
		   even
		&& ml_rat_cmp (arena, a.exact, ml_rat_from_i64 (arena, 0)) >= 0
	)
	{
		root = ml_rat_sqrt_exact (arena, a.exact);
		r    = ml_choose (
			root.exact,
			ml_value_quantity (root.value, half),
			error_at (ML_EVAL_NOT_EXACT)
		);
	}

	return r;
}

/* abs, sqrt, min and max of quantities; nothing else makes sense for them. */
static MlValue quantity_function (
	MlArena       *arena,
	MlBuiltin     id,
	const MlValue *args,
	uint64_t      argc
)
{
	MlValue r = error_at (ML_EVAL_TYPE);

	if (id == BUILTIN_SQRT && args [0].kind == ML_VALUE_QUANTITY)
	{
		r = quantity_sqrt (arena, args [0]);
	}

	else if (id == BUILTIN_ABS)
	{
		r = args [0];

		if (ml_value_cmp (arena, r, ml_value_from_i64 (arena, 0)) < 0)
		{
			r = ml_value_neg (arena, r);
		}
	}

	else if (id == BUILTIN_MIN || id == BUILTIN_MAX)
	{
		int32_t same = 1;

		for (uint64_t i = 0; i < argc; i++)
		{
			MlDim di = ml_choose (
				args [i].kind == ML_VALUE_QUANTITY,
				args [i].dim,
				ml_dim_none ()
			);
			MlDim d0 = ml_choose (
				args [0].kind == ML_VALUE_QUANTITY,
				args [0].dim,
				ml_dim_none ()
			);

			same = same && ml_dim_equal (di, d0);
		}

		r = ml_choose (
			same,
			min_max (arena, id, args, argc),
			error_at (ML_EVAL_DIMENSION_MISMATCH)
		);
	}

	return r;
}

/* a to b: a in the dimension of the unit expression b, or a mismatch. */
static MlValue convert_value (MlValue a, MlValue b)
{
	MlValue r    = error_at (ML_EVAL_TYPE);
	int32_t ok_a = a.kind == ML_VALUE_EXACT || a.kind == ML_VALUE_QUANTITY;
	int32_t ok_b = b.kind == ML_VALUE_EXACT || b.kind == ML_VALUE_QUANTITY;

	if (a.kind == ML_VALUE_APPROX || b.kind == ML_VALUE_APPROX)
	{
		r = error_at (ML_EVAL_NOT_EXACT);
	}

	else if (ok_a && ok_b)
	{
		MlDim da = ml_choose (
			a.kind == ML_VALUE_QUANTITY,
			a.dim,
			ml_dim_none ()
		);
		MlDim db = ml_choose (
			b.kind == ML_VALUE_QUANTITY,
			b.dim,
			ml_dim_none ()
		);

		r = ml_choose (
			ml_dim_equal (da, db),
			a,
			error_at (ML_EVAL_DIMENSION_MISMATCH)
		);
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

/* The elementary functions of a formula; the others need numbers. */
static MlValue formula_function (MlArena *arena, MlBuiltin id, MlValue a)
{
	MlValue  out = error_at (ML_EVAL_TYPE);
	MlSymCtx ctx;

	ml_symctx_init (&ctx, arena);

	if (id == BUILTIN_SQRT)
	{
		out = ml_value_pow (
			arena,
			a,
			ml_value_exact (
				ml_rat_make (
					arena,
					ml_big_from_u64 (arena, 1),
					ml_big_from_u64 (arena, 2)
				)
			)
		);
	}

	else if (
		   (id >= BUILTIN_SIN && id <= BUILTIN_LN)
		|| id == BUILTIN_EXP
		|| id == BUILTIN_ABS
	)
	{
		out = ml_value_expr (
			ml_sym_call (&ctx, ml_slice_cstr (builtins [id].name), a.sym),
			ctx.status
		);
	}

	else if (id == BUILTIN_LOG)
	{
		out = ml_value_expr (
			ml_sym_div (
				&ctx,
				ml_sym_call (&ctx, ML_WORD_NAME (LN), a.sym),
				ml_sym_call (&ctx, ML_WORD_NAME (LN), ml_sym_int (&ctx, 10))
			),
			ctx.status
		);
	}

	return out;
}

/*
 * The dimension of a value as one coherent SI unit of it: dim (5 km) is 1 m,
 * dim (3 N) is 1 N, and a plain number has the dimension 1.
 */
static MlValue dim_value (MlArena *arena, MlValue a)
{
	return ml_choose (
		a.kind == ML_VALUE_QUANTITY,
		ml_value_quantity (ml_rat_from_i64 (arena, 1), a.dim),
		ml_choose (
			   a.kind == ML_VALUE_EXACT
			|| a.kind == ML_VALUE_APPROX
			|| a.kind == ML_VALUE_MATRIX,
			ml_value_from_i64 (arena, 1),
			error_at (ML_EVAL_TYPE)
		)
	);
}

/* The functions of the projective geometric algebra. */
static MlValue mv_builtin (
	MlArena       *arena,
	MlBuiltin     id,
	const MlValue *args,
	uint64_t      argc
)
{
	MlValue r = error_at (ML_EVAL_TYPE);

	switch (id)
	{
		case BUILTIN_MV_DUAL:
		{
			r = ml_mv_function (arena, ML_MVFN_DUAL, args [0]);
		} break;

		case BUILTIN_MV_UNDUAL:
		{
			r = ml_mv_function (arena, ML_MVFN_UNDUAL, args [0]);
		} break;

		case BUILTIN_MV_UNIT:
		{
			r = ml_mv_function (arena, ML_MVFN_UNIT, args [0]);
		} break;

		case BUILTIN_MV_SCALAR:
		{
			r = ml_mv_function (arena, ML_MVFN_SCALAR, args [0]);
		} break;

		case BUILTIN_MV_GRADE:
		{
			r = ml_mv_grade (arena, args [0], args [1]);
		} break;

		case BUILTIN_MV_INNER:
		{
			r = ml_mv_apply (arena, ML_MV_INNER, args [0], args [1]);
		} break;

		case BUILTIN_MV_JOIN:
		{
			r = ml_mv_apply (arena, ML_MV_JOIN, args [0], args [1]);
		} break;

		case BUILTIN_MV_MEET:
		{
			r = ml_mv_apply (arena, ML_MV_WEDGE, args [0], args [1]);
		} break;

		case BUILTIN_MV_POINT:
		{
			r = ml_mv_make (arena, ML_MVMAKE_POINT, args, argc);
		} break;

		case BUILTIN_MV_LINE:
		{
			r = ml_mv_make (arena, ML_MVMAKE_LINE, args, argc);
		} break;

		case BUILTIN_MV_PLANE:
		{
			r = ml_mv_make (arena, ML_MVMAKE_PLANE, args, argc);
		} break;

		case BUILTIN_MV_TRANSLATOR:
		{
			r = ml_mv_make (arena, ML_MVMAKE_TRANSLATOR, args, argc);
		} break;

		case BUILTIN_MV_ROTOR:
		default:
		{
			r = ml_mv_make (arena, ML_MVMAKE_ROTOR, args, argc);
		} break;
	}

	return r;
}

/* The functions of complex numbers and quaternions (and numbers among them). */
static MlValue hyper_builtin (MlArena *arena, MlBuiltin id, const MlValue *args)
{
	MlValue r = error_at (ML_EVAL_TYPE);

	if (id == BUILTIN_QUAT)
	{
		r = ml_hyper_quat (arena, args);
	}

	else if (id == BUILTIN_POLAR)
	{
		r = ml_hyper_polar (arena, args [0], args [1]);
	}

	else if (id == BUILTIN_ROTATE)
	{
		r = ml_hyper_rotate (arena, args [0], args [1]);
	}

	else if (id == BUILTIN_RE)
	{
		r = ml_hyper_function (arena, ML_HFN_RE, args [0]);
	}

	else if (id == BUILTIN_IM)
	{
		r = ml_hyper_function (arena, ML_HFN_IM, args [0]);
	}

	else if (id == BUILTIN_CONJ)
	{
		r = ml_hyper_function (arena, ML_HFN_CONJ, args [0]);
	}

	else if (id == BUILTIN_ARG)
	{
		r = ml_hyper_function (arena, ML_HFN_ARG, args [0]);
	}

	else if (id == BUILTIN_ABS)
	{
		r = ml_hyper_function (arena, ML_HFN_ABS, args [0]);
	}

	else if (id == BUILTIN_SQRT)
	{
		r = ml_hyper_function (arena, ML_HFN_SQRT, args [0]);
	}

	else if (id == BUILTIN_EXP)
	{
		r = ml_hyper_function (arena, ML_HFN_EXP, args [0]);
	}

	else if (id == BUILTIN_LN)
	{
		r = ml_hyper_function (arena, ML_HFN_LN, args [0]);
	}

	else if (id == BUILTIN_SIN)
	{
		r = ml_hyper_function (arena, ML_HFN_SIN, args [0]);
	}

	else if (id == BUILTIN_COS)
	{
		r = ml_hyper_function (arena, ML_HFN_COS, args [0]);
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
	int32_t has_expr   = 0;
	int32_t has_qty    = 0;
	int32_t has_hyper  = 0;
	int32_t has_list   = 0;
	int32_t has_mv     = 0;

	for (uint64_t i = 0; i < argc; i++)
	{
		has_mv     = has_mv || args [i].kind == ML_VALUE_MV;
		has_list   = has_list || ml_value_is_list (args [i]);
		has_matrix = has_matrix || ml_value_is_matrix (args [i]);
		has_expr   = has_expr || ml_value_is_expr (args [i]);
		has_qty    = has_qty || ml_value_is_quantity (args [i]);
	}

	for (uint64_t i = 0; i < argc; i++)
	{
		has_hyper = has_hyper || ml_value_is_hyper (args [i]);
	}

	if (id >= BUILTIN_MV_FIRST && id <= BUILTIN_MV_LAST)
	{
		r = mv_builtin (arena, id, args, argc);
	}

	else if (
		   argc == 1
		&& args [0].kind == ML_VALUE_MV
		&& (id == BUILTIN_INV || id == BUILTIN_NORM)
	)
	{
		r = ml_mv_function (
			arena,
			ml_choose (id == BUILTIN_INV, ML_MVFN_INVERSE, ML_MVFN_NORM),
			args [0]
		);
	}

	else if (
		   id == BUILTIN_LISTFN_REVERSE
		&& argc == 1
		&& (args [0].kind == ML_VALUE_MV || ml_value_is_number (args [0]))
	)
	{
		r = ml_mv_function (arena, ML_MVFN_REVERSE, args [0]);
	}

	else if (has_mv && id < BUILTIN_LISTFN_LIST)
	{
		/* the functions of numbers do not take multivectors */
		r = error_at (ML_EVAL_TYPE);
	}

	else if (id == BUILTIN_NOT)
	{
		r = ml_choose (
			args [0].kind == ML_VALUE_BOOL,
			ml_value_bool (!args [0].flag),
			error_at (ML_EVAL_TYPE)
		);
	}

	else if (id == BUILTIN_STEPS)
	{
		/* its value is the value of its argument; the line asks for steps */
		r = args [0];
	}

	else if (id == BUILTIN_APPROX)
	{
		/* an exact number as the nearest double; what follows is approximate */
		r = ml_choose (
			ml_value_is_number (args [0]),
			ml_value_approx (ml_value_to_double (arena, args [0])),
			error_at (ML_EVAL_TYPE)
		);
	}

	else if (id >= BUILTIN_LISTFN_LIST)
	{
		r = ml_listfn_apply (
			arena,
			(MlListFn)(id - BUILTIN_LISTFN_LIST),
			args,
			argc
		);
	}

	else if (id == BUILTIN_DIM)
	{
		r = dim_value (arena, args [0]);
	}

	else if ((id >= BUILTIN_RE && id <= BUILTIN_ROTATE) || has_hyper)
	{
		r = hyper_builtin (arena, id, args);
	}

	else if (has_expr)
	{
		r = formula_function (arena, id, args [0]);
	}

	else if (has_qty)
	{
		r = quantity_function (arena, id, args, argc);
	}

	else if (has_list && (id == BUILTIN_MIN || id == BUILTIN_MAX) && argc == 1)
	{
		r = ml_stats_extreme (arena, args [0], id == BUILTIN_MAX);
	}

	else if (id >= BUILTIN_STAT_SUM)
	{
		r = ml_stats_apply (
			arena,
			(MlStat)(id - BUILTIN_STAT_SUM),
			args,
			argc
		);
	}

	else if (has_list)
	{
		/* the number functions do not take lists */
		r = error_at (ML_EVAL_TYPE);
	}

	else if (id >= BUILTIN_DET)
	{
		r = matrix_function (arena, id, args, argc);
	}

	else if (
		   (id == BUILTIN_MIN || id == BUILTIN_MAX)
		&& argc == 1
		&& has_matrix
	)
	{
		r = ml_stats_extreme (arena, args [0], id == BUILTIN_MAX);
	}

	else if (has_matrix)
	{
		/* the number functions do not take matrices */
		r = error_at (ML_EVAL_TYPE);
	}

	else
	{
		switch (id)
		{
			case BUILTIN_SQRT:
			{
				r = sqrt_value (arena, args [0]);
			} break;

			case BUILTIN_ABS:
			{
				r = abs_value (arena, args [0]);
			} break;

			case BUILTIN_SIN:
			case BUILTIN_COS:
			case BUILTIN_TAN:
			case BUILTIN_ASIN:
			case BUILTIN_ACOS:
			case BUILTIN_ATAN:
			case BUILTIN_LN:
			case BUILTIN_LOG:
			case BUILTIN_EXP:
			{
				r = real_function (arena, id, args [0]);
			} break;

			case BUILTIN_FLOOR:
			{
				r = round_value (arena, args [0], ROUND_FLOOR);
			} break;

			case BUILTIN_CEIL:
			{
				r = round_value (arena, args [0], ROUND_CEIL);
			} break;

			case BUILTIN_ROUND:
			{
				r = round_value (arena, args [0], ROUND_NEAREST);
			} break;

			case BUILTIN_MIN:
			case BUILTIN_MAX:
			{
				r = min_max (arena, id, args, argc);
			} break;

			default:
			{
				r = gcd_lcm (arena, id, args [0], args [1]);
			} break;
		}
	}

	return r;
}

/* The operator for two values that are not lists. */
static MlValue binary_scalar (
	MlArena    *arena,
	MlNodeKind kind,
	MlValue    a,
	MlValue    b
)
{
	MlValue r;

	switch (kind)
	{
		case ML_NODE_ADD:
		{
			r = ml_value_add (arena, a, b);
		} break;

		case ML_NODE_SUB:
		{
			r = ml_value_sub (arena, a, b);
		} break;

		case ML_NODE_MUL:
		{
			r = ml_value_mul (arena, a, b);
		} break;

		case ML_NODE_DIV:
		{
			r = ml_value_div (arena, a, b);
		} break;

		case ML_NODE_TO:
		{
			r = convert_value (a, b);
		} break;

		case ML_NODE_LT:
		case ML_NODE_GT:
		case ML_NODE_LE:
		case ML_NODE_GE:
		case ML_NODE_EQ:
		case ML_NODE_NE:
		{
			/* the nodes < > <= >= == != in the order of MlCompare */
			r = ml_value_compare (
				arena,
				(MlCompare)(kind - ML_NODE_LT),
				a,
				b
			);
		} break;

		default:
		{
			r = ml_value_pow (arena, a, b);
		} break;
	}

	return r;
}

/*
 * Arithmetic on lists goes item by item: two lists of one length, or a list
 * and a single value, which is used for every item. The items are never
 * lists, so the item operator is the one of single values.
 */
static MlValue binary_list (
	MlArena    *arena,
	MlNodeKind kind,
	MlValue    a,
	MlValue    b
)
{
	uint64_t n    = ml_choose (
		a.kind == ML_VALUE_LIST,
		a.list->count,
		b.list->count
	);
	MlValue  r    = ml_value_none ();
	MlValue  *out = NULL;

	if (
		   a.kind == ML_VALUE_LIST
		&& b.kind == ML_VALUE_LIST
		&& a.list->count != b.list->count
	)
	{
		r = error_at (ML_EVAL_SIZE);
	}

	else
	{
		out = ML_ARENA_NEW_ARRAY (arena, MlValue, n + 1);
		r   = ml_choose (out == NULL, error_at (ML_EVAL_NO_MEMORY), r);
	}

	for (uint64_t i = 0; i < n && r.kind == ML_VALUE_NONE; i++)
	{
		MlValue x = ml_choose (a.kind == ML_VALUE_LIST, a.list->items [i], a);
		MlValue y = ml_choose (b.kind == ML_VALUE_LIST, b.list->items [i], b);

		out [i] = binary_scalar (arena, kind, x, y);
		r       = ml_choose (
			out [i].kind == ML_VALUE_ERROR,
			out [i],
			r
		);
	}

	return ml_choose (
		r.kind == ML_VALUE_NONE,
		ml_value_list (arena, out, n),
		r
	);
}

static MlValue binary_apply (
	MlArena    *arena,
	MlNodeKind kind,
	MlValue    a,
	MlValue    b
)
{
	return ml_choose (
		(a.kind == ML_VALUE_LIST || b.kind == ML_VALUE_LIST)
			&& kind >= ML_NODE_ADD
			&& kind <= ML_NODE_POW,
		binary_list (arena, kind, a, b),
		binary_scalar (arena, kind, a, b)
	);
}

/* The negative of every item of a list. */
static MlValue neg_list (MlArena *arena, MlValue a)
{
	MlValue *out = ML_ARENA_NEW_ARRAY (arena, MlValue, a.list->count + 1);
	MlValue r    = error_at (ML_EVAL_NO_MEMORY);

	if (out != NULL)
	{
		for (uint64_t i = 0; i < a.list->count; i++)
		{
			out [i] = ml_value_neg (arena, a.list->items [i]);
		}

		r = ml_value_list (arena, out, a.list->count);
	}

	return r;
}

/* ---- names -------------------------------------------------------------- */

typedef struct MlParamHit
{
	int32_t  found;
	uint64_t index;
} MlParamHit;

static MlParamHit param_find (const MlScope *sc, MlName id)
{
	MlParamHit hit = {
		.found = 0,
		.index = 0
	};

	if (sc->func.kind == ML_BIND_FUNC && sc->func.param_ids != NULL)
	{
		/* a defined function: its parameters were interned when it was
		   defined, so a name that is not interned is none of them */
		for (uint64_t i = 0; i < sc->func.param_n && !hit.found; i++)
		{
			if (id != ML_NAME_NONE && sc->func.param_ids [i] == id)
			{
				hit.found = 1;
				hit.index = i;
			}
		}
	}

	else if (sc->func.kind == ML_BIND_FUNC)
	{
		/* an x -> body written in a scope has no cached names: the nodes of
		   its parameters carry them */
		uint64_t n = ml_binding_param_count (sc->func);

		for (uint64_t i = 0; i < n && !hit.found; i++)
		{
			MlName p = ml_ast_name (
				&sc->func.ast,
				ml_ast_child (&sc->func.ast, sc->func.params, i)
			);

			if (id != ML_NAME_NONE && p == id)
			{
				hit.found = 1;
				hit.index = i;
			}
		}
	}

	return hit;
}

/* A unit name as a quantity of one of that unit, undefined if not a unit. */
static MlValue unit_value (MlArena *arena, MlSlice name)
{
	MlUnitMatch m = ml_unit_find (name);

	return ml_choose (
		m.found,
		ml_value_quantity (ml_unit_factor (arena, m), m.dim),
		error_at (ML_EVAL_UNDEFINED_NAME)
	);
}

/*
 * The newest local of the scope with this name, as its index plus one, 0 when
 * there is none (the value is read with local_value, only when it is needed).
 */
static uint64_t local_find (
	const MlEvalCtx *cx,
	const MlScope   *sc,
	MlName          id
)
{
	uint64_t scope = (uint64_t)(sc - cx->scopes.data);
	uint64_t found = 0;

	/* a local is interned when it is made; the scan compares numbers */
	for (uint64_t i = cx->locals.len; i > 0 && found == 0 && id != 0; i--)
	{
		const MlLocal *l = &cx->locals.data [i - 1];

		if (l->scope == scope && l->id == id)
		{
			found = i;
		}
	}

	return found;
}

static MlValue local_value (const MlEvalCtx *cx, uint64_t at)
{
	return cx->locals.data [at - 1].value;
}

/* Makes a local name of a let, a parameter or a loop variable. */
static MlResult local_push (
	MlEvalCtx *cx,
	uint64_t  scope,
	MlSlice   name,
	MlValue   value
)
{
	MlLocal l = {
		.scope = scope,
		.name  = name,
		.id    = ml_name (name),
		.value = value
	};

	return ml_choose (
		l.id != ML_NAME_NONE,
		MlLocalVec_push (&cx->locals, l),
		ML_ERR_NOMEM
	);
}

/* The value a name stands for: local, parameter, variable or constant. */
static MlValue name_value (MlEvalCtx *cx, const MlScope *sc, MlRef name)
{
	uint64_t   local = local_find (cx, sc, name.id);
	MlParamHit hit   = param_find (sc, name.id);
	MlValue    v;

	if (local != 0)
	{
		v = local_value (cx, local);
	}

	else if (hit.found)
	{
		v = sc->args [hit.index];
	}

	else
	{
		uint64_t   at   = ml_env_find (cx->env, name.id, cx->snapshot);
		MlBindKind kind = ml_env_kind (cx->env, at);

		if (kind == ML_BIND_VAR)
		{
			v = ml_env_value (cx->env, at);
		}

		else if (kind == ML_BIND_FUNC)
		{
			v = error_at (ML_EVAL_NOT_VALUE);
		}

		else
		{
			v = constant_of (name.id);

			if (v.kind == ML_VALUE_NONE)
			{
				v = hyper_unit_find (cx->arena, name.text);
			}

			/* e0 e1 e12 ...: the blades of the algebra of a pga line */
			if (v.kind == ML_VALUE_NONE && sc->ast.pga != 0)
			{
				v = ml_mv_blade (cx->arena, sc->ast.pga, name.text);
			}

			/* unit names are names of the physics domain only */
			if (v.kind == ML_VALUE_NONE)
			{
				v = ml_choose (
					sc->ast.units,
					unit_value (cx->arena, name.text),
					error_at (ML_EVAL_UNDEFINED_NAME)
				);
			}

			if (
				   v.kind == ML_VALUE_ERROR
				&& v.error == ML_EVAL_UNDEFINED_NAME
				&& cx->env->free_symbols
			)
			{
				MlSymCtx sctx;

				ml_symctx_init (&sctx, cx->arena);
				v = ml_value_expr (ml_sym_var (&sctx, name.text), sctx.status);
			}
		}
	}

	return v;
}

/* ---- symbolic calls ----------------------------------------------------- */

/*
 * simplify (f), expand (f), subs (f, x, v), diff (f, x [, n]) and
 * integrate (f, x [, a, b]): the arguments are not evaluated, they are read
 * as formulas (sym/expr.h) in which names are symbols.
 */

typedef struct MlSymCall
{
	MlEvalCtx     *cx;
	const MlScope *sc;
	uint64_t      call;
	MlSymCtx      ctx;
} MlSymCall;

/* The formula of argument n of the call; free is the symbol left alone. */
static const MlSym *formula_arg (MlSymCall *c, uint64_t n, MlSlice free)
{
	return ml_sym_from_ast (
		&c->ctx,
		c->cx->env,
		c->cx->snapshot,
		c->sc->source,
		&c->sc->ast,
		ml_ast_child (&c->sc->ast, c->call, n),
		free
	);
}

/* The name written as argument n: the symbol worked on. */
static MlSlice symbol_arg (const MlSymCall *c, uint64_t n)
{
	uint64_t arg = ml_ast_child (&c->sc->ast, c->call, n);

	return ml_choose (
		ml_ast_node (&c->sc->ast, arg).kind == ML_NODE_NAME,
		ml_ast_text (&c->sc->ast, c->sc->source, arg),
		ml_slice ("", 0)
	);
}

/* The error for a failed context. */
static MlValue sym_error (const MlSymCall *c)
{
	MlValue v = ml_value_expr (NULL, c->ctx.status);

	return ml_choose (
		c->ctx.status == ML_ERR_INVALID,
		ml_value_error (
			ML_EVAL_NOT_SYMBOLIC,
			c->ctx.fail_start,
			c->ctx.fail_len
		),
		v
	);
}

/*
 * The antiderivative at a bound of a definite integral. A bound that is
 * infinity (inf or -inf) takes the limit of the antiderivative, which must be
 * a finite value; ok is cleared when it is not.
 */
static const MlSym *bound_value (
	MlSymCall   *c,
	const MlSym *big,
	const MlSym *x,
	const MlSym *bound,
	int32_t     *ok
)
{
	int32_t     plus  = ml_sym_is_var (bound, ML_WORD_NAME (INF));
	int32_t     minus = (
		   bound->kind == ML_SYM_MUL
		&& bound->count == 2
		&& ml_sym_is_var (bound->kids [1], ML_WORD_NAME (INF))
	);
	const MlSym *out  = NULL;

	if (plus || minus)
	{
		MlSymLimit lim = ml_sym_limit (&c->ctx, big, x, bound, 0);

		*ok = lim.kind == ML_LIMIT_VALUE;
		out = ml_choose (*ok, lim.value, big);
	}

	else
	{
		out = ml_sym_substitute (&c->ctx, big, x, bound);
	}

	return out;
}

/* diff (f, x, n): the n-th derivative, n a whole number written out. */
static uint64_t derivative_order (
	const MlSymCall *c,
	uint64_t        argc,
	int32_t         *ok
)
{
	uint64_t order = 1;

	*ok = 1;

	if (argc == 3)
	{
		uint64_t arg  = ml_ast_child (&c->sc->ast, c->call, 3);
		MlSlice  text = ml_ast_text (&c->sc->ast, c->sc->source, arg);
		int64_t  n    = 0;
		MlSymCtx tmp;

		ml_symctx_init (&tmp, c->cx->arena);
		*ok = (
			   ml_ast_node (&c->sc->ast, arg).kind == ML_NODE_NUMBER
			&& ml_sym_to_i64 (
				ml_sym_num (&tmp, ml_rat_parse_decimal (c->cx->arena, text)),
				&n
			)
			&& n >= 1
			&& n <= 20
		);
		order = (uint64_t)n;
	}

	return order;
}

/* "F(x) = ..., then F(b) - F(a):" for a definite integral. */
static void log_antiderivative (MlSymCall *c, const MlSym *big)
{
	if (c->cx->log != NULL && c->cx->log->status == ML_OK)
	{
		MlStr text;

		ml_str_init (&text);
		c->cx->log->status = ml_str_append (&text, ml_slice_cstr ("F(x) = "));

		if (c->cx->log->status == ML_OK)
		{
			c->cx->log->status = ml_sym_format (&text, big);
		}

		if (c->cx->log->status == ML_OK)
		{
			c->cx->log->status = ml_str_append (
				&text,
				ml_slice_cstr (", the integral is F(upper) - F(lower):")
			);
		}

		if (c->cx->log->status == ML_OK)
		{
			ml_steplog_line (c->cx->log, ml_str_view (text));
		}

		ml_str_free (&text);
	}
}

static MlValue symbolic_call (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      call,
	MlSlice       name,
	uint64_t      argc
)
{
	MlSymCall c;
	MlValue   out  = ml_value_none ();
	MlSlice   none = ml_slice ("", 0);
	int32_t   is_diff      = ML_WORD_IS (name, DIFF);
	int32_t   is_integrate = ML_WORD_IS (name, INTEGRATE);
	int32_t   is_subs      = ML_WORD_IS (name, SUBS);
	int32_t   is_limit     = ML_WORD_IS (name, LIMIT);
	int32_t   is_solve     = ML_WORD_IS (name, SOLVE);
	int32_t   fits         = (
		   ml_choose (
			   is_diff,
			   argc == 2 || argc == 3,
			   ml_choose (
				   is_integrate,
				   argc == 2 || argc == 4,
				   ml_choose (
					   is_limit,
					   argc == 3 || argc == 4,
					   ml_choose (
						   is_solve,
						   argc == 2,
						   ml_choose (is_subs, argc == 3, argc == 1)
					   )
				   )
			   )
		   )
	);

	c.cx   = cx;
	c.sc   = sc;
	c.call = call;
	ml_symctx_init (&c.ctx, cx->arena);
	c.ctx.log = cx->log;

	if (is_diff)
	{
		ml_steplog_line (
			cx->log,
			ml_slice_cstr ("Differentiate, rule by rule:")
		);
	}

	else if (is_integrate)
	{
		ml_steplog_line (cx->log, ml_slice_cstr ("Integrate with the table:"));
	}

	else if (is_limit)
	{
		ml_steplog_line (cx->log, ml_slice_cstr ("Limit:"));
	}

	else if (is_solve)
	{
		ml_steplog_line (cx->log, ml_slice_cstr ("Solve (real solutions):"));
	}

	else if (!is_subs)
	{
		ml_steplog_line (
			cx->log,
			ml_choose (
				ML_WORD_IS (name, EXPAND),
				ml_slice_cstr ("Multiply out:"),
				ml_slice_cstr ("Simplify:")
			)
		);
	}

	if (!fits)
	{
		out = error_at (ML_EVAL_ARITY);
	}

	else if (!is_diff && !is_integrate && !is_subs && !is_limit && !is_solve)
	{
		const MlSym *f;

		/* to show how it simplifies, the formula is taken as written */
		c.ctx.raw = cx->log != NULL;
		f         = formula_arg (&c, 1, none);
		c.ctx.raw = 0;
		out = ml_choose (
			ML_WORD_IS (name, EXPAND),
			ml_value_expr (ml_sym_expand (&c.ctx, f), c.ctx.status),
			ml_value_expr (ml_sym_simplify (&c.ctx, f), c.ctx.status)
		);
		out = ml_choose (c.ctx.status == ML_OK, out, sym_error (&c));
	}

	else
	{
		MlSlice      var = symbol_arg (&c, 2);
		const MlSym  *x;
		const MlSym  *f;
		const MlSym  *result = NULL;

		if (var.len == 0)
		{
			out = ml_value_error (
				ML_EVAL_NOT_SYMBOLIC,
				ml_ast_node (&sc->ast, ml_ast_child (&sc->ast, call, 2)).start,
				ml_ast_node (&sc->ast, ml_ast_child (&sc->ast, call, 2)).len
			);
		}

		else
		{
			x = ml_sym_var (&c.ctx, var);
			f = formula_arg (&c, 1, var);

			if (is_subs)
			{
				result = ml_sym_substitute (
					&c.ctx,
					f,
					x,
					formula_arg (&c, 3, none)
				);
			}

			else if (is_solve)
			{
				MlSymSolution sol = ml_sym_solve (&c.ctx, f, x);

				if (sol.found)
				{
					result = sol.set;
				}

				else if (c.ctx.status == ML_OK)
				{
					out = error_at (ML_EVAL_CANNOT_SOLVE);
				}
			}

			else if (is_limit)
			{
				const MlSym *at   = formula_arg (&c, 3, var);
				int32_t     side  = 0;
				int32_t     ok    = 1;
				MlSymLimit  lim;

				if (argc == 4)
				{
					int64_t s64 = 0;

					ok   = ml_sym_to_i64 (formula_arg (&c, 4, none), &s64);
					side = ml_choose (s64 > 0, 1, ml_choose (s64 < 0, -1, 0));
				}

				lim = ml_sym_limit (&c.ctx, f, x, at, ml_choose (ok, side, 0));

				if (!ok)
				{
					out = error_at (ML_EVAL_NOT_INTEGER);
				}

				else if (lim.kind == ML_LIMIT_VALUE)
				{
					result = lim.value;
				}

				else if (lim.kind == ML_LIMIT_PINF)
				{
					result = ml_sym_var (&c.ctx, ML_WORD_NAME (INF));
				}

				else if (lim.kind == ML_LIMIT_NINF)
				{
					result = ml_sym_neg (
						&c.ctx,
						ml_sym_var (&c.ctx, ML_WORD_NAME (INF))
					);
				}

				else if (c.ctx.status == ML_OK)
				{
					out = error_at (ML_EVAL_NO_LIMIT);
				}
			}

			else if (is_diff)
			{
				int32_t  ok    = 1;
				uint64_t order = derivative_order (&c, argc, &ok);

				result = f;

				for (uint64_t i = 0; i < order && ok; i++)
				{
					result = ml_sym_diff (&c.ctx, result, x);
				}

				out = ml_choose (ok, out, error_at (ML_EVAL_NOT_INTEGER));
				result = ml_choose (ok, result, NULL);
			}

			else
			{
				MlSymIntegral big = ml_sym_integrate (&c.ctx, f, x);

				if (!big.found && c.ctx.status == ML_OK)
				{
					out = error_at (ML_EVAL_CANNOT_INTEGRATE);
				}

				else if (argc == 4)
				{
					const MlSym *low  = formula_arg (&c, 3, none);
					const MlSym *high = formula_arg (&c, 4, none);

					log_antiderivative (&c, big.value);

					int32_t ok_low  = 1;
					int32_t ok_high = 1;
					const MlSym *at_high = bound_value (
						&c,
						big.value,
						x,
						high,
						&ok_high
					);
					const MlSym *at_low = bound_value (
						&c,
						big.value,
						x,
						low,
						&ok_low
					);

					if (ok_low && ok_high)
					{
						result = ml_sym_sub (&c.ctx, at_high, at_low);
					}

					else if (c.ctx.status == ML_OK)
					{
						out = error_at (ML_EVAL_NO_LIMIT);
					}
				}

				else
				{
					result = big.value;
				}
			}

			if (result != NULL && c.ctx.status == ML_OK)
			{
				out = ml_value_expr (result, c.ctx.status);
			}

			else if (c.ctx.status != ML_OK)
			{
				out = sym_error (&c);
			}
		}
	}

	return out;
}

/* ---- calls -------------------------------------------------------------- */

static int32_t is_symbolic_name (MlName name)
{
	return ml_word_of (name).kind == ML_WORD_SYMBOLIC;
}

/*
 * Is the node a name that stands for nothing (no variable, parameter,
 * constant or unit)? solve (f, x) with such a second argument solves the
 * equation f = 0, solve (A, b) with a matrix is the linear system.
 */
static int32_t is_unbound_symbol (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      node
)
{
	MlRef name = ref_of (sc, node);

	return (
		   ml_ast_node (&sc->ast, node).kind == ML_NODE_NAME
		&& !param_find (sc, name.id).found
		&& !local_find (cx, sc, name.id) != 0
		&& ml_env_kind (
			cx->env,
			ml_env_find (cx->env, name.id, cx->snapshot)
		) == ML_BIND_NONE
		&& constant_of (name.id).kind == ML_VALUE_NONE
		&& !(sc->ast.units && ml_unit_find (name.text).found)
	);
}

/* ---- functions as values ------------------------------------------------ */

/* The value a parameter or a local of the scope holds, none if no name. */
static MlValue held_value (const MlEvalCtx *cx, const MlScope *sc, MlRef name)
{
	uint64_t   local = local_find (cx, sc, name.id);
	MlParamHit hit   = param_find (sc, name.id);
	MlValue    v     = ml_value_none ();

	if (local != 0)
	{
		v = local_value (cx, local);
	}

	else if (hit.found)
	{
		v = sc->args [hit.index];
	}

	return v;
}

/* Can the builtin be called with values (not if, map, sum (i, ...))? */
static int32_t plain_builtin (MlBuiltin id)
{
	return (
		   id != BUILTIN_NONE
		&& id != BUILTIN_IF
		&& id != BUILTIN_MAP
		&& id != BUILTIN_FILTER
		&& id != BUILTIN_FOLD
		&& id != BUILTIN_STAT_SUM
		&& id != BUILTIN_STAT_PROD
	);
}

/* What a call of a name runs: a defined function, a lambda or a built in. */
typedef struct MlTarget
{
	int32_t   found;
	int32_t   builtin;
	MlBuiltin id;
	MlBinding b;
} MlTarget;

static MlTarget target_of_value (const MlEvalCtx *cx, MlValue v)
{
	MlTarget t =
	{
		.found   = 0,
		.builtin = 0,
		.id      = BUILTIN_NONE
	};

	memset (&t.b, 0, sizeof t.b);

	if (v.kind == ML_VALUE_FUNC && v.fn_kind == ML_FN_USER)
	{
		t.b     = ml_env_lookup (cx->env, v.show, cx->snapshot);
		t.found = t.b.kind == ML_BIND_FUNC;
	}

	else if (v.kind == ML_VALUE_FUNC && v.fn_kind == ML_FN_BUILTIN)
	{
		t.found   = 1;
		t.builtin = 1;
		t.id      = (MlBuiltin)v.fn_node;
	}

	else if (v.kind == ML_VALUE_FUNC && v.fn_scope < cx->scopes.len)
	{
		/* x -> body written in an enclosing scope: a function of its own */
		const MlScope *home   = &cx->scopes.data [v.fn_scope];
		uint64_t      params = ml_ast_child (&home->ast, v.fn_node, 0);

		t.b.kind   = ML_BIND_FUNC;
		t.b.source = home->source;
		t.b.ast    = home->ast;
		t.b.params = params;
		t.b.body   = ml_ast_child (&home->ast, v.fn_node, 1);
		t.found    = 1;
	}

	return t;
}

/* The function a name stands for: one a parameter or a local holds, else a
   defined one. */
static MlTarget call_target (MlEvalCtx *cx, const MlScope *sc, MlRef name)
{
	MlValue held = held_value (cx, sc, name);
	MlTarget t   = target_of_value (cx, held);

	if (!t.found && held.kind == ML_VALUE_NONE)
	{
		t.b     = ml_env_binding (
			cx->env,
			ml_env_find (cx->env, name.id, cx->snapshot)
		);
		t.found = t.b.kind == ML_BIND_FUNC;
	}

	return t;
}

/* How many arguments the target takes at least and at most. */
static uint64_t target_min (MlTarget t)
{
	return ml_choose (
		t.builtin,
		builtins [t.id].min,
		ml_binding_param_count (t.b)
	);
}

static uint64_t target_max (MlTarget t)
{
	return ml_choose (
		t.builtin,
		builtins [t.id].max,
		ml_binding_param_count (t.b)
	);
}

/*
 * An argument of a call of a function of the user that is a function: a
 * lambda, or the name of a function (that is not hidden by a variable).
 */
static int32_t func_argument (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      node,
	uint64_t      scope,
	MlValue       *out
)
{
	MlNode  n     = ml_ast_node (&sc->ast, node);
	int32_t found = 0;

	*out = ml_value_none ();

	if (n.kind == ML_NODE_LAMBDA)
	{
		out->kind     = ML_VALUE_FUNC;
		out->fn_kind  = ML_FN_LAMBDA;
		out->fn_node  = node;
		out->fn_scope = scope;
		found         = 1;
	}

	else if (n.kind == ML_NODE_NAME)
	{
		MlRef      name  = ref_of (sc, node);
		MlBindKind bound = ml_env_kind (
			cx->env,
			ml_env_find (cx->env, name.id, cx->snapshot)
		);
		MlBuiltin  id    = builtin_of (name.id);
		int32_t    taken = (
			   local_find (cx, sc, name.id) != 0
			|| param_find (sc, name.id).found
			|| bound == ML_BIND_VAR
		);

		if (!taken && bound == ML_BIND_FUNC)
		{
			out->kind    = ML_VALUE_FUNC;
			out->fn_kind = ML_FN_USER;
			out->show    = name.text;
			found        = 1;
		}

		else if (!taken && bound == ML_BIND_NONE && plain_builtin (id))
		{
			out->kind    = ML_VALUE_FUNC;
			out->fn_kind = ML_FN_BUILTIN;
			out->fn_node = (uint64_t)id;
			out->show    = name.text;
			found        = 1;
		}
	}

	return found;
}

static MlCallKind call_kind (MlEvalCtx *cx, const MlScope *sc, MlRef name)
{
	MlCallKind kind  = CALL_UNKNOWN;
	MlBindKind bound = ml_env_kind (
		cx->env,
		ml_env_find (cx->env, name.id, cx->snapshot)
	);

	if (held_value (cx, sc, name).kind == ML_VALUE_FUNC)
	{
		kind = CALL_USER;
	}

	else if (
		   param_find (sc, name.id).found
		|| local_find (cx, sc, name.id) != 0
		|| bound == ML_BIND_VAR
	)
	{
		kind = CALL_PRODUCT;
	}

	else if (bound == ML_BIND_FUNC)
	{
		kind = CALL_USER;
	}

	else if (is_symbolic_name (name.id))
	{
		kind = CALL_SYMBOLIC;
	}

	else if (builtin_of (name.id) != BUILTIN_NONE)
	{
		kind = CALL_BUILTIN;
	}

	else if (
		   constant_of (name.id).kind != ML_VALUE_NONE
		|| hyper_unit_find (cx->arena, name.text).kind != ML_VALUE_NONE
	)
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

	else if (node.kind == ML_NODE_LAMBDA)
	{
		/* x -> body is only an argument of map, filter, fold and the like */
		v = error_at (ML_EVAL_NOT_VALUE);
	}

	else if (node.kind == ML_NODE_NAME)
	{
		v = name_value (cx, sc, ref_of (sc, idx));
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
	MlFrame f = {
		.node  = node,
		.scope = scope,
		.step  = 0,
		.kind  = CALL_NEW,
		.mark  = 0,
		.index = 0,
		.vstart = 0
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

	if (MlValueVec_push (&cx->vals, v) == ML_OK)
	{
		/* an error with no place yet gets the place of this node, in the
		   pushed value itself, without copying it again */
		MlValue *top = &cx->vals.data [cx->vals.len - 1];

		if (top->kind == ML_VALUE_ERROR && top->error_len == 0)
		{
			top->error_start = node.start;
			top->error_len   = node.len;
		}
	}

	else
	{
		cx->failed = 1;
	}

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
		|| kind == ML_NODE_TO
		|| (kind >= ML_NODE_ADD && kind <= ML_NODE_POW)
		|| (kind >= ML_NODE_LT && kind <= ML_NODE_OR)
		|| kind == ML_NODE_INDEX
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
	MlValue a    = MlValueVec_get (&cx->vals, cx->vals.len - n);
	MlValue r;
	int32_t func = 0;

	for (uint64_t i = 0; i < n; i++)
	{
		func = func || (
			cx->vals.data [cx->vals.len - n + i].kind == ML_VALUE_FUNC
		);
	}

	if (func)
	{
		/* a function is called or given to map, it is not part of a sum */
		r = error_at (ML_EVAL_TYPE);
	}

	else if (kind == ML_NODE_LIST)
	{
		r = list_value (cx->arena, &cx->vals.data [cx->vals.len - n], n);
	}

	else if (kind == ML_NODE_NEG && a.kind == ML_VALUE_LIST)
	{
		r = neg_list (cx->arena, a);
	}

	else if (kind == ML_NODE_INDEX)
	{
		r = ml_list_item (cx->arena, a, top_value (cx));
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

/* "x" and the coherent SI unit of the dimension: "1143/1250 m". */
static MlResult append_si (MlStr *line, MlRat x, MlDim dim)
{
	MlSlice  name = ml_dim_unit_name (dim);
	MlResult st   = ml_rat_format (line, x);

	if (st == ML_OK)
	{
		st = ml_str_append (line, ml_slice_cstr (" "));
	}

	if (st == ML_OK && name.len > 0)
	{
		st = ml_str_append (line, name);
	}

	else if (st == ML_OK)
	{
		st = ml_dim_format (line, dim);
	}

	return st;
}

/* Writes the line to the log when it was built without a failure. */
static void log_line (MlEvalCtx *cx, MlStr *line, MlResult st)
{
	if (st == ML_OK)
	{
		ml_steplog_line (cx->log, ml_str_view (*line));
	}

	ml_str_free (line);
}

/*
 * The steps of a conversion: the value in SI units, what one of the unit is
 * worth in SI units, and the division.
 */
static void log_conversion (MlEvalCtx *cx, MlFrame f, MlValue r)
{
	const MlScope *sc   = &cx->scopes.data [f.scope];
	MlSlice       from = ml_ast_text (
		&sc->ast,
		sc->source,
		ml_ast_child (&sc->ast, f.node, 0)
	);
	MlStr         line;
	MlResult      st;

	if (cx->log != NULL)
	{
		ml_str_init (&line);
		st = ml_str_appendf (
			&line,
			"%.*s in SI units: ",
			(int)from.len,
			from.data
		);

		if (st == ML_OK)
		{
			st = append_si (&line, r.exact, r.dim);
		}

		log_line (cx, &line, st);

		ml_str_init (&line);
		st = ml_str_appendf (
			&line,
			"1 %.*s = ",
			(int)r.show.len,
			r.show.data
		);

		if (st == ML_OK)
		{
			st = append_si (&line, r.scale, r.dim);
		}

		log_line (cx, &line, st);

		ml_str_init (&line);
		st = ml_rat_format (&line, r.exact);

		if (st == ML_OK)
		{
			st = ml_str_append (&line, ml_slice_cstr (" / "));
		}

		if (st == ML_OK)
		{
			st = ml_rat_format (&line, r.scale);
		}

		if (st == ML_OK)
		{
			st = ml_str_append (&line, ml_slice_cstr (" = "));
		}

		if (st == ML_OK)
		{
			st = ml_rat_format (
				&line,
				ml_rat_div (cx->arena, r.exact, r.scale)
			);
		}

		if (st == ML_OK)
		{
			st = ml_str_append (&line, ml_slice_cstr (" "));
		}

		if (st == ML_OK)
		{
			st = ml_str_append (&line, r.show);
		}

		log_line (cx, &line, st);
	}
}

/* The result of a conversion remembers the unit it was asked for. */
static MlValue show_unit (MlEvalCtx *cx, MlFrame f, MlValue r)
{
	const MlScope *sc   = &cx->scopes.data [f.scope];
	MlValue       unit = top_value (cx);
	MlSlice       text = ml_ast_text (
		&sc->ast,
		sc->source,
		ml_ast_child (&sc->ast, f.node, 1)
	);

	if (r.kind == ML_VALUE_QUANTITY && unit.kind == ML_VALUE_QUANTITY)
	{
		char *copy = ml_arena_strdup (cx->arena, text);

		if (copy != NULL)
		{
			r.show  = ml_slice (copy, text.len);
			r.scale = unit.exact;
			log_conversion (cx, f, r);
		}
	}

	return r;
}

/*
 * and, or: the left operand is on vals (step 1), or both (step 2). A left
 * operand that decides the result (false and ..., true or ...) ends it
 * without looking at the right one; otherwise the result is the right one.
 */
static void step_logic (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc    = &cx->scopes.data [f.scope];
	MlValue       top   = top_value (cx);
	int32_t       is_or = node.kind == ML_NODE_OR;

	if (top.kind != ML_VALUE_BOOL)
	{
		finish (cx, f.step, error_at (ML_EVAL_TYPE), node);
	}

	else if (f.step == 1 && top.flag == is_or)
	{
		finish (cx, 1, top, node);
	}

	else if (f.step == 1)
	{
		set_step (cx, 2);
		push_frame (cx, ml_ast_child (&sc->ast, f.node, 1), f.scope);
	}

	else
	{
		finish (cx, 2, top, node);
	}
}

static void step_operator (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc   = &cx->scopes.data [f.scope];
	uint64_t      want = ml_choose (
		node.kind == ML_NODE_LIST,
		ml_ast_child_count (&sc->ast, f.node),
		ml_choose (
			   node.kind == ML_NODE_TO
			|| (node.kind >= ML_NODE_ADD && node.kind <= ML_NODE_POW)
			|| (node.kind >= ML_NODE_LT && node.kind <= ML_NODE_OR)
			|| node.kind == ML_NODE_INDEX,
			2,
			1
		)
	);

	if (f.step > 0 && top_is_error (cx))
	{
		finish (cx, f.step, top_value (cx), node);
	}

	else if (
		   (node.kind == ML_NODE_AND || node.kind == ML_NODE_OR)
		&& f.step > 0
	)
	{
		step_logic (cx, f, node);
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
		MlValue r = operator_apply (cx, node.kind, want);

		if (node.kind == ML_NODE_TO)
		{
			r = show_unit (cx, f, r);
		}

		finish (cx, want, r, node);
	}

	else
	{
		set_step (cx, f.step + 1);
		push_frame (cx, ml_ast_child (&sc->ast, f.node, f.step), f.scope);
	}
}

/* ---- calls ---- */

static int32_t is_hof (MlBuiltin id, uint64_t argc);

/* Looks at the callee and either ends the call with an error or prepares it. */
static void call_begin (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc     = &cx->scopes.data [f.scope];
	uint64_t      callee = ml_ast_child (&sc->ast, f.node, 0);
	MlNode        cnode  = ml_ast_node (&sc->ast, callee);
	MlRef         name   = ref_of (sc, callee);
	uint64_t      argc   = ml_ast_child_count (&sc->ast, f.node) - 1;
	MlCallKind    kind   = ml_choose (
		cnode.kind == ML_NODE_NAME,
		call_kind (cx, sc, name),
		CALL_UNKNOWN
	);
	MlValue       r      = ml_value_none ();

	if (
		   kind == CALL_BUILTIN
		&& argc == 2
		&& REF_IS (name, SOLVE)
		&& is_unbound_symbol (cx, sc, ml_ast_child (&sc->ast, f.node, 2))
	)
	{
		kind = CALL_SYMBOLIC;
	}

	if (kind == CALL_BUILTIN && builtin_of (name.id) == BUILTIN_IF)
	{
		kind = CALL_IF;
	}

	if (kind == CALL_BUILTIN && is_hof (builtin_of (name.id), argc))
	{
		kind = CALL_HOF;
	}

	if (kind == CALL_SYMBOLIC)
	{
		r = symbolic_call (cx, sc, f.node, name.text, argc);
	}

	else if (kind == CALL_UNKNOWN)
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
		MlTarget t = call_target (cx, sc, name);

		if (!t.found || argc < target_min (t) || argc > target_max (t))
		{
			r = error_at (ML_EVAL_ARITY);
		}
	}

	else if (kind == CALL_BUILTIN || kind == CALL_IF || kind == CALL_HOF)
	{
		MlBuiltin id = builtin_of (name.id);

		if (
			   argc < builtins [id].min
			|| argc > builtins [id].max
			|| (
				   (id == BUILTIN_STAT_SUM || id == BUILTIN_STAT_PROD)
				&& argc != 1
				&& argc != 4
			)
		)
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

	if (r.kind == ML_VALUE_ERROR || kind == CALL_SYMBOLIC)
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
	MlTarget      t     = call_target (
		cx,
		sc,
		ref_of (sc, ml_ast_child (&sc->ast, f.node, 0))
	);
	MlBinding     b     = t.b;
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

	else if (t.builtin)
	{
		/* a built in function held by a parameter: no scope of its own */
		finish (
			cx,
			argc,
			builtin_apply (
				cx->arena,
				t.id,
				&cx->vals.data [cx->vals.len - argc],
				argc
			),
			node
		);
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

	/* a lambda that was written in the scope that has just ended is gone */
	else if (
		   r.kind == ML_VALUE_FUNC
		&& r.fn_kind == ML_FN_LAMBDA
		&& r.fn_scope >= cx->scopes.len
	)
	{
		r = ml_value_error (ML_EVAL_NOT_VALUE, node.start, node.len);
	}

	finish (cx, 0, r, node);
}

/*
 * if (c, a, b): the condition first (step 0), then only the branch it picks
 * (step 1 to 2), so what the other branch would do never happens.
 */
static void step_if (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc = &cx->scopes.data [f.scope];

	if (f.step == 0)
	{
		set_step (cx, 1);
		push_frame (cx, ml_ast_child (&sc->ast, f.node, 1), f.scope);
	}

	else if (f.step == 1 && top_value (cx).kind == ML_VALUE_BOOL)
	{
		uint64_t branch = ml_choose (top_value (cx).flag, 2, 3);

		set_step (cx, 2);
		push_frame (cx, ml_ast_child (&sc->ast, f.node, branch), f.scope);
	}

	else if (f.step == 1 && top_is_error (cx))
	{
		finish (cx, 1, top_value (cx), node);
	}

	else if (f.step == 1)
	{
		finish (cx, 1, error_at (ML_EVAL_TYPE), node);
	}

	else
	{
		finish (cx, 2, top_value (cx), node);
	}
}

/* ---- map, filter, fold and sum (i, a, b, e) ------------------------------ */

/*
 * The loops of the language. The function is not evaluated as an argument: it
 * is a name (a function of the user, a built in one) or a lambda written right
 * there, x -> body, and it is run once for every item. A lambda runs in the
 * scope it is written in with its names as locals, so it sees the names of
 * its line; nothing else is kept. Items and results wait on vals:
 *
 *	map, filter   [list, result, result ...]
 *	fold          [list, accumulator]
 *	sum, prod     [a, b, accumulator]
 *
 * and the frame's step says where it is: the arguments (0 to 2), then 10 to
 * start the next item or end, and 11 when the function has given its result.
 */

#define HOF_ITEM   10
#define HOF_RESULT 11

static int32_t is_hof (MlBuiltin id, uint64_t argc)
{
	return (
		   id == BUILTIN_MAP
		|| id == BUILTIN_FILTER
		|| id == BUILTIN_FOLD
		|| ((id == BUILTIN_STAT_SUM || id == BUILTIN_STAT_PROD) && argc == 4)
	);
}

typedef enum HofKind
{
	HOF_BAD = 0,
	HOF_LAMBDA,
	HOF_USER,
	HOF_BUILTIN
} HofKind;

/* What the function argument of a loop is. */
typedef struct HofTarget
{
	HofKind     kind;
	MlErrorKind error;
	uint64_t    node;   /* the lambda or the name */
	MlBinding   user;
	MlBuiltin   id;
} HofTarget;

static HofTarget hof_target (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      node,
	uint64_t      want
)
{
	MlNode    n = ml_ast_node (&sc->ast, node);
	HofTarget t = {
		.kind  = HOF_BAD,
		.error = ML_EVAL_NOT_FUNCTION,
		.node  = node,
		.id    = BUILTIN_NONE
	};

	memset (&t.user, 0, sizeof t.user);

	if (n.kind == ML_NODE_LAMBDA)
	{
		uint64_t have = ml_ast_child_count (
			&sc->ast,
			ml_ast_child (&sc->ast, node, 0)
		);

		t.kind  = ml_choose (have == want, HOF_LAMBDA, HOF_BAD);
		t.error = ml_choose (have == want, ML_EVAL_NONE, ML_EVAL_ARITY);
	}

	else if (n.kind == ML_NODE_NAME)
	{
		MlRef     name = ref_of (sc, node);
		MlTarget  held = call_target (cx, sc, name);
		MlBinding b    = held.b;
		MlBuiltin id   = builtin_of (name.id);

		if (held.found && held.builtin)
		{
			int32_t fits = (
				   want >= builtins [held.id].min
				&& want <= builtins [held.id].max
			);

			t.kind  = ml_choose (fits, HOF_BUILTIN, HOF_BAD);
			t.error = ml_choose (fits, ML_EVAL_NONE, ML_EVAL_ARITY);
			t.id    = held.id;
		}

		else if (held.found)
		{
			int32_t fits = ml_binding_param_count (b) == want;

			t.kind  = ml_choose (fits, HOF_USER, HOF_BAD);
			t.error = ml_choose (fits, ML_EVAL_NONE, ML_EVAL_ARITY);
			t.user  = b;
		}

		else if (
			   b.kind == ML_BIND_NONE
			&& id != BUILTIN_NONE
			&& id != BUILTIN_IF
			&& !is_hof (id, 4)
			&& id < BUILTIN_LISTFN_LIST
		)
		{
			int32_t fits = (
				   want >= builtins [id].min
				&& want <= builtins [id].max
			);

			t.kind  = ml_choose (fits, HOF_BUILTIN, HOF_BAD);
			t.error = ml_choose (fits, ML_EVAL_NONE, ML_EVAL_ARITY);
			t.id    = id;
		}

		else if (b.kind == ML_BIND_NONE)
		{
			t.error = ML_EVAL_UNDEFINED_NAME;
		}
	}

	return t;
}

/* Starts the function on the arguments; its result will be on top of vals. */
static void hof_apply (
	MlEvalCtx     *cx,
	MlFrame       f,
	HofTarget     t,
	const MlValue *args,
	uint64_t      nargs
)
{
	const MlScope *sc = &cx->scopes.data [f.scope];

	cx->frames.data [cx->frames.len - 1].mark = cx->locals.len;

	if (t.kind == HOF_LAMBDA)
	{
		uint64_t params = ml_ast_child (&sc->ast, t.node, 0);

		for (uint64_t i = 0; i < nargs; i++)
		{
			MlSlice nm = ml_ast_text (
				&sc->ast,
				sc->source,
				ml_ast_child (&sc->ast, params, i)
			);

			if (local_push (cx, f.scope, nm, args [i]) != ML_OK)
			{
				cx->failed = 1;
			}
		}

		push_frame (cx, ml_ast_child (&sc->ast, t.node, 1), f.scope);
	}

	else if (t.kind == HOF_USER)
	{
		MlValue *own = ML_ARENA_NEW_ARRAY (cx->arena, MlValue, nargs + 1);
		MlScope rec;

		if (own == NULL)
		{
			push_value (cx, error_at (ML_EVAL_NO_MEMORY));
		}

		else
		{
			for (uint64_t i = 0; i < nargs; i++)
			{
				own [i] = args [i];
			}

			rec.source = t.user.source;
			rec.ast    = t.user.ast;
			rec.func   = t.user;
			rec.args   = own;

			if (MlScopeVec_push (&cx->scopes, rec) != ML_OK)
			{
				cx->failed = 1;
			}

			else
			{
				push_frame (cx, t.user.body, cx->scopes.len - 1);
			}
		}
	}

	else
	{
		push_value (cx, builtin_apply (cx->arena, t.id, args, nargs));
	}
}

/* The value of a loop argument as a list (a vector is read as one). */
static MlValue hof_list (MlEvalCtx *cx, MlValue v)
{
	return ml_choose (
		v.kind == ML_VALUE_LIST || v.kind == ML_VALUE_ERROR,
		v,
		ml_listfn_apply (cx->arena, ML_LISTFN_TOLIST, &v, 1)
	);
}

/* Ends the loop with a value or an error: everything it kept is dropped. */
static void hof_finish (MlEvalCtx *cx, MlFrame f, MlNode node, MlValue v)
{
	finish (cx, cx->vals.len - f.vstart, v, node);
}

/* sum (i, a, b, e): the bounds are whole numbers a and b at vstart. */
static int32_t series_bounds (MlEvalCtx *cx, MlFrame f, int64_t *a, int64_t *b)
{
	MlI64Result x = ml_value_to_i64 (cx->vals.data [f.vstart]);
	MlI64Result y = ml_value_to_i64 (cx->vals.data [f.vstart + 1]);

	*a = x.value;
	*b = y.value;

	return x.ok && y.ok;
}

/* The loop is at an item (or the end): start the function on the next one. */
static void hof_next (MlEvalCtx *cx, MlFrame f, MlNode node, MlBuiltin id)
{
	const MlScope *sc      = &cx->scopes.data [f.scope];
	int32_t       series   = id == BUILTIN_STAT_SUM || id == BUILTIN_STAT_PROD;
	int32_t       fold     = id == BUILTIN_FOLD;
	uint64_t      fn_node  = ml_ast_child (&sc->ast, f.node, 1);
	uint64_t      count    = 0;
	int64_t       a        = 0;
	int64_t       b        = 0;
	MlValue       args [2];
	HofTarget     t;

	if (series)
	{
		series_bounds (cx, f, &a, &b);
		count = ml_choose (b >= a, (uint64_t)(b - a) + 1, 0);
	}

	else
	{
		count = cx->vals.data [f.vstart].list->count;
	}

	t = hof_target (cx, sc, fn_node, ml_choose (fold, 2, 1));

	if (f.index >= count)
	{
		MlValue  done;
		uint64_t first = f.vstart + 1;

		if (series)
		{
			done = cx->vals.data [f.vstart + 2];
		}

		else if (fold)
		{
			done = cx->vals.data [f.vstart + 1];
		}

		else
		{
			done = ml_value_list (
				cx->arena,
				&cx->vals.data [first],
				cx->vals.len - first
			);
		}

		hof_finish (cx, f, node, done);
	}

	else if (!series && t.kind == HOF_BAD)
	{
		hof_finish (cx, f, node, error_at (t.error));
	}

	else
	{
		MlValue item = ml_choose (
			series,
			ml_value_exact (
				ml_rat_add (
					cx->arena,
					cx->vals.data [f.vstart].exact,
					ml_rat_from_i64 (cx->arena, (int64_t)f.index)
				)
			),
			cx->vals.data [f.vstart].list->items [f.index]
		);

		args [0] = ml_choose (fold, cx->vals.data [f.vstart + 1], item);
		args [1] = item;
		set_step (cx, HOF_RESULT);

		if (series)
		{
			/* the body is the 4th argument and i a local of its own */
			MlSlice nm = ml_ast_text (&sc->ast, sc->source, fn_node);

			cx->frames.data [cx->frames.len - 1].mark = cx->locals.len;

			if (local_push (cx, f.scope, nm, item) != ML_OK)
			{
				cx->failed = 1;
			}

			push_frame (cx, ml_ast_child (&sc->ast, f.node, 4), f.scope);
		}

		else
		{
			hof_apply (cx, f, t, args, ml_choose (fold, 2, 1));
		}
	}
}

/* Keeps the result r of the function for the loop; 0 when the loop ended. */
static int32_t hof_keep (
	MlEvalCtx *cx,
	MlFrame   f,
	MlNode    node,
	MlBuiltin id,
	MlValue   r
)
{
	int32_t going = 1;

	if (id == BUILTIN_MAP)
	{
		push_value (cx, r);
	}

	else if (id == BUILTIN_FILTER && r.kind != ML_VALUE_BOOL)
	{
		hof_finish (cx, f, node, error_at (ML_EVAL_TYPE));
		going = 0;
	}

	else if (id == BUILTIN_FILTER && r.flag)
	{
		push_value (cx, cx->vals.data [f.vstart].list->items [f.index]);
	}

	else if (id == BUILTIN_FOLD)
	{
		cx->vals.data [f.vstart + 1] = r;
	}

	else if (id == BUILTIN_STAT_SUM || id == BUILTIN_STAT_PROD)
	{
		MlValue acc = cx->vals.data [f.vstart + 2];
		MlValue now = ml_choose (
			id == BUILTIN_STAT_SUM,
			ml_value_add (cx->arena, acc, r),
			ml_value_mul (cx->arena, acc, r)
		);

		cx->vals.data [f.vstart + 2] = now;

		if (now.kind == ML_VALUE_ERROR)
		{
			hof_finish (cx, f, node, now);
			going = 0;
		}
	}

	return going;
}

/* The function has given its result: keep it according to the loop. */
static void hof_result (MlEvalCtx *cx, MlFrame f, MlNode node, MlBuiltin id)
{
	const MlScope *sc      = &cx->scopes.data [f.scope];
	MlValue       r        = top_value (cx);
	int32_t       series   = id == BUILTIN_STAT_SUM || id == BUILTIN_STAT_PROD;
	HofTarget     t        = hof_target (
		cx,
		sc,
		ml_ast_child (&sc->ast, f.node, 1),
		ml_choose (id == BUILTIN_FOLD, 2, 1)
	);

	/* what the call kept: locals of a lambda, the scope of a function */
	cx->locals.len = f.mark;

	if (!series && t.kind == HOF_USER)
	{
		cx->scopes.len--;
		r = ml_choose (
			r.kind == ML_VALUE_ERROR,
			ml_value_error (r.error, node.start, node.len),
			r
		);
	}

	cx->vals.len--;

	if (r.kind == ML_VALUE_ERROR)
	{
		hof_finish (cx, f, node, r);
	}

	else if (hof_keep (cx, f, node, id, r))
	{
		cx->frames.data [cx->frames.len - 1].index = f.index + 1;
		set_step (cx, HOF_ITEM);
	}
}

/* The values of the arguments are in: ready the list, or the bounds. */
static void hof_ready (MlEvalCtx *cx, MlFrame f, MlNode node, MlBuiltin id)
{
	int32_t series = id == BUILTIN_STAT_SUM || id == BUILTIN_STAT_PROD;
	int32_t fold   = id == BUILTIN_FOLD;
	int64_t a      = 0;
	int64_t b      = 0;

	cx->frames.data [cx->frames.len - 1].index = 0;

	if (series)
	{
		int64_t start = ml_choose (id == BUILTIN_STAT_SUM, 0, 1);

		push_value (cx, ml_value_exact (ml_rat_from_i64 (cx->arena, start)));

		if (!series_bounds (cx, f, &a, &b))
		{
			hof_finish (cx, f, node, error_at (ML_EVAL_NOT_INTEGER));
		}

		else
		{
			set_step (cx, HOF_ITEM);
		}
	}

	else
	{
		MlValue lst = hof_list (cx, top_value (cx));

		if (lst.kind == ML_VALUE_ERROR)
		{
			hof_finish (cx, f, node, lst);
		}

		else
		{
			/* fold: [init, list] becomes [list, init] */
			MlValue init = ml_choose (
				fold,
				cx->vals.data [cx->vals.len - 2],
				lst
			);

			cx->vals.data [cx->vals.len - 1] = init;

			if (fold)
			{
				cx->vals.data [cx->vals.len - 2] = lst;
			}

			set_step (cx, HOF_ITEM);
		}
	}
}

static void step_hof (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc     = &cx->scopes.data [f.scope];
	MlRef         name    = ref_of (sc, ml_ast_child (&sc->ast, f.node, 0));
	MlBuiltin     id      = builtin_of (name.id);
	int32_t       series  = id == BUILTIN_STAT_SUM || id == BUILTIN_STAT_PROD;
	uint64_t      args_n  = ml_choose (
		series,
		2,
		ml_choose (id == BUILTIN_FOLD, 2, 1)
	);

	if (f.step == 0)
	{
		cx->frames.data [cx->frames.len - 1].vstart = cx->vals.len;
		set_step (cx, 1);
		push_frame (cx, ml_ast_child (&sc->ast, f.node, 2), f.scope);
	}

	else if (f.step < HOF_ITEM && top_is_error (cx))
	{
		hof_finish (cx, f, node, top_value (cx));
	}

	else if (f.step < HOF_ITEM && f.step < args_n)
	{
		/* the next argument that is a value: the init of a fold, the bound b */
		set_step (cx, f.step + 1);
		push_frame (cx, ml_ast_child (&sc->ast, f.node, f.step + 2), f.scope);
	}

	else if (f.step < HOF_ITEM)
	{
		hof_ready (cx, f, node, id);
	}

	else if (f.step == HOF_ITEM)
	{
		hof_next (cx, f, node, id);
	}

	else
	{
		hof_result (cx, f, node, id);
	}
}

static void step_call (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc   = &cx->scopes.data [f.scope];
	uint64_t      argc = ml_ast_child_count (&sc->ast, f.node) - 1;

	if (f.kind == CALL_NEW)
	{
		call_begin (cx, f, node);
	}

	else if (f.kind == CALL_IF)
	{
		step_if (cx, f, node);
	}

	else if (f.kind == CALL_HOF)
	{
		step_hof (cx, f, node);
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
		MlRef name = ref_of (sc, ml_ast_child (&sc->ast, f.node, 0));

		finish (
			cx,
			argc,
			builtin_apply (
				cx->arena,
				builtin_of (name.id),
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

		uint64_t arg = ml_ast_child (&sc->ast, f.node, child);
		MlValue  fn;

		set_step (cx, f.step + 1);

		if (f.kind == CALL_USER && func_argument (cx, sc, arg, f.scope, &fn))
		{
			push_value (cx, fn);
		}

		else
		{
			push_frame (cx, arg, f.scope);
		}
	}
}

/* Ends the locals a seq made, and the seq with the value or the error. */
static void finish_seq (MlEvalCtx *cx, MlFrame f, MlNode node, uint64_t done)
{
	cx->locals.len = f.mark;
	finish (cx, done, top_value (cx), node);
}

/*
 * The statements of a body, one after the other. After a statement its value
 * is dropped, except for the last one, which is the value of the whole; an
 * error ends the sequence at once.
 */
static void step_seq (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc = &cx->scopes.data [f.scope];
	uint64_t      n   = ml_ast_child_count (&sc->ast, f.node);

	if (f.step == 0)
	{
		cx->frames.data [cx->frames.len - 1].mark = cx->locals.len;
		set_step (cx, 1);
		push_frame (cx, ml_ast_child (&sc->ast, f.node, 0), f.scope);
	}

	else if (f.step >= n || top_is_error (cx))
	{
		finish_seq (cx, f, node, 1);
	}

	else
	{
		cx->vals.len--;
		set_step (cx, f.step + 1);
		push_frame (cx, ml_ast_child (&sc->ast, f.node, f.step), f.scope);
	}
}

/* let name = expression: the value is the local name's from here on. */
static void step_let (MlEvalCtx *cx, MlFrame f, MlNode node)
{
	const MlScope *sc = &cx->scopes.data [f.scope];

	if (f.step == 0)
	{
		set_step (cx, 1);
		push_frame (cx, ml_ast_child (&sc->ast, f.node, 1), f.scope);
	}

	else
	{
		MlSlice nm = ml_ast_text (
			&sc->ast,
			sc->source,
			ml_ast_child (&sc->ast, f.node, 0)
		);

		if (
			   !top_is_error (cx)
			&& local_push (cx, f.scope, nm, top_value (cx)) != ML_OK
		)
		{
			cx->failed = 1;
		}

		finish (cx, 1, top_value (cx), node);
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

	else if (node.kind == ML_NODE_SEQ)
	{
		step_seq (cx, f, node);
	}

	else if (node.kind == ML_NODE_LET)
	{
		step_let (cx, f, node);
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

static MlValue eval_with (
	MlArena     *arena,
	const MlEnv *env,
	uint64_t    snapshot,
	MlSlice     source,
	const MlAst *ast,
	MlStepLog   *log
)
{
	MlEvalCtx cx = {
		.arena    = arena,
		.env      = env,
		.snapshot = snapshot,
		.failed   = 0,
		.steps    = 0,
		.log      = log
	};

	MlNode    root = ml_ast_node (ast, ast->root);
	MlValue   r    = ml_value_none ();

	if (root.kind != ML_NODE_NIL && root.kind != ML_NODE_FUNC_DEF)
	{
		MlScope line = {
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

		while (
			   cx.frames.len > 0
			&& !cx.failed
			&& cx.steps < ML_EVAL_MAX_STEPS
		)
		{
			cx.steps++;
			eval_step (&cx);
		}

		r = ml_choose (
			cx.failed,
			error_at (ML_EVAL_NO_MEMORY),
			ml_choose (
				cx.frames.len > 0,
				error_at (ML_EVAL_TOO_LONG),
				MlValueVec_get (&cx.vals, 0)
			)
		);

		/* a function is not a result: it points into the line, which ends */
		if (r.kind == ML_VALUE_FUNC)
		{
			r = error_at (ML_EVAL_NOT_VALUE);
		}
	}

	MlFrameVec_free (&cx.frames);
	MlValueVec_free (&cx.vals);
	MlScopeVec_free (&cx.scopes);
	MlLocalVec_free (&cx.locals);
	return r;
}

MlValue ml_eval (
	MlArena     *arena,
	const MlEnv *env,
	uint64_t    snapshot,
	MlSlice     source,
	const MlAst *ast
)
{
	return eval_with (arena, env, snapshot, source, ast, NULL);
}

MlValue ml_eval_steps (
	MlArena     *arena,
	const MlEnv *env,
	uint64_t    snapshot,
	MlSlice     source,
	const MlAst *ast,
	MlStepLog   *log
)
{
	return eval_with (arena, env, snapshot, source, ast, log);
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
		&& (
			   ml_value_is_number (value)
			|| ml_value_is_matrix (value)
			|| ml_value_is_expr (value)
			|| ml_value_is_quantity (value)
			|| ml_value_is_hyper (value)
			|| value.kind == ML_VALUE_PLOT
			|| value.kind == ML_VALUE_BOOL
			|| value.kind == ML_VALUE_LIST
			|| value.kind == ML_VALUE_MV
		)
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
