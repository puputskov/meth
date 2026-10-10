#include <matlog/ui/ghost.h>

#include <matlog/core/mem.h>
#include <matlog/logic/bool.h>
#include <matlog/logic/lambda.h>
#include <matlog/math/eval.h>

/* The preview of a line of the boolean or the lambda domain. */
static MlGhost ghost_logic (
	MlStr           *out,
	const MlResults *results,
	MlDomain        domain,
	uint64_t        idx,
	MlSlice         text
)
{
	MlGhost g =
	{
		.result = ML_OK,
		.shown  = 0
	};

	MlLogicOut o;
	uint64_t   count = ml_choose (
		idx < results->lines.len,
		results->lines.data [idx].logic,
		results->defs.len
	);

	ml_logic_out_init (&o);
	g.result = ml_choose (
		domain == ML_DOMAIN_BOOLEAN,
		ml_bool_run (&results->defs, count, text, &o),
		ml_lambda_run (&results->defs, count, text, &o)
	);

	if (g.result == ML_OK && o.error == ML_LOGIC_OK && o.value)
	{
		g.result = ml_str_append (out, ml_str_view (o.text));
		g.shown  = g.result == ML_OK;
	}

	ml_logic_out_free (&o);
	return g;
}

/* The preview of a line of the algebra domain. */
static MlGhost ghost_algebra (
	MlStr           *out,
	const MlResults *results,
	uint64_t        idx,
	MlSlice         text
)
{
	MlGhost g =
	{
		.result = ML_OK,
		.shown  = 0
	};

	MlParseResult p = ml_parse (text);
	MlArena       arena;

	ml_arena_init (&arena, 0);

	if (p.error.code == ML_PARSE_OK)
	{
		uint64_t snapshot = ml_choose (
			idx < results->lines.len,
			results->lines.data [idx].snapshot,
			ml_env_snapshot (&results->env)
		);
		MlValue v = ml_eval (
			&arena,
			&results->env,
			snapshot,
			text,
			&p.ast
		);

		if (
			   v.kind == ML_VALUE_EXACT
			|| v.kind == ML_VALUE_APPROX
			|| v.kind == ML_VALUE_MATRIX
			|| v.kind == ML_VALUE_EXPR
			|| v.kind == ML_VALUE_QUANTITY
			|| v.kind == ML_VALUE_COMPLEX
			|| v.kind == ML_VALUE_QUAT
			|| v.kind == ML_VALUE_BOOL
			|| v.kind == ML_VALUE_LIST
		)
		{
			g.result = ml_value_format (out, &arena, v);
			g.shown  = g.result == ML_OK;
		}
	}

	ml_ast_free (&p.ast);
	ml_arena_free (&arena);
	return g;
}

MlGhost ml_ghost (
	MlStr           *out,
	const MlResults *results,
	MlDomain        domain,
	uint64_t        idx,
	MlSlice         text
)
{
	MlGhost g =
	{
		.result = ML_OK,
		.shown  = 0
	};

	if (domain == ML_DOMAIN_BOOLEAN || domain == ML_DOMAIN_LAMBDA)
	{
		g = ghost_logic (out, results, domain, idx, text);
	}

	else
	{
		g = ghost_algebra (out, results, idx, text);
	}

	return g;
}
