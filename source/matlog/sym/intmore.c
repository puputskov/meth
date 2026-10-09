#include "internal.h"

/*
 * Integration beyond the table: substitution, integration by parts and the
 * check of an antiderivative. The methods are layered so that none calls
 * back into a layer above it: substitution uses the table (and the partial
 * fractions) for what is left; parts uses those and substitution. The search
 * is a loop over candidates and a work list, never a recursion.
 */

/*
 * What a method uses for the integrals it is left with. Passing the solver in
 * keeps the methods from calling each other in a circle: substitution is
 * given a solver that may use parts, parts one that may use substitution,
 * and those two are given the table alone.
 */
typedef const MlSym *(*IntSolver) (
	MlSymCtx    *ctx,
	const MlSym *g,
	const MlSym *x
);

static const MlSym *subst_with (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlRule      *rule,
	IntSolver   solve
);

static const MlSym *parts_with (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlRule      *rule,
	IntSolver   solve
);

#define MAX_CANDIDATES 48
#define MAX_PARTS      8
#define MAX_WORK       64

static const MlSym *int_node (MlSymCtx *ctx, int64_t n)
{
	return ml_sym_int (ctx, n);
}

static int32_t is_name (MlSlice name, const char *text)
{
	return ml_slice_equal (name, ml_slice_cstr (text));
}

/* ---- the steps ---- */

void ml_int_note (
	MlSymCtx    *ctx,
	const char  *head,
	const MlSym *a,
	const char  *mid,
	const MlSym *b
)
{
	MlStepLog *log = ctx->log;

	if (log != NULL && log->status == ML_OK && ctx->status == ML_OK)
	{
		MlStr text;

		ml_str_init (&text);
		log->status = ml_str_append (&text, ml_slice_cstr (head));

		if (log->status == ML_OK && a != NULL)
		{
			log->status = ml_sym_format (&text, a);
		}

		if (log->status == ML_OK && mid != NULL)
		{
			log->status = ml_str_append (&text, ml_slice_cstr (mid));
		}

		if (log->status == ML_OK && b != NULL)
		{
			log->status = ml_sym_format (&text, b);
		}

		if (log->status == ML_OK)
		{
			ml_steplog_line (log, ml_str_view (text));
		}

		ml_str_free (&text);
	}
}

/* ---- the check ---- */

/* The value of s with x set to num / den, as a double; 0 if there is none. */
static int32_t value_at (
	MlSymCtx    *ctx,
	const MlSym *s,
	const MlSym *x,
	int64_t     num,
	int64_t     den,
	double      *out
)
{
	const MlSym *at
		= ml_sym_div (ctx, int_node (ctx, num), int_node (ctx, den));

	return ml_sym_to_double (
		ctx->arena,
		ml_sym_substitute (ctx, s, x, at),
		out
	);
}

int32_t ml_int_verify (
	MlSymCtx    *ctx,
	const MlSym *big,
	const MlSym *f,
	const MlSym *x
)
{
	static const int64_t points [] = { 31, 17, 29, -83, 57, 5 };
	static const int64_t scales [] = { 100, 10, 10, 100, 100, 2 };
	MlStepLog           *saved     = ctx->log;
	const MlSym         *slope;
	int32_t             ok         = 1;

	ctx->log = NULL;
	slope    = ml_sym_diff (ctx, big, x);

	for (uint64_t i = 0; i < ml_count (points) && ok; i++)
	{
		double want;
		double got;

		if (
			   value_at (ctx, f, x, points [i], scales [i], &want)
			&& value_at (ctx, slope, x, points [i], scales [i], &got)
		)
		{
			double scale = 1.0 + ml_choose (want < 0, -want, want);
			double gap   = want - got;

			ok = ml_choose (gap < 0, -gap, gap) <= 1e-7 * scale;
		}
	}

	ctx->log = saved;
	return ok && ctx->status == ML_OK;
}

/* ---- substitution ---- */

/* Every distinct inner part of f that contains x, the outer ones first. */
void ml_sym_collect_parts (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	SymPtrVec   *out
)
{
	SymPtrVec stack;

	SymPtrVec_init (&stack);

	if (SymPtrVec_push (&stack, f) != ML_OK)
	{
		ctx->status = ML_ERR_NOMEM;
	}

	while (stack.len > 0 && out->len < MAX_CANDIDATES && ctx->status == ML_OK)
	{
		const MlSym *s     = stack.data [stack.len - 1];
		int32_t     known = 0;

		stack.len--;

		for (uint64_t i = 0; i < out->len && !known; i++)
		{
			known = ml_sym_compare (ctx, out->data [i], s) == 0;
		}

		if (
			   !known
			&& s != f
			&& s->kind != ML_SYM_NUM
			&& s->kind != ML_SYM_VAR
			&& ml_sym_has_var (s, x->name)
			&& SymPtrVec_push (out, s) != ML_OK
		)
		{
			ctx->status = ML_ERR_NOMEM;
		}

		for (uint64_t i = s->count; i > 0 && s->kind != ML_SYM_NUM; i--)
		{
			if (SymPtrVec_push (&stack, s->kids [i - 1]) != ML_OK)
			{
				ctx->status = ML_ERR_NOMEM;
			}
		}
	}

	SymPtrVec_free (&stack);
}

