#include <matlog/docs/stdlib.h>

#include <matlog/math/eval.h>

#define TAG "m:algebra "

/* Defines the function or the variable of one line of a library file. */
static MlResult define_line (MlEnv *env, MlArena *arena, MlSlice line)
{
	MlResult      r = ML_OK;
	MlParseResult p = ml_parse (line);

	if (p.error.code != ML_PARSE_OK)
	{
		r = ML_ERR_PARSE;
	}

	else
	{
		MlValue v = ml_eval (
			arena,
			env,
			ml_env_snapshot (env),
			line,
			&p.ast
		);

		r = ml_eval_commit (env, line, &p.ast, v);
		ml_ast_free (&p.ast);
	}

	return r;
}

MlResult ml_stdlib_load (MlEnv *env)
{
	MlResult r = ML_OK;
	MlArena  arena;

	ml_arena_init (&arena, 0);

	for (uint64_t i = 0; i < ml_stdlib_source_count && r == ML_OK; i++)
	{
		MlSlice  text = ml_slice_cstr (ml_stdlib_sources [i].text);
		uint64_t pos  = 0;

		while (pos < text.len && r == ML_OK)
		{
			uint64_t end = pos;
			MlSlice  line;

			while (end < text.len && text.data [end] != '\n')
			{
				end++;
			}

			line = ml_slice_sub (text, pos, end - pos);
			pos  = end + 1;

			/* notes and blank lines have nothing to define */
			if (
				   line.len > ml_count (TAG) - 1
				&& ml_slice_equal (
					ml_slice_sub (line, 0, ml_count (TAG) - 1),
					ml_slice_cstr (TAG)
				)
			)
			{
				r = define_line (
					env,
					&arena,
					ml_slice_sub (
						line,
						ml_count (TAG) - 1,
						line.len - (ml_count (TAG) - 1)
					)
				);
			}
		}
	}

	env->library = ml_env_snapshot (env);
	ml_arena_free (&arena);

	return r;
}
