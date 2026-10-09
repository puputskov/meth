#include "internal.h"

#include <string.h>

/* What the operations return after a failure, so they stay safe to chain. */
static const MlSym failed_node =
{
	.kind  = ML_SYM_VAR,
	.name  =
	{
		.data = "?",
		.len  = 1
	},
	.kids  = NULL,
	.count = 0
};

const MlSym *ml_sym_failed (void)
{
	return &failed_node;
}

void ml_symctx_init (MlSymCtx *ctx, MlArena *arena)
{
	ctx->arena      = arena;
	ctx->status     = ML_OK;
	ctx->budget     = ML_SYM_BUDGET;
	ctx->log        = NULL;
	ctx->mode       = ML_SYM_REBUILD;
	ctx->subst_var  = NULL;
	ctx->subst_val  = NULL;
	ctx->fail_start = 0;
	ctx->fail_len   = 0;
	ctx->raw        = 0;
}

int32_t ml_symctx_ok (const MlSymCtx *ctx)
{
	return ctx->status == ML_OK;
}

/* A new node, zeroed; NULL (and a status) when there is no room. */
static MlSym *node_new (MlSymCtx *ctx, MlSymKind kind)
{
	MlSym *s = NULL;

	if (ctx->status == ML_OK && ctx->budget == 0)
	{
		ctx->status = ML_ERR_LIMIT;
	}

	if (ctx->status == ML_OK)
	{
		s = ML_ARENA_NEW (ctx->arena, MlSym);

		if (s == NULL)
		{
			ctx->status = ML_ERR_NOMEM;
		}

		else
		{
			ctx->budget--;
			s->kind = kind;
		}
	}

	return s;
}

const MlSym *ml_sym_num (MlSymCtx *ctx, MlRat value)
{
	MlSym *s = NULL;

	if (!ml_rat_ok (value))
	{
		if (ctx->status == ML_OK)
		{
			ctx->status = ml_rat_status (value);
		}
	}

	else
	{
		s = node_new (ctx, ML_SYM_NUM);
	}

	if (s != NULL)
	{
		/* the digits belong to the context, not to whoever made the number */
		s->num = ml_rat_copy (ctx->arena, value);
	}

	return ml_choose (s != NULL, s, &failed_node);
}

const MlSym *ml_sym_int (MlSymCtx *ctx, int64_t value)
{
	return ml_sym_num (ctx, ml_rat_from_i64 (ctx->arena, value));
}

/* A copy of text in the arena, or NULL (and a status). */
static MlSlice name_copy (MlSymCtx *ctx, MlSlice name)
{
	char    *copy = ml_arena_strdup (ctx->arena, name);
	MlSlice out   = ml_slice ("", 0);

	if (copy == NULL)
	{
		ctx->status = ML_ERR_NOMEM;
	}

	else
	{
		out = ml_slice (copy, name.len);
	}

	return out;
}

const MlSym *ml_sym_var (MlSymCtx *ctx, MlSlice name)
{
	MlSym *s = node_new (ctx, ML_SYM_VAR);

	if (s != NULL)
	{
		s->name = name_copy (ctx, name);
	}

	return ml_choose (s != NULL && ctx->status == ML_OK, s, &failed_node);
}

const MlSym *ml_sym_raw (
	MlSymCtx           *ctx,
	MlSymKind          kind,
	const MlSym *const *kids,
	uint64_t           count
)
{
	MlSym *s = node_new (ctx, kind);

	if (s != NULL)
	{
		const MlSym **copy = ML_ARENA_NEW_ARRAY (
			ctx->arena,
			const MlSym *,
			ml_choose (count > 0, count, 1)
		);

		if (copy == NULL)
		{
			ctx->status = ML_ERR_NOMEM;
		}

		else
		{
			for (uint64_t i = 0; i < count; i++)
			{
				copy [i] = kids [i];
			}

			s->kids  = (const MlSym *const *)copy;
			s->count = count;
		}
	}

	return ml_choose (s != NULL && ctx->status == ML_OK, s, &failed_node);
}

const MlSym *ml_sym_func (MlSymCtx *ctx, MlSlice name, const MlSym *arg)
{
	const MlSym *kids [1] = { arg };
	const MlSym *s        = ml_sym_raw (ctx, ML_SYM_FUNC, kids, 1);

	if (ctx->status == ML_OK)
	{
		((MlSym *)s)->name = name_copy (ctx, name);
	}

	return ml_choose (ctx->status == ML_OK, s, &failed_node);
}

const MlSym *ml_sym_func_n (
	MlSymCtx           *ctx,
	MlSlice            name,
	const MlSym *const *kids,
	uint64_t           count
)
{
	const MlSym *s = ml_sym_raw (ctx, ML_SYM_FUNC, kids, count);

	if (ctx->status == ML_OK)
	{
		((MlSym *)s)->name = name_copy (ctx, name);
	}

	return ml_choose (ctx->status == ML_OK, s, &failed_node);
}

int32_t ml_sym_is_set (const MlSym *s)
{
	return (
		   s->kind == ML_SYM_FUNC
		&& ml_slice_equal (s->name, ml_slice_cstr ("set"))
	);
}

int32_t ml_sym_is_num (const MlSym *s)
{
	return s->kind == ML_SYM_NUM;
}

int32_t ml_sym_is_zero (const MlSym *s)
{
	return s->kind == ML_SYM_NUM && ml_rat_is_zero (s->num);
}

int32_t ml_sym_is_one (const MlSym *s)
{
	return (
		   s->kind == ML_SYM_NUM
		&& ml_rat_is_integer (s->num)
		&& s->num.num.len == 1
		&& s->num.num.limbs [0] == 1
		&& !s->num.num.negative
	);
}