static const MlSym *subst_with (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlRule      *rule,
	IntSolver   solve
)
{
	const MlSym *result = NULL;
	const MlSym *w      = ml_sym_var (ctx, ml_slice_cstr ("_w"));
	SymPtrVec   cands;
	MlStepLog   *saved  = ctx->log;

	SymPtrVec_init (&cands);
	ml_sym_collect_parts (ctx, f, x, &cands);
	*rule = ML_RULE_NONE;

	for (uint64_t i = 0;
	     i < cands.len && result == NULL && ctx->status == ML_OK;
	     i++)
	{
		const MlSym *u = cands.data [i];
		const MlSym *du;
		const MlSym *g;
		const MlSym *inner;

		ctx->log = NULL;
		du       = ml_sym_diff (ctx, u, x);

		if (!ml_sym_is_zero (du) && ctx->status == ML_OK)
		{
			g     = ml_sym_replace (ctx, ml_sym_div (ctx, f, du), u, w);
			inner = NULL;

			if (!ml_sym_has_var (g, x->name))
			{
				inner = solve (ctx, g, w);
			}

			if (inner != NULL && ctx->status == ML_OK)
			{
				const MlSym *back = ml_sym_substitute (ctx, inner, w, u);

				if (ml_int_verify (ctx, back, f, x))
				{
					result = back;
					*rule  = ML_RULE_INT_SUBSTITUTION;
					ctx->log = saved;
					ml_int_note (ctx, "substitution u = ", u, ", du = ", du);
				}
			}
		}

		ctx->log = saved;
	}

	SymPtrVec_free (&cands);
	return result;
}

/* ---- integration by parts ---- */

/* How early a factor is chosen as u: inverse trig, log, algebraic, trig, exp.
 */
static uint32_t liate_rank (const MlSym *g, const MlSym *x)
{
	uint32_t rank = 6;

	if (g->kind == ML_SYM_FUNC)
	{
		rank = ml_choose (
			is_name (g->name, "atan") || is_name (g->name, "asin")
				|| is_name (g->name, "acos"),
			1,
			ml_choose (
				is_name (g->name, "ln"),
				2,
				ml_choose (
					is_name (g->name, "sin") || is_name (g->name, "cos")
						|| is_name (g->name, "tan"),
					4,
					ml_choose (is_name (g->name, "exp"), 5, 6)
				)
			)
		);
	}

	else if (g->kind == ML_SYM_VAR || g->kind == ML_SYM_ADD)
	{
		rank = 3;
	}

	else if (g->kind == ML_SYM_POW)
	{
		rank = ml_choose (
			ml_sym_has_var (g->kids [1], x->name),
			5,
			3
		);
	}

	return rank;
}

typedef struct PartsItem
{
	const MlSym *coef;
	const MlSym *term;
} PartsItem;

ML_VEC_DEFINE (PartsVec, PartsItem)

static void push_item (
	MlSymCtx    *ctx,
	PartsVec    *work,
	const MlSym *coef,
	const MlSym *term
)
{
	PartsItem item =
	{
		.coef = coef,
		.term = term
	};

	if (work->len >= MAX_WORK || PartsVec_push (work, item) != ML_OK)
	{
		ctx->status = ML_ERR_LIMIT;
	}
}

/*
 * Splits t = u dv by the order of the ranks and integrates dv. On success
 * uv and rest (v du, expanded) are set.
 */
