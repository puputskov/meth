#include <meth/proof/rule.h>

#include <meth/math/env.h>
#include <meth/parse/parser.h>
#include <meth/parse/words.h>
#include <meth/texts/texts.h>

#include <string.h>

#define MAX_BINDINGS 8

/* What a symbol of the rule stands for in the formula that is matched. */
typedef struct Binding
{
	MlSlice     name;
	const MlSym *value;
} Binding;

typedef struct Pair
{
	const MlSym *pattern;
	const MlSym *tree;
} Pair;

ML_VEC_DEFINE (PairVec, Pair)

/* The names the symbols of a rule are given while a formula is built. */
static const char *const fresh_names [MAX_BINDINGS] = {
	"#0", "#1", "#2", "#3", "#4", "#5", "#6", "#7"
};

/* 1 for a symbol of a rule: a name that is no constant of the syntax. */
static int32_t is_meta (const MlSym *p)
{
	return p->kind == ML_SYM_VAR && ml_word_find (p->name).kind == ML_WORD_NONE;
}

/* The place of a lone = outside brackets, s.len when there is none. */
static uint64_t find_equals (MlSlice s)
{
	uint64_t found = s.len;
	int64_t  depth = 0;

	for (uint64_t i = 0; i < s.len && found == s.len; i++)
	{
		char c = s.data [i];

		depth += ml_choose (c == '(' || c == '[', 1, 0);
		depth -= ml_choose (c == ')' || c == ']', 1, 0);

		if (depth == 0 && c == '=')
		{
			found = i;
		}
	}

	return found;
}

/* The formula of the text, as written (raw 1) or in canonical form. */
static const MlSym *build (
	MlSymCtx *ctx,
	MlEnv    *env,
	MlSlice  text,
	int32_t  raw
)
{
	MlParseResult p = ml_parse (text);
	const MlSym   *s = NULL;

	if (p.error.code == ML_PARSE_OK)
	{
		ctx->raw = raw;
		s        = ml_sym_from_ast (
			ctx,
			env,
			0,
			text,
			&p.ast,
			p.ast.root,
			ml_slice ("", 0)
		);
		ctx->raw = 0;
		s        = ml_choose (ml_symctx_ok (ctx), s, NULL);
		ml_ast_free (&p.ast);
	}

	return s;
}

typedef struct Frame
{
	const MlSym *node;
	uint64_t    next;   /* the next kid to visit */
	const MlSym **out;  /* what the kids became */
} Frame;

ML_VEC_DEFINE (FrameVec, Frame)

/* A node of the kind of `node` over the new kids; sums and products absorb
   the sums and products among them. */
static const MlSym *rebuild (
	MlSymCtx      *ctx,
	const MlSym   *node,
	const MlSym   **kids
)
{
	const MlSym *made  = node;
	uint64_t     total = 0;

	for (uint64_t i = 0; i < node->count; i++)
	{
		total += ml_choose (
			(node->kind == ML_SYM_ADD || node->kind == ML_SYM_MUL)
			&& kids [i]->kind == node->kind,
			kids [i]->count,
			1
		);
	}

	if (node->kind == ML_SYM_ADD || node->kind == ML_SYM_MUL)
	{
		const MlSym **all = ML_ARENA_NEW_ARRAY (
			ctx->arena,
			const MlSym *,
			total
		);
		uint64_t      at  = 0;

		for (uint64_t i = 0; all != NULL && i < node->count; i++)
		{
			if (kids [i]->kind == node->kind)
			{
				for (uint64_t k = 0; k < kids [i]->count; k++)
				{
					all [at++] = kids [i]->kids [k];
				}
			}

			else
			{
				all [at++] = kids [i];
			}
		}

		made = ml_choose (
			all != NULL,
			ml_sym_raw (ctx, node->kind, (const MlSym *const *)all, total),
			node
		);
	}

	else if (node->kind == ML_SYM_FUNC)
	{
		made = ml_sym_func_n (
			ctx,
			node->name,
			(const MlSym *const *)kids,
			node->count
		);
	}

	else
	{
		made = ml_sym_raw (
			ctx,
			node->kind,
			(const MlSym *const *)kids,
			node->count
		);
	}

	return made;
}

/*
 * The tree with nested sums (and products) joined into one: (a + b) + c as
 * the sum a, b, c. A step is written the way the formula reads, and the parser
 * of the tool nests to the left.
 */