int32_t ml_sym_is_var (const MlSym *s, MlSlice name)
{
	return s->kind == ML_SYM_VAR && ml_slice_equal (s->name, name);
}

int32_t ml_sym_to_i64 (const MlSym *s, int64_t *value)
{
	int32_t ok = s->kind == ML_SYM_NUM && ml_rat_is_integer (s->num);

	*value = 0;

	if (ok)
	{
		MlBig       mag = s->num.num;
		MlU64Result u;

		mag.negative = 0;
		u            = ml_big_to_u64 (mag);
		ok           = u.ok && u.value <= (uint64_t)INT64_MAX;

		if (ok)
		{
			*value = ml_choose (
				s->num.num.negative,
				-(int64_t)u.value,
				(int64_t)u.value
			);
		}
	}

	return ok;
}

/* ---- comparison ---- */

typedef struct Pair
{
	const MlSym *a;
	const MlSym *b;
} Pair;

ML_VEC_DEFINE (PairVec, Pair)

static int32_t compare_names (MlSlice a, MlSlice b)
{
	uint64_t n      = ml_choose (a.len < b.len, a.len, b.len);
	int32_t  result = ml_choose (n > 0, memcmp (a.data, b.data, n), 0);

	result = ml_choose (
		result != 0,
		ml_choose (result < 0, -1, 1),
		ml_choose (a.len == b.len, 0, ml_choose (a.len < b.len, -1, 1))
	);
	return result;
}

int32_t ml_sym_compare (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	int32_t result = 0;
	PairVec st;

	PairVec_init (&st);
	Pair first = { .a = a, .b = b };

	if (PairVec_push (&st, first) != ML_OK)
	{
		ctx->status = ML_ERR_NOMEM;
		st.len      = 0;
	}

	while (result == 0 && st.len > 0)
	{
		Pair p = st.data [st.len - 1];

		st.len--;

		if (p.a == p.b)
		{
			result = 0;
		}

		else if (p.a->kind != p.b->kind)
		{
			result = ml_choose (p.a->kind < p.b->kind, -1, 1);
		}

		else if (p.a->kind == ML_SYM_NUM)
		{
			result = ml_rat_cmp (ctx->arena, p.a->num, p.b->num);
		}

		else
		{
			if (p.a->kind == ML_SYM_VAR || p.a->kind == ML_SYM_FUNC)
			{
				result = compare_names (p.a->name, p.b->name);
			}

			if (result == 0 && p.a->count != p.b->count)
			{
				result = ml_choose (p.a->count < p.b->count, -1, 1);
			}

			for (uint64_t i = p.a->count; result == 0 && i > 0; i--)
			{
				Pair child =
				{
					.a = p.a->kids [i - 1],
					.b = p.b->kids [i - 1]
				};

				if (PairVec_push (&st, child) != ML_OK)
				{
					ctx->status = ML_ERR_NOMEM;
					st.len      = 0;
				}
			}
		}
	}

	PairVec_free (&st);
	return result;
}

int32_t ml_sym_equal (MlSymCtx *ctx, const MlSym *a, const MlSym *b)
{
	return ml_sym_compare (ctx, a, b) == 0;
}

int32_t ml_sym_has_var (const MlSym *s, MlSlice name)
{
	int32_t   found = 0;
	SymPtrVec st;

	SymPtrVec_init (&st);

	if (SymPtrVec_push (&st, s) != ML_OK)
	{
		st.len = 0;
	}

	while (!found && st.len > 0)
	{
		const MlSym *n = st.data [st.len - 1];

		st.len--;
		found = n->kind == ML_SYM_VAR && ml_slice_equal (n->name, name);

		for (uint64_t i = 0; i < n->count && !found; i++)
		{
			if (SymPtrVec_push (&st, n->kids [i]) != ML_OK)
			{
				found = 1; /* cannot say: assume it is there */
			}
		}
	}

	SymPtrVec_free (&st);
	return found;
}

/* ---- the generic bottom up walk ---- */

typedef struct MapFrame
{
	const MlSym *node;
	uint64_t    next;  /* the next kid to visit */
	uint64_t    base;  /* index in the results where the kids' results start */
} MapFrame;

ML_VEC_DEFINE (MapFrameVec, MapFrame)

const MlSym *ml_sym_map (MlSymCtx *ctx, const MlSym *s)
{
	MapFrameVec frames;
	SymPtrVec   results;
	const MlSym *out = &failed_node;
	MlResult    r;

	MapFrameVec_init (&frames);
	SymPtrVec_init (&results);

	MapFrame root =
	{
		.node = s,
		.next = 0,
		.base = 0
	};

	r = MapFrameVec_push (&frames, root);

	while (r == ML_OK && frames.len > 0 && ctx->status == ML_OK)
	{
		MapFrame *f = &frames.data [frames.len - 1];

		if (f->next < f->node->count)
		{
			MapFrame child =
			{
				.node = f->node->kids [f->next],
				.next = 0,
				.base = results.len
			};

			f->next++;
			r = MapFrameVec_push (&frames, child);
		}

		else
		{
			MapFrame done = *f;
			const MlSym *made = ml_sym_rewrite (
				ctx,
				done.node,
				ml_choose (
					results.len > done.base,
					(const MlSym *const *)&results.data [done.base],
					NULL
				),
				results.len - done.base
			);

			results.len = done.base;
			frames.len--;
			r = SymPtrVec_push (&results, made);
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

	MapFrameVec_free (&frames);
	SymPtrVec_free (&results);
	return out;
}