static int32_t split_parts (
	MlSymCtx    *ctx,
	const MlSym *t,
	const MlSym *x,
	const MlSym **uv,
	const MlSym **rest,
	IntSolver   solve
)
{
	uint64_t count = ml_choose (t->kind == ML_SYM_MUL, t->count, 1);
	int32_t  found = 0;

	for (uint32_t rank = 1; rank <= 6 && !found; rank++)
	{
		for (uint64_t i = 0; i < count && !found && ctx->status == ML_OK; i++)
		{
			const MlSym *u = ml_choose (t->kind == ML_SYM_MUL, t->kids [i], t);
			SymPtrVec   others;
			const MlSym *dv;
			const MlSym *v = NULL;

			if (liate_rank (u, x) == rank)
			{
				SymPtrVec_init (&others);

				/* a lone factor is u itself, dv is 1 */
				for (uint64_t k = 0; t->kind == ML_SYM_MUL && k < count; k++)
				{
					if (k != i
					    && SymPtrVec_push (&others, t->kids [k]) != ML_OK)
					{
						ctx->status = ML_ERR_NOMEM;
					}
				}

				dv = ml_sym_product (ctx, others.data, others.len);
				SymPtrVec_free (&others);

				v = solve (ctx, dv, x);

				if (v != NULL && ctx->status == ML_OK)
				{
					*uv   = ml_sym_mul (ctx, u, v);
					*rest = ml_sym_expand (
						ctx,
						ml_sym_mul (ctx, v, ml_sym_diff (ctx, u, x))
					);
					found = 1;
				}
			}
		}
	}

	return found;
}

static const MlSym *parts_with (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlRule      *rule,
	IntSolver   solve
)
{
	const MlSym *result = NULL;
	const MlSym *acc    = int_node (ctx, 0);
	const MlSym *self   = int_node (ctx, 0);
	PartsVec    work;
	int32_t     ok      = 1;
	uint64_t    steps   = 0;
	int32_t     first   = 1;
	MlStepLog   *saved  = ctx->log;

	PartsVec_init (&work);
	push_item (ctx, &work, int_node (ctx, 1), f);
	*rule    = ML_RULE_NONE;
	ctx->log = NULL;

	while (work.len > 0 && ok && ctx->status == ML_OK)
	{
		PartsItem   item = work.data [work.len - 1];
		const MlSym *c   = NULL;
		const MlSym *t   = NULL;
		const MlSym *ratio;
		const MlSym *big;

		work.len--;
		ml_int_split (ctx, item.term, x, &c, &t);
		c     = ml_sym_mul (ctx, item.coef, c);
		ratio = ml_sym_div (ctx, t, f);

		if (!first && ratio->kind == ML_SYM_NUM)
		{
			/* the integral we started with came back: I = A + k I */
			self = ml_sym_add (ctx, self, ml_sym_mul (ctx, c, ratio));
		}

		else
		{
			big = solve (ctx, t, x);

			if (big != NULL)
			{
				acc = ml_sym_add (ctx, acc, ml_sym_mul (ctx, c, big));
			}

			else if (steps < MAX_PARTS)
			{
				const MlSym *uv   = NULL;
				const MlSym *rest = NULL;

				steps++;
				ok = split_parts (ctx, t, x, &uv, &rest, solve);

				if (ok)
				{
					uint64_t n = ml_choose (
						rest->kind == ML_SYM_ADD,
						rest->count,
						1
					);

					acc = ml_sym_add (ctx, acc, ml_sym_mul (ctx, c, uv));

					for (uint64_t i = 0; i < n; i++)
					{
						push_item (
							ctx,
							&work,
							ml_sym_neg (ctx, c),
							ml_choose (
								rest->kind == ML_SYM_ADD,
								rest->kids [i],
								rest
							)
						);
					}
				}
			}

			else
			{
				ok = 0;
			}
		}

		first = 0;
	}

	ctx->log = saved;

	if (ok && ctx->status == ML_OK)
	{
		const MlSym *denominator = ml_sym_sub (ctx, int_node (ctx, 1), self);

		if (!ml_sym_is_zero (denominator))
		{
			result = ml_sym_div (ctx, acc, denominator);
			*rule  = ML_RULE_INT_PARTS;
		}
	}

	if (result != NULL && !ml_int_verify (ctx, result, f, x))
	{
		result = NULL;
	}

	PartsVec_free (&work);
	return result;
}

/* The table, then integration by parts with the table alone. */
static const MlSym *with_parts (
	MlSymCtx    *ctx,
	const MlSym *g,
	const MlSym *x
)
{
	MlRule      rule = ML_RULE_NONE;
	const MlSym *big = ml_int_simple (ctx, g, x);

	if (big == NULL)
	{
		big = parts_with (ctx, g, x, &rule, ml_int_simple);
	}

	return big;
}

/* The table, then a substitution with the table alone. */
static const MlSym *with_subst (
	MlSymCtx    *ctx,
	const MlSym *g,
	const MlSym *x
)
{
	MlRule      rule = ML_RULE_NONE;
	const MlSym *big = ml_int_simple (ctx, g, x);

	if (big == NULL)
	{
		big = subst_with (ctx, g, x, &rule, ml_int_simple);
	}

	return big;
}

const MlSym *ml_int_subst (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlRule      *rule
)
{
	return subst_with (ctx, f, x, rule, with_parts);
}

const MlSym *ml_int_parts (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlRule      *rule
)
{
	return parts_with (ctx, f, x, rule, with_subst);
}
