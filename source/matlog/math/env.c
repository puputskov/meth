#include <matlog/math/env.h>

void ml_env_init (MlEnv *env)
{
	ml_arena_init (&env->arena, 0);
	MlBindingVec_init (&env->bindings);
}

void ml_env_free (MlEnv *env)
{
	for (uint64_t i = 0; i < env->bindings.len; i++)
	{
		ml_ast_free (&env->bindings.data [i].ast);
	}

	MlBindingVec_free (&env->bindings);
	ml_arena_free (&env->arena);
}

uint64_t ml_env_snapshot (const MlEnv *env)
{
	return env->bindings.len;
}

void ml_env_truncate (MlEnv *env, uint64_t snapshot)
{
	for (uint64_t i = snapshot; i < env->bindings.len; i++)
	{
		ml_ast_free (&env->bindings.data [i].ast);
	}

	if (snapshot < env->bindings.len)
	{
		env->bindings.len = snapshot;
	}
}

/* A copy of x whose limbs live in the arena. */
static MlBig copy_big (MlArena *arena, MlBig x)
{
	MlBig     out = x;
	uint32_t  *p  = NULL;

	if (ml_big_ok (x) && x.len > 0)
	{
		p = (uint32_t *)ml_arena_alloc (
			arena,
			x.len * sizeof (uint32_t),
			_Alignof (uint32_t)
		);

		if (p)
		{
			memcpy (p, x.limbs, x.len * sizeof (uint32_t));
			out.limbs = p;
		}

		else
		{
			out = ml_big_error (ML_ERR_NOMEM);
		}
	}

	return out;
}

static MlRat copy_rat (MlArena *arena, MlRat x)
{
	MlRat out =
	{
		.num = copy_big (arena, x.num),
		.den = copy_big (arena, x.den)
	};

	return out;
}

/* A copy of m whose cells and digits live in the arena. */
static MlMat copy_mat (MlArena *arena, MlMat m)
{
	MlMat out = ml_mat_zero (arena, m.rows, m.cols);

	for (uint64_t i = 0; i < m.rows * m.cols && out.status == ML_OK; i++)
	{
		out.cells [i] = copy_rat (arena, m.cells [i]);

		if (!ml_rat_ok (out.cells [i]))
		{
			out = ml_mat_error (ML_ERR_NOMEM);
		}
	}

	return out;
}

static MlValue copy_value (MlArena *arena, MlValue v)
{
	MlValue out = v;

	if (v.kind == ML_VALUE_EXACT || v.kind == ML_VALUE_QUANTITY)
	{
		out.exact.num = copy_big (arena, v.exact.num);
		out.exact.den = copy_big (arena, v.exact.den);

		if (v.kind == ML_VALUE_QUANTITY && v.show.len > 0)
		{
			out.scale = copy_rat (arena, v.scale);
			out.show  = ml_slice_cstr (ml_arena_strdup (arena, v.show));
		}
	}

	else if (v.kind == ML_VALUE_MATRIX)
	{
		out.matrix = copy_mat (arena, v.matrix);
	}

	else if (v.kind == ML_VALUE_EXPR)
	{
		/* rebuilt into the arena of the environment; already canonical */
		MlSymCtx ctx;

		ml_symctx_init (&ctx, arena);
		out.sym = ml_sym_simplify (&ctx, v.sym);

		if (ctx.status != ML_OK)
		{
			out = ml_value_error (ML_EVAL_NO_MEMORY, 0, 0);
		}
	}

	return out;
}