static const MlSym *flatten (MlSymCtx *ctx, const MlSym *root)
{
	const MlSym *value = root;
	FrameVec    stack;
	Frame       first = { .node = root, .next = 0, .out = NULL };
	int32_t     ok    = 1;

	FrameVec_init (&stack);
	first.out = ML_ARENA_NEW_ARRAY (ctx->arena, const MlSym *, root->count + 1);
	ok        = first.out != NULL && FrameVec_push (&stack, first) == ML_OK;

	while (ok && stack.len > 0)
	{
		uint64_t top = stack.len - 1;
		Frame    f   = stack.data [top];

		if (f.next < f.node->count)
		{
			const MlSym *kid   = f.node->kids [f.next];
			Frame        child = { .node = kid, .next = 0, .out = NULL };

			stack.data [top].next++;
			child.out = ML_ARENA_NEW_ARRAY (
				ctx->arena,
				const MlSym *,
				kid->count + 1
			);
			ok = child.out != NULL && FrameVec_push (&stack, child) == ML_OK;
		}

		else
		{
			const MlSym *made = ml_choose (
				f.node->count == 0,
				f.node,
				rebuild (ctx, f.node, f.out)
			);

			FrameVec_pop (&stack);

			if (stack.len > 0)
			{
				Frame *parent = &stack.data [stack.len - 1];

				parent->out [parent->next - 1] = made;
			}

			else
			{
				value = made;
			}
		}
	}

	FrameVec_free (&stack);
	return ml_choose (ok, value, root);
}

/* 1 when both formulas have the same expanded form. */
static int32_t same_formula (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	const MlSym *ea = ml_sym_expand (ctx, a);
	const MlSym *eb = ml_sym_expand (ctx, b);

	return ml_symctx_ok (ctx) && ml_sym_equal (ctx, ea, eb);
}

/* The kids of a sum or product from index `from`, n of them, as one node. */
static const MlSym *middle (
	MlSymCtx    *ctx,
	const MlSym *node,
	uint64_t    from,
	uint64_t    n
)
{
	return ml_choose (
		n == 1,
		node->kids [from],
		ml_sym_raw (ctx, node->kind, node->kids + from, n)
	);
}

/*
 * Walks the two trees together while exactly one of the parts differs; the
 * pair where they stop is the place that was rewritten.
 */
static void find_change (
	MlSymCtx    *ctx,
	const MlSym *l,
	const MlSym *r,
	const MlSym **s,
	const MlSym **t
)
{
	int32_t going = 1;

	while (going)
	{
		uint64_t differing = 0;
		uint64_t at        = 0;
		int32_t  shape     = (
			   l->kind == r->kind
			&& l->count == r->count
			&& l->kind != ML_SYM_NUM
			&& l->kind != ML_SYM_VAR
			&& (l->kind != ML_SYM_FUNC || ml_slice_equal (l->name, r->name))
		);

		for (uint64_t k = 0; shape && k < l->count; k++)
		{
			if (!ml_sym_equal (ctx, l->kids [k], r->kids [k]))
			{
				differing++;
				at = k;
			}
		}

		if (shape && differing == 1)
		{
			l = l->kids [at];
			r = r->kids [at];
		}

		else if (
			   l->kind == r->kind
			&& (l->kind == ML_SYM_ADD || l->kind == ML_SYM_MUL)
			&& l->count != r->count
		)
		{
			/* terms were split or joined: keep what differs in the middle */
			uint64_t least = ml_choose (
				l->count < r->count,
				l->count,
				r->count
			);
			uint64_t head  = 0;
			uint64_t tail  = 0;

			while (
				   head < least
				&& ml_sym_equal (ctx, l->kids [head], r->kids [head])
			)
			{
				head++;
			}

			while (
				   tail + head < least
				&& ml_sym_equal (
					ctx,
					l->kids [l->count - 1 - tail],
					r->kids [r->count - 1 - tail]
				)
			)
			{
				tail++;
			}

			if (head + tail < l->count && head + tail < r->count)
			{
				const MlSym *nl = middle (ctx, l, head, l->count - head - tail);
				const MlSym *nr = middle (ctx, r, head, r->count - head - tail);

				l = nl;
				r = nr;
			}

			going = 0;
		}

		else
		{
			going = 0;
		}
	}

	*s = l;
	*t = r;
}

/*
 * 1 when the tree is the pattern for some formulas bound to its symbols; the
 * walk keeps its pairs on a vector, not on the C stack.
 */
