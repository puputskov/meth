#include <matlog/ui/ghost.h>

#include <matlog/core/mem.h>
#include <matlog/math/eval.h>

MlGhost ml_ghost (
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
