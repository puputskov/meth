#include "internal.h"

/*
 * The derivative, computed bottom up: when the derivatives of the kids of a
 * node are known, the rule of the node gives its own. No recursion, the walk
 * has a stack of frames on the heap, like ml_sym_map.
 */

typedef struct DiffFrame
{
	const MlSym *node;
	uint64_t    next;
	uint64_t    base;
} DiffFrame;

ML_VEC_DEFINE (DiffFrameVec, DiffFrame)

static int32_t is_name (MlSlice name, const char *text)
{
	return ml_slice_equal (name, ml_slice_cstr (text));
}

static const MlSym *int_node (MlSymCtx *ctx, int64_t n)
{
	return ml_sym_int (ctx, n);
}

static const MlSym *half (MlSymCtx *ctx)
{
	return ml_sym_num (
		ctx,
		ml_rat_make (
			ctx->arena,
			ml_big_from_u64 (ctx->arena, 1),
			ml_big_from_u64 (ctx->arena, 2)
		)
	);
}

/* The rule of a function: u' times f'(u). */
static const MlSym *function_rule (
	MlSymCtx    *ctx,
	MlSlice     name,
	const MlSym *u,
	const MlSym *du,
	MlRule      *rule
)
{
	const MlSym *outer = NULL;
	MlSlice     fn     = ml_slice_cstr ("");

	*rule = ML_RULE_DIFF_CHAIN;

	if (is_name (name, "sin"))
	{
		outer = ml_sym_call (ctx, ml_slice_cstr ("cos"), u);
	}

	else if (is_name (name, "cos"))
	{
		outer = ml_sym_neg (ctx, ml_sym_call (ctx, ml_slice_cstr ("sin"), u));
	}

	else if (is_name (name, "tan"))
	{
		outer = ml_sym_pow (
			ctx,
			ml_sym_call (ctx, ml_slice_cstr ("cos"), u),
			int_node (ctx, -2)
		);
	}

	else if (is_name (name, "exp"))
	{
		outer = ml_sym_call (ctx, ml_slice_cstr ("exp"), u);
	}

	else if (is_name (name, "ln"))
	{
		outer = ml_sym_pow (ctx, u, int_node (ctx, -1));
	}

	else if (is_name (name, "abs"))
	{
		/* u / |u| */
		outer = ml_sym_div (
			ctx,
			u,
			ml_sym_call (ctx, ml_slice_cstr ("abs"), u)
		);
	}

	else
	{
		/* asin, acos, atan: 1/sqrt(1 - u^2), -1/sqrt(1 - u^2), 1/(1 + u^2) */
		const MlSym *square = ml_sym_pow (ctx, u, int_node (ctx, 2));
		int32_t     inverse = is_name (name, "atan");

		outer = ml_choose (
			inverse,
			ml_sym_pow (
				ctx,
				ml_sym_add (ctx, int_node (ctx, 1), square),
				int_node (ctx, -1)
			),
			ml_sym_pow (
				ctx,
				ml_sym_sub (ctx, int_node (ctx, 1), square),
				ml_sym_neg (ctx, half (ctx))
			)
		);
		outer = ml_choose (
			is_name (name, "acos"),
			ml_sym_neg (ctx, outer),
			outer
		);
	}

	(void)fn;
	return ml_sym_mul (ctx, du, outer);
}