static int32_t match (
	MlSymCtx    *ctx,
	const MlSym *pattern,
	const MlSym *tree,
	Binding     *b,
	uint64_t    *nb
)
{
	int32_t ok = 1;
	PairVec stack;
	Pair    first = { .pattern = pattern, .tree = tree };

	PairVec_init (&stack);
	ok = PairVec_push (&stack, first) == ML_OK;

	while (ok && stack.len > 0)
	{
		Pair cur = PairVec_get (&stack, stack.len - 1);

		PairVec_pop (&stack);

		if (is_meta (cur.pattern))
		{
			int32_t bound = 0;

			for (uint64_t i = 0; i < *nb; i++)
			{
				if (ml_slice_equal (b [i].name, cur.pattern->name))
				{
					bound = 1;
					ok    = ok && ml_sym_equal (ctx, b [i].value, cur.tree);
				}
			}

			if (!bound && *nb < MAX_BINDINGS)
			{
				b [*nb].name  = cur.pattern->name;
				b [*nb].value = cur.tree;
				(*nb)++;
			}

			else if (!bound)
			{
				ok = 0;
			}
		}

		else if (
			   cur.pattern->kind == ML_SYM_NUM
			|| cur.pattern->kind == ML_SYM_VAR
		)
		{
			ok = ml_sym_equal (ctx, cur.pattern, cur.tree);
		}

		else
		{
			ok = (
				   cur.pattern->kind == cur.tree->kind
				&& cur.pattern->count == cur.tree->count
				&& (
					   cur.pattern->kind != ML_SYM_FUNC
					|| ml_slice_equal (cur.pattern->name, cur.tree->name)
				)
			);

			for (uint64_t k = 0; ok && k < cur.pattern->count; k++)
			{
				Pair kid = {
					.pattern = cur.pattern->kids [k],
					.tree    = cur.tree->kids [k]
				};

				ok = PairVec_push (&stack, kid) == ML_OK;
			}
		}
	}

	PairVec_free (&stack);
	return ok;
}

/*
 * The formula with the symbols of the rule replaced by what they stand for.
 * They go through names no user can write first, so that a formula that
 * contains a symbol of the rule is not replaced again.
 */
static const MlSym *instantiate (
	MlSymCtx    *ctx,
	const MlSym *form,
	const Binding *b,
	uint64_t    nb
)
{
	const MlSym *t = form;

	for (uint64_t i = 0; i < nb; i++)
	{
		t = ml_sym_substitute (
			ctx,
			t,
			ml_sym_var (ctx, b [i].name),
			ml_sym_var (ctx, ml_slice_cstr (fresh_names [i]))
		);
	}

	for (uint64_t i = 0; i < nb; i++)
	{
		t = ml_sym_substitute (
			ctx,
			t,
			ml_sym_var (ctx, ml_slice_cstr (fresh_names [i])),
			b [i].value
		);
	}

	return t;
}

MlProofRuleResult ml_proof_rule_check (
	MlSymCtx    *ctx,
	MlSlice     name,
	const MlSym *left,
	const MlSym *right
)
{
	MlProofRuleResult res  = ML_PROOF_RULE_UNKNOWN;
	MlStr             key;
	const char        *rule = NULL;

	ml_str_init (&key);

	if (ml_str_append (&key, ml_slice_cstr ("rules.")) == ML_OK)
	{
		if (ml_str_append (&key, name) == ML_OK)
		{
			rule = ml_text_find (ml_str_cstr (key));
		}
	}

	if (rule != NULL)
	{
		MlSlice  text = ml_slice_cstr (rule);
		uint64_t at   = find_equals (text);
		MlEnv    env;
		MlSlice  lt;
		MlSlice  rt;
		const MlSym *raw [2] = { NULL, NULL };
		const MlSym *can [2] = { NULL, NULL };

		res = ML_PROOF_RULE_NO_FIT;
		ml_env_init (&env);
		ctx->status = ML_OK;
		ctx->budget = ML_SYM_BUDGET;
		lt          = ml_slice_sub (text, 0, at);
		rt          = ml_slice ("", 0);

		if (at < text.len)
		{
			rt      = ml_slice_sub (text, at + 1, text.len - at - 1);
			raw [0] = build (ctx, &env, lt, 1);
			raw [1] = build (ctx, &env, rt, 1);
			raw [0] = ml_choose (raw [0] != NULL, flatten (ctx, raw [0]), NULL);
			raw [1] = ml_choose (raw [1] != NULL, flatten (ctx, raw [1]), NULL);
			can [0] = build (ctx, &env, lt, 0);
			can [1] = build (ctx, &env, rt, 0);
		}

		if (raw [0] && raw [1] && can [0] && can [1])
		{
			const MlSym *s = NULL;
			const MlSym *t = NULL;

			find_change (
				ctx,
				flatten (ctx, left),
				flatten (ctx, right),
				&s,
				&t
			);

			for (uint64_t o = 0; o < 2 && res != ML_PROOF_RULE_FITS; o++)
			{
				Binding  b [MAX_BINDINGS];
				uint64_t nb = 0;

				if (match (ctx, raw [o], s, b, &nb))
				{
					const MlSym *inst = instantiate (ctx, can [1 - o], b, nb);

					if (same_formula (ctx, t, inst))
					{
						res = ML_PROOF_RULE_FITS;
					}
				}
			}
		}

		ml_env_free (&env);
		ctx->status = ML_OK;
	}

	ml_str_free (&key);
	return res;
}
