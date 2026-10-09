#include "internal.h"

#include <matlog/math/env.h>

#include <string.h>

/*
 * The syntax tree of a statement to a symbolic expression. The walk follows
 * the links of the tree (first child, next sibling, parent) and keeps the
 * result of each node in a table, so it needs no stack of its own, except for
 * a call of a user defined function: its body is converted in a scope of its
 * own, on a stack on the heap, with the arguments as the values of its
 * parameters. No function calls itself.
 */

#define MAX_SCOPES 64

typedef struct Scope
{
	MlSlice            source;
	MlAst              ast;      /* a view of the tree */
	uint64_t           root;
	const MlSym        **results; /* one per node of the tree */
	uint64_t           cur;
	int32_t            down;
	MlBinding          func;     /* the running function, none on the line */
	const MlSym *const *args;    /* its arguments */
	uint64_t           call;     /* the call node in the scope below */
	int32_t            inherited; /* errors are reported at the first call */
	uint64_t           rep_start;
	uint64_t           rep_len;
} Scope;

ML_VEC_DEFINE (ScopeVec, Scope)

typedef struct Build
{
	MlSymCtx           *ctx;
	const struct MlEnv *env;
	uint64_t           snapshot;
	MlSlice            free;
	ScopeVec           scopes;
} Build;

static int32_t is_name (MlSlice name, const char *text)
{
	return ml_slice_equal (name, ml_slice_cstr (text));
}

/* A node that cannot be a formula: the status and the place. */
static const MlSym *fail_at (Build *b, const Scope *sc, MlNode node)
{
	if (b->ctx->status == ML_OK)
	{
		b->ctx->status     = ML_ERR_INVALID;
		b->ctx->fail_start = ml_choose (sc->inherited, sc->rep_start, node.start);
		b->ctx->fail_len   = ml_choose (sc->inherited, sc->rep_len, node.len);
	}

	return ml_sym_failed ();
}

/* The value of a name: parameter, the free symbol, a variable, or a symbol. */
static const MlSym *name_value (Build *b, const Scope *sc, MlNode node, MlSlice name)
{
	const MlSym *out   = NULL;
	uint64_t    params = ml_binding_param_count (sc->func);

	for (uint64_t i = 0; i < params && out == NULL; i++)
	{
		if (ml_slice_equal (ml_binding_param (sc->func, i), name))
		{
			out = sc->args [i];
		}
	}

	if (out == NULL && !sc->inherited && sc->func.kind == ML_BIND_NONE
		&& ml_slice_equal (name, b->free))
	{
		out = ml_sym_var (b->ctx, name);
	}

	if (out == NULL)
	{
		MlBinding bound = ml_env_lookup (b->env, name, b->snapshot);

		if (bound.kind == ML_BIND_VAR && bound.value.kind == ML_VALUE_EXACT)
		{
			out = ml_sym_num (b->ctx, bound.value.exact);
		}

		else if (bound.kind == ML_BIND_VAR && bound.value.kind == ML_VALUE_EXPR)
		{
			out = bound.value.sym;
		}

		else if (bound.kind != ML_BIND_NONE)
		{
			out = fail_at (b, sc, node);
		}
	}

	if (out == NULL)
	{
		out = ml_sym_var (
			b->ctx,
			ml_choose (
				is_name (name, "\xCF\x80"),
				ml_slice_cstr ("pi"),
				name
			)
		);
	}

	return out;
}

/* n! for a small whole number n. */
static const MlSym *factorial_of (Build *b, const Scope *sc, MlNode node, const MlSym *a)
{
	int64_t     n   = 0;
	const MlSym *out = NULL;

	if (!ml_sym_to_i64 (a, &n) || n < 0 || n > 170)
	{
		out = fail_at (b, sc, node);
	}

	else
	{
		MlRat acc = ml_rat_from_i64 (b->ctx->arena, 1);

		for (int64_t i = 2; i <= n; i++)
		{
			acc = ml_rat_mul (b->ctx->arena, acc, ml_rat_from_i64 (b->ctx->arena, i));
		}

		out = ml_sym_num (b->ctx, acc);
	}

	return out;
}

static int32_t is_function_name (MlSlice name)
{
	return (
		   is_name (name, "sin") || is_name (name, "cos")
		|| is_name (name, "tan") || is_name (name, "asin")
		|| is_name (name, "acos") || is_name (name, "atan")
		|| is_name (name, "ln") || is_name (name, "exp")
		|| is_name (name, "abs")
	);
}

/* The value of the child n of a node. */
static const MlSym *kid (const Scope *sc, uint64_t node, uint64_t n)
{
	return sc->results [ml_ast_child (&sc->ast, node, n)];
}