MlResult ml_env_define_var (MlEnv *env, MlSlice name, MlValue value)
{
	MlResult  r = ML_OK;
	MlBinding b;
	char      *txt = NULL;

	memset (&b, 0, sizeof b);

	if (
		   value.kind != ML_VALUE_EXACT
		&& value.kind != ML_VALUE_APPROX
		&& value.kind != ML_VALUE_MATRIX
		&& value.kind != ML_VALUE_EXPR
		&& value.kind != ML_VALUE_QUANTITY
	)
	{
		r = ML_ERR_INVALID;
	}

	if (r == ML_OK)
	{
		txt     = ml_arena_strdup (&env->arena, name);
		b.value = copy_value (&env->arena, value);

		if (txt == NULL)
		{
			r = ML_ERR_NOMEM;
		}

		if (b.value.kind == ML_VALUE_EXACT && !ml_rat_ok (b.value.exact))
		{
			r = ML_ERR_NOMEM;
		}

		if (b.value.kind == ML_VALUE_MATRIX && !ml_mat_ok (b.value.matrix))
		{
			r = ML_ERR_NOMEM;
		}

		if (b.value.kind == ML_VALUE_ERROR)
		{
			r = ML_ERR_NOMEM;
		}
	}

	if (r == ML_OK)
	{
		b.kind = ML_BIND_VAR;
		b.name = ml_slice (txt, name.len);
		r      = MlBindingVec_push (&env->bindings, b);
	}

	return r;
}

/* A private copy of a tree, so the binding does not depend on the caller's. */
static MlResult copy_ast (MlAst *out, const MlAst *ast)
{
	MlResult r;

	MlNodeVec_init (&out->nodes);
	out->root = ast->root;
	r         = MlNodeVec_reserve (&out->nodes, ast->nodes.len);

	if (r == ML_OK && ast->nodes.len > 0)
	{
		memcpy (
			out->nodes.data,
			ast->nodes.data,
			ast->nodes.len * sizeof (MlNode)
		);
		out->nodes.len = ast->nodes.len;
	}

	return r;
}

MlResult ml_env_define_func (MlEnv *env, MlSlice source, const MlAst *ast)
{
	MlResult  r = ML_OK;
	MlBinding b;
	char      *txt = NULL;

	memset (&b, 0, sizeof b);

	if (ml_ast_node (ast, ast->root).kind != ML_NODE_FUNC_DEF)
	{
		r = ML_ERR_INVALID;
	}

	if (r == ML_OK)
	{
		txt = ml_arena_strdup (&env->arena, source);
		r   = ml_choose (
			txt != NULL,
			copy_ast (&b.ast, ast),
			ML_ERR_NOMEM
		);
	}

	if (r == ML_OK)
	{
		b.kind   = ML_BIND_FUNC;
		b.source = ml_slice (txt, source.len);
		b.name   = ml_ast_text (
			&b.ast,
			b.source,
			ml_ast_child (&b.ast, b.ast.root, 0)
		);
		b.params = ml_ast_child (&b.ast, b.ast.root, 1);
		b.body   = ml_ast_child (&b.ast, b.ast.root, 2);
		r        = MlBindingVec_push (&env->bindings, b);
	}

	if (r != ML_OK)
	{
		ml_ast_free (&b.ast);
	}

	return r;
}

MlBinding ml_env_lookup (const MlEnv *env, MlSlice name, uint64_t snapshot)
{
	MlBinding found;
	uint64_t  i = ml_choose (
		snapshot < env->bindings.len,
		snapshot,
		env->bindings.len
	);

	memset (&found, 0, sizeof found);

	while (i > 0 && found.kind == ML_BIND_NONE)
	{
		MlBinding b = env->bindings.data [i - 1];

		if (ml_slice_equal (b.name, name))
		{
			found = b;
		}

		i--;
	}

	return found;
}

uint64_t ml_binding_param_count (MlBinding b)
{
	return ml_choose (
		b.kind == ML_BIND_FUNC,
		ml_ast_child_count (&b.ast, b.params),
		0
	);
}

MlSlice ml_binding_param (MlBinding b, uint64_t n)
{
	MlSlice s = ml_slice (NULL, 0);

	if (b.kind == ML_BIND_FUNC)
	{
		s = ml_ast_text (
			&b.ast,
			b.source,
			ml_ast_child (&b.ast, b.params, n)
		);
	}

	return s;
}