/* The derivative of node, given the derivatives of its kids. */
static const MlSym *rule (
	MlSymCtx           *ctx,
	const MlSym        *n,
	const MlSym *const *d,
	const MlSym        *x,
	MlRule             *name
)
{
	const MlSym *out = int_node (ctx, 0);

	*name = ML_RULE_DIFF_CONSTANT;

	if (n->kind == ML_SYM_VAR)
	{
		out = ml_choose (
			ml_slice_equal (n->name, x->name),
			int_node (ctx, 1),
			out
		);
	}

	else if (n->kind == ML_SYM_ADD)
	{
		*name = ML_RULE_DIFF_SUM;
		out   = ml_sym_sum (ctx, d, n->count);
	}

	else if (n->kind == ML_SYM_MUL)
	{
		SymPtrVec terms;

		*name = ml_choose (
			n->count == 2 && n->kids [0]->kind == ML_SYM_NUM,
			ML_RULE_DIFF_CONSTANT_MULTIPLE,
			ML_RULE_DIFF_PRODUCT
		);
		SymPtrVec_init (&terms);

		for (uint64_t i = 0; i < n->count && ctx->status == ML_OK; i++)
		{
			SymPtrVec factors;

			SymPtrVec_init (&factors);

			for (uint64_t j = 0; j < n->count; j++)
			{
				if (
					SymPtrVec_push (
						&factors,
						ml_choose (j == i, d [j], n->kids [j])
					) != ML_OK
				)
				{
					ctx->status = ML_ERR_NOMEM;
				}
			}

			if (
				SymPtrVec_push (
					&terms,
					ml_sym_product (ctx, factors.data, factors.len)
				) != ML_OK
			)
			{
				ctx->status = ML_ERR_NOMEM;
			}

			SymPtrVec_free (&factors);
		}

		out = ml_sym_sum (ctx, terms.data, terms.len);
		SymPtrVec_free (&terms);
	}

	else if (n->kind == ML_SYM_POW)
	{
		const MlSym *b  = n->kids [0];
		const MlSym *e  = n->kids [1];
		int32_t     in_e = ml_sym_has_var (e, x->name);
		int32_t     in_b = ml_sym_has_var (b, x->name);

		if (!in_e)
		{
			/* e b^(e-1) b' */
			*name = ML_RULE_DIFF_POWER;
			out   = ml_sym_product (
				ctx,
				(const MlSym *[3]){
					e,
					ml_sym_pow (
						ctx,
						b,
						ml_sym_add (ctx, e, int_node (ctx, -1))
					),
					d [0]
				},
				3
			);
		}

		else if (!in_b)
		{
			/* b^e ln(b) e' ; for e (the constant) ln(e) is 1 */
			*name = ML_RULE_DIFF_EXPONENTIAL;
			out   = ml_sym_product (
				ctx,
				(const MlSym *[3]){
					n,
					ml_choose (
						ml_sym_is_var (b, ml_slice_cstr ("e")),
						int_node (ctx, 1),
						ml_sym_call (ctx, ml_slice_cstr ("ln"), b)
					),
					d [1]
				},
				3
			);
		}

		else
		{
			/* b^e (e' ln(b) + e b' / b) */
			*name = ML_RULE_DIFF_GENERAL_POWER;
			out   = ml_sym_mul (
				ctx,
				n,
				ml_sym_add (
					ctx,
					ml_sym_mul (
						ctx,
						d [1],
						ml_sym_call (ctx, ml_slice_cstr ("ln"), b)
					),
					ml_sym_div (ctx, ml_sym_mul (ctx, e, d [0]), b)
				)
			);
		}
	}

	else if (n->kind == ML_SYM_FUNC)
	{
		out = function_rule (ctx, n->name, n->kids [0], d [0], name);
	}

	return out;
}

/* "d/dx f = g  (rule)" into the log. */
static void log_diff (
	MlSymCtx    *ctx,
	const MlSym *n,
	const MlSym *x,
	const MlSym *result,
	MlRule      name
)
{
	if (ctx->log != NULL && ctx->status == ML_OK && ctx->log->status == ML_OK)
	{
		MlStr text;

		ml_str_init (&text);
		ctx->log->status = ml_str_append (&text, ml_slice_cstr ("d/d"));

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_sym_format (&text, x);
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_str_append (&text, ml_slice_cstr (" ("));
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_sym_format (&text, n);
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_str_appendf (&text, ") = ");
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_sym_format (&text, result);
		}

		if (ctx->log->status == ML_OK)
		{
			ctx->log->status = ml_str_appendf (
				&text,
				"   (%s)",
				ml_rule_name (name)
			);
		}

		if (ctx->log->status == ML_OK)
		{
			ml_steplog_line (ctx->log, ml_str_view (text));
		}

		ml_str_free (&text);
	}
}

const MlSym *ml_sym_diff (MlSymCtx *ctx, const MlSym *s, const MlSym *x)
{
	DiffFrameVec frames;
	SymPtrVec    results;
	const MlSym  *out = ml_sym_failed ();
	MlResult     r;
	DiffFrame    root =
	{
		.node = s,
		.next = 0,
		.base = 0
	};

	DiffFrameVec_init (&frames);
	SymPtrVec_init (&results);
	r = DiffFrameVec_push (&frames, root);

	while (r == ML_OK && frames.len > 0 && ctx->status == ML_OK)
	{
		DiffFrame *f = &frames.data [frames.len - 1];

		if (f->next < f->node->count)
		{
			DiffFrame child =
			{
				.node = f->node->kids [f->next],
				.next = 0,
				.base = results.len
			};

			f->next++;
			r = DiffFrameVec_push (&frames, child);
		}

		else
		{
			DiffFrame   done = *f;
			MlRule      name = ML_RULE_NONE;
			const MlSym *d   = rule (
				ctx,
				done.node,
				ml_choose (
					results.len > done.base,
					(const MlSym *const *)&results.data [done.base],
					NULL
				),
				x,
				&name
			);

			if (done.node->count > 0 && !ml_sym_is_zero (d))
			{
				log_diff (ctx, done.node, x, d, name);
			}

			results.len = done.base;
			frames.len--;
			r = SymPtrVec_push (&results, d);
		}
	}

	if (r != ML_OK && ctx->status == ML_OK)
	{
		ctx->status = r;
	}

	if (ctx->status == ML_OK && results.len == 1)
	{
		out = results.data [0];
	}

	DiffFrameVec_free (&frames);
	SymPtrVec_free (&results);
	return out;
}