/* A call whose callee is a name: a function, a user function or a product. */
static const MlSym *call_value (Build *b, Scope *sc, MlNode node, int32_t *pushed)
{
	uint64_t    callee = ml_ast_child (&sc->ast, sc->cur, 0);
	MlSlice     name   = ml_ast_text (&sc->ast, sc->source, callee);
	uint64_t    argc   = ml_ast_child_count (&sc->ast, sc->cur) - 1;
	MlBinding   fn     = ml_env_lookup (b->env, name, b->snapshot);
	int32_t     is_var = (
		   fn.kind == ML_BIND_VAR
		|| (ml_slice_equal (name, b->free) && !sc->inherited)
	);
	const MlSym *out   = NULL;

	/* a parameter called with one argument is a product as well */
	for (uint64_t i = 0; i < ml_binding_param_count (sc->func); i++)
	{
		is_var = is_var || ml_slice_equal (ml_binding_param (sc->func, i), name);
	}

	if (fn.kind == ML_BIND_FUNC)
	{
		if (argc != ml_binding_param_count (fn) || b->scopes.len >= MAX_SCOPES)
		{
			out = fail_at (b, sc, node);
		}

		else
		{
			Scope inner;
			const MlSym **args = ML_ARENA_NEW_ARRAY (
				b->ctx->arena,
				const MlSym *,
				ml_choose (argc > 0, argc, 1)
			);
			const MlSym **table = ML_ARENA_NEW_ARRAY (
				b->ctx->arena,
				const MlSym *,
				fn.ast.nodes.len
			);

			if (args == NULL || table == NULL)
			{
				b->ctx->status = ML_ERR_NOMEM;
				out            = ml_sym_failed ();
			}

			else
			{
				for (uint64_t i = 0; i < argc; i++)
				{
					args [i] = kid (sc, sc->cur, i + 1);
				}

				inner.source    = fn.source;
				inner.ast       = fn.ast;
				inner.root      = fn.body;
				inner.results   = table;
				inner.cur       = fn.body;
				inner.down      = 1;
				inner.func      = fn;
				inner.args      = (const MlSym *const *)args;
				inner.call      = sc->cur;
				inner.inherited = 1;
				inner.rep_start = ml_choose (sc->inherited, sc->rep_start, node.start);
				inner.rep_len   = ml_choose (sc->inherited, sc->rep_len, node.len);

				if (ScopeVec_push (&b->scopes, inner) != ML_OK)
				{
					b->ctx->status = ML_ERR_NOMEM;
					out            = ml_sym_failed ();
				}

				else
				{
					*pushed = 1;
				}
			}
		}
	}

	else if (is_function_name (name) && argc == 1 && fn.kind == ML_BIND_NONE)
	{
		out = ml_sym_call (b->ctx, name, kid (sc, sc->cur, 1));
	}

	else if (is_name (name, "sqrt") && argc == 1 && fn.kind == ML_BIND_NONE)
	{
		out = ml_sym_pow (
			b->ctx,
			kid (sc, sc->cur, 1),
			ml_sym_num (
				b->ctx,
				ml_rat_make (
					b->ctx->arena,
					ml_big_from_u64 (b->ctx->arena, 1),
					ml_big_from_u64 (b->ctx->arena, 2)
				)
			)
		);
	}

	else if (is_name (name, "log") && argc == 1 && fn.kind == ML_BIND_NONE)
	{
		out = ml_sym_div (
			b->ctx,
			ml_sym_call (b->ctx, ml_slice_cstr ("ln"), kid (sc, sc->cur, 1)),
			ml_sym_call (b->ctx, ml_slice_cstr ("ln"), ml_sym_int (b->ctx, 10))
		);
	}

	else if (is_var && argc == 1)
	{
		out = ml_sym_mul (
			b->ctx,
			name_value (b, sc, ml_ast_node (&sc->ast, callee), name),
			kid (sc, sc->cur, 1)
		);
	}

	else
	{
		out = fail_at (b, sc, node);
	}

	return out;
}

