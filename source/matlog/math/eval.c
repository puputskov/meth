#include <matlog/math/eval.h>

#include <math.h>

/* ---- context ------------------------------------------------------------ */

typedef struct MlEvalCtx
{
	MlArena     *arena;
	const MlEnv *env;
	uint64_t    snapshot;
	uint64_t    depth; /* node levels in use, over all running functions */
} MlEvalCtx;

/* The text a node belongs to: the line, or the body of a running function. */
typedef struct MlScope
{
	MlSlice       source;
	const MlAst   *ast;
	MlBinding     func;  /* the running function, ML_BIND_NONE on the line */
	const MlValue *args; /* its arguments, one per parameter */
} MlScope;

static MlValue eval_node (MlEvalCtx *cx, const MlScope *sc, uint64_t idx);

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
	[BUILTIN_LCM]   = { .name = "lcm",   .min = 2, .max = 2 }
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

static MlValue builtin_apply (
	MlArena       *arena,
	MlBuiltin     id,
	const MlValue *args,
	uint64_t      argc
)
{
	MlValue r;

	if (id == BUILTIN_SQRT)
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

typedef enum MlCallKind
{
	CALL_UNKNOWN,
	CALL_PRODUCT, /* a(2) with a a number: a times 2 */
	CALL_USER,
	CALL_BUILTIN
} MlCallKind;

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

/* Evaluates the arguments of a call into out; gives the first error, if any. */
static MlValue eval_args (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      idx,
	MlValue       *out,
	uint64_t      count
)
{
	MlValue first = ml_value_none ();

	for (uint64_t i = 0; i < count && first.kind == ML_VALUE_NONE; i++)
	{
		MlValue v = eval_node (cx, sc, ml_ast_child (sc->ast, idx, i + 1));

		if (v.kind == ML_VALUE_ERROR)
		{
			first = v;
		}

		else
		{
			out [i] = v;
		}
	}

	return first;
}

/* The arguments of a call, in a new array (NULL when there are none). */
static MlValue call_args (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      idx,
	MlValue       **out
)
{
	uint64_t count = ml_ast_child_count (sc->ast, idx) - 1;
	MlValue  r     = ml_value_none ();

	*out = NULL;

	if (count > 0)
	{
		*out = ML_ARENA_NEW_ARRAY (cx->arena, MlValue, count);

		r = ml_choose (
			*out != NULL,
			eval_args (cx, sc, idx, *out, count),
			error_at (ML_EVAL_NO_MEMORY)
		);
	}

	return r;
}

static MlValue call_user (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      idx,
	MlNode        node
)
{
	MlScope  callee;
	MlValue  *args = NULL;
	uint64_t argc  = ml_ast_child_count (sc->ast, idx) - 1;
	MlValue  r;

	callee.func = ml_env_lookup (
		cx->env,
		ml_ast_text (sc->ast, sc->source, ml_ast_child (sc->ast, idx, 0)),
		cx->snapshot
	);

	if (argc != ml_binding_param_count (callee.func))
	{
		r = error_at (ML_EVAL_ARITY);
	}

	else
	{
		r = call_args (cx, sc, idx, &args);
	}

	if (r.kind == ML_VALUE_NONE)
	{
		callee.source = callee.func.source;
		callee.ast    = &callee.func.ast;
		callee.args   = args;
		r             = eval_node (cx, &callee, callee.func.body);

		/* the body is text of another line: point at the call instead */
		if (r.kind == ML_VALUE_ERROR)
		{
			r = ml_value_error (r.error, node.start, node.len);
		}
	}

	return r;
}

static MlValue call_builtin (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      idx,
	MlBuiltin     id
)
{
	uint64_t argc  = ml_ast_child_count (sc->ast, idx) - 1;
	MlValue  *args = NULL;
	MlValue  r;

	if (argc < builtins [id].min || argc > builtins [id].max)
	{
		r = error_at (ML_EVAL_ARITY);
	}

	else
	{
		r = call_args (cx, sc, idx, &args);
	}

	if (r.kind == ML_VALUE_NONE)
	{
		r = builtin_apply (cx->arena, id, args, argc);
	}

	return r;
}

/* a(2) where a is a number: a times 2. */
static MlValue call_product (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      idx
)
{
	uint64_t callee = ml_ast_child (sc->ast, idx, 0);
	MlNode   cnode  = ml_ast_node (sc->ast, callee);
	MlValue  r;

	if (ml_ast_child_count (sc->ast, idx) != 2)
	{
		r = error_at (ML_EVAL_NOT_FUNCTION);
	}

	else
	{
		r = ml_value_locate (
			name_value (cx, sc, ml_ast_text (sc->ast, sc->source, callee)),
			cnode.start,
			cnode.len
		);
	}

	if (r.kind != ML_VALUE_ERROR && r.kind != ML_VALUE_NONE)
	{
		r = ml_value_mul (
			cx->arena,
			r,
			eval_node (cx, sc, ml_ast_child (sc->ast, idx, 1))
		);
	}

	return r;
}

static MlValue eval_call (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      idx,
	MlNode        node
)
{
	uint64_t   callee = ml_ast_child (sc->ast, idx, 0);
	MlNode     cnode  = ml_ast_node (sc->ast, callee);
	MlSlice    name   = ml_ast_text (sc->ast, sc->source, callee);
	MlCallKind kind   = call_kind (cx, sc, name);
	MlValue    r;

	if (cnode.kind != ML_NODE_NAME)
	{
		r = error_at (ML_EVAL_NOT_FUNCTION);
	}

	else if (kind == CALL_USER)
	{
		r = call_user (cx, sc, idx, node);
	}

	else if (kind == CALL_BUILTIN)
	{
		r = call_builtin (cx, sc, idx, builtin_find (name));
	}

	else if (kind == CALL_PRODUCT)
	{
		r = call_product (cx, sc, idx);
	}

	else
	{
		r = ml_value_error (
			ML_EVAL_UNDEFINED_NAME,
			cnode.start,
			cnode.len
		);
	}

	return r;
}

/* ---- leaves ------------------------------------------------------------- */

static MlValue eval_number (MlEvalCtx *cx, const MlScope *sc, uint64_t idx)
{
	return ml_value_exact (
		ml_rat_parse_decimal (
			cx->arena,
			ml_ast_text (sc->ast, sc->source, idx)
		)
	);
}

static MlValue eval_superscript (
	MlEvalCtx     *cx,
	const MlScope *sc,
	uint64_t      idx
)
{
	MlStr   digits;
	MlValue r;

	ml_str_init (&digits);

	if (ml_superscript_to_ascii (
		&digits,
		ml_ast_text (sc->ast, sc->source, idx)
	) == ML_OK)
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

/* ---- the tree ----------------------------------------------------------- */

static MlValue eval_unary (
	MlEvalCtx     *cx,
	const MlScope *sc,
	MlNode        node,
	uint64_t      idx
)
{
	MlValue r = eval_node (cx, sc, ml_ast_child (sc->ast, idx, 0));

	if (node.kind == ML_NODE_NEG)
	{
		r = ml_value_neg (cx->arena, r);
	}

	else if (node.kind == ML_NODE_FACT)
	{
		r = factorial (cx->arena, r);
	}

	else
	{
		r = sqrt_value (cx->arena, r);
	}

	return r;
}

static MlValue eval_binary (
	MlEvalCtx     *cx,
	const MlScope *sc,
	MlNode        node,
	uint64_t      idx
)
{
	MlValue r = eval_node (cx, sc, ml_ast_child (sc->ast, idx, 0));

	if (r.kind != ML_VALUE_ERROR)
	{
		r = binary_apply (
			cx->arena,
			node.kind,
			r,
			eval_node (cx, sc, ml_ast_child (sc->ast, idx, 1))
		);
	}

	return r;
}

static MlValue eval_node (MlEvalCtx *cx, const MlScope *sc, uint64_t idx)
{
	MlNode  node = ml_ast_node (sc->ast, idx);
	MlValue v;

	cx->depth++;

	if (cx->depth > ML_EVAL_MAX_DEPTH)
	{
		v = error_at (ML_EVAL_DEPTH);
	}

	else if (node.kind == ML_NODE_NUMBER)
	{
		v = eval_number (cx, sc, idx);
	}

	else if (node.kind == ML_NODE_SUPERSCRIPT)
	{
		v = eval_superscript (cx, sc, idx);
	}

	else if (node.kind == ML_NODE_NAME)
	{
		v = name_value (cx, sc, ml_ast_text (sc->ast, sc->source, idx));
	}

	else if (
		   node.kind == ML_NODE_NEG
		|| node.kind == ML_NODE_FACT
		|| node.kind == ML_NODE_SQRT
	)
	{
		v = eval_unary (cx, sc, node, idx);
	}

	else if (node.kind >= ML_NODE_ADD && node.kind <= ML_NODE_POW)
	{
		v = eval_binary (cx, sc, node, idx);
	}

	else if (node.kind == ML_NODE_CALL)
	{
		v = eval_call (cx, sc, idx, node);
	}

	else
	{
		v = error_at (ML_EVAL_SYNTAX);
	}

	cx->depth--;
	return ml_value_locate (v, node.start, node.len);
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
	MlEvalCtx cx   =
	{
		.arena    = arena,
		.env      = env,
		.snapshot = snapshot,
		.depth    = 0
	};
	MlScope   sc   =
	{
		.source = source,
		.ast    = ast,
		.args   = NULL
	};
	MlNode    root = ml_ast_node (ast, ast->root);
	MlValue   r;

	memset (&sc.func, 0, sizeof sc.func);

	if (root.kind == ML_NODE_NIL || root.kind == ML_NODE_FUNC_DEF)
	{
		r = ml_value_none ();
	}

	else if (root.kind == ML_NODE_ASSIGN)
	{
		r = eval_node (&cx, &sc, ml_ast_child (ast, ast->root, 1));
	}

	else
	{
		r = eval_node (&cx, &sc, ast->root);
	}

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

	if (root.kind == ML_NODE_ASSIGN && ml_value_is_number (value))
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