/* The value of the node the walk is at, all its kids being done. */
static const MlSym *node_value (Build *b, Scope *sc, int32_t *pushed)
{
	MlNode      node = ml_ast_node (&sc->ast, sc->cur);
	MlSlice     text = ml_ast_text (&sc->ast, sc->source, sc->cur);
	MlSymCtx    *ctx = b->ctx;
	const MlSym *out = NULL;

	if (node.kind == ML_NODE_NUMBER)
	{
		out = ml_sym_num (ctx, ml_rat_parse_decimal (ctx->arena, text));
	}

	else if (node.kind == ML_NODE_SUPERSCRIPT)
	{
		MlStr digits;

		ml_str_init (&digits);

		if (ml_superscript_to_ascii (&digits, text) == ML_OK)
		{
			out = ml_sym_num (
				ctx,
				ml_rat_parse_decimal (ctx->arena, ml_str_view (digits))
			);
		}

		else
		{
			out = fail_at (b, sc, node);
		}

		ml_str_free (&digits);
	}

	else if (
		   node.kind == ML_NODE_NAME
		&& ml_ast_node (&sc->ast, node.parent).kind == ML_NODE_CALL
		&& ml_ast_child (&sc->ast, node.parent, 0) == sc->cur
	)
	{
		/* the name of a function that is called: looked at by the call */
		out = ml_sym_int (ctx, 0);
	}

	else if (node.kind == ML_NODE_NAME)
	{
		out = name_value (b, sc, node, text);
	}

	else if (node.kind == ML_NODE_NEG)
	{
		out = ml_sym_neg (ctx, kid (sc, sc->cur, 0));
	}

	else if (node.kind == ML_NODE_ADD)
	{
		out = ml_sym_add (ctx, kid (sc, sc->cur, 0), kid (sc, sc->cur, 1));
	}

	else if (node.kind == ML_NODE_SUB)
	{
		out = ml_sym_sub (ctx, kid (sc, sc->cur, 0), kid (sc, sc->cur, 1));
	}

	else if (node.kind == ML_NODE_MUL)
	{
		out = ml_sym_mul (ctx, kid (sc, sc->cur, 0), kid (sc, sc->cur, 1));
	}

	else if (node.kind == ML_NODE_DIV)
	{
		out = ml_sym_div (ctx, kid (sc, sc->cur, 0), kid (sc, sc->cur, 1));
	}

	else if (node.kind == ML_NODE_POW)
	{
		out = ml_sym_pow (ctx, kid (sc, sc->cur, 0), kid (sc, sc->cur, 1));
	}

	else if (node.kind == ML_NODE_FACT)
	{
		out = factorial_of (b, sc, node, kid (sc, sc->cur, 0));
	}

	else if (node.kind == ML_NODE_SQRT)
	{
		out = ml_sym_pow (
			ctx,
			kid (sc, sc->cur, 0),
			ml_sym_num (
				ctx,
				ml_rat_make (
					ctx->arena,
					ml_big_from_u64 (ctx->arena, 1),
					ml_big_from_u64 (ctx->arena, 2)
				)
			)
		);
	}

	else if (node.kind == ML_NODE_CALL)
	{
		out = call_value (b, sc, node, pushed);
	}

	else
	{
		out = fail_at (b, sc, node);
	}

	return out;
}

/* Moves the walk of a scope on after the value of its current node is in. */
static void climb (Scope *sc)
{
	MlNode here = ml_ast_node (&sc->ast, sc->cur);

	if (sc->cur == sc->root)
	{
		sc->cur = ML_NODE_NIL_INDEX;
	}

	else if (here.next != ML_NODE_NIL_INDEX)
	{
		sc->cur  = here.next;
		sc->down = 1;
	}

	else
	{
		sc->cur  = here.parent;
		sc->down = 0;
	}
}

const MlSym *ml_sym_from_ast (
	MlSymCtx           *ctx,
	const struct MlEnv *env,
	uint64_t           snapshot,
	MlSlice            source,
	const MlAst        *ast,
	uint64_t           node,
	MlSlice            free
)
{
	Build       b;
	const MlSym *result = ml_sym_failed ();
	Scope       first;

	b.ctx      = ctx;
	b.env      = env;
	b.snapshot = snapshot;
	b.free     = free;
	ScopeVec_init (&b.scopes);

	first.source    = source;
	first.ast       = *ast;
	first.root      = node;
	first.results   = ML_ARENA_NEW_ARRAY (ctx->arena, const MlSym *, ast->nodes.len);
	first.cur       = node;
	first.down      = 1;
	memset (&first.func, 0, sizeof first.func);
	first.args      = NULL;
	first.call      = 0;
	first.inherited = 0;
	first.rep_start = 0;
	first.rep_len   = 0;

	if (first.results == NULL)
	{
		ctx->status = ML_ERR_NOMEM;
	}

	else if (ScopeVec_push (&b.scopes, first) != ML_OK)
	{
		ctx->status = ML_ERR_NOMEM;
	}

	while (ctx->status == ML_OK && b.scopes.len > 0)
	{
		Scope *sc = &b.scopes.data [b.scopes.len - 1];

		if (sc->down && ml_ast_node (&sc->ast, sc->cur).first != ML_NODE_NIL_INDEX)
		{
			sc->cur = ml_ast_node (&sc->ast, sc->cur).first;
		}

		else
		{
			int32_t     pushed = 0;
			const MlSym *v     = node_value (&b, sc, &pushed);

			/* a call of a user function went on in a new scope */
			sc = &b.scopes.data [b.scopes.len - 1 - (pushed ? 1 : 0)];

			if (!pushed)
			{
				sc->results [sc->cur] = v;
				climb (sc);
			}

			/* a finished scope hands its value to the scope below it */
			while (
				   ctx->status == ML_OK
				&& b.scopes.len > 0
				&& b.scopes.data [b.scopes.len - 1].cur == ML_NODE_NIL_INDEX
			)
			{
				Scope done = b.scopes.data [b.scopes.len - 1];
				const MlSym *value = done.results [done.root];

				b.scopes.len--;

				if (b.scopes.len == 0)
				{
					result = value;
				}

				else
				{
					Scope *parent = &b.scopes.data [b.scopes.len - 1];

					parent->results [done.call] = value;
					climb (parent);
				}
			}
		}
	}

	ScopeVec_free (&b.scopes);
	return ml_choose (ctx->status == ML_OK, result, ml_sym_failed ());
}
