#include <matlog/docs/stdlib.h>

#include <matlog/math/eval.h>
#include <matlog/session/results.h>
#include <matlog/session/session.h>

/* Defines the function or the variable of one line of a library file. */
static MlResult define_line (
	MlEnv    *env,
	MlArena  *arena,
	MlDomain domain,
	MlSlice  line
)
{
	MlResult      r = ML_OK;
	MlParseResult p = ml_parse_mode (
		line,
		ml_domain_units (domain),
		ml_domain_pga (domain)
	);

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

/*
 * A line of a library file is `m:<domain> <math>` (defines something) or
 * `a:<domain> <math>` (an assert, see ml_stdlib_check). The tag letter
 * is 'm' or 'a' or 0 for a note; domain and body are filled for a tag.
 */
typedef struct LibLine
{
	char     tag;
	MlDomain domain;
	MlSlice  body;
} LibLine;

static LibLine lib_line (MlSlice line)
{
	LibLine l = {
		.tag    = 0,
		.domain = ML_DOMAIN_ALGEBRA,
		.body   = ml_slice ("", 0)
	};

	if (
		   line.len > 2
		&& (line.data [0] == 'm' || line.data [0] == 'a')
		&& line.data [1] == ':'
	)
	{
		uint64_t k = 2;

		while (k < line.len && line.data [k] != ' ')
		{
			k++;
		}

		if (k < line.len)
		{
			MlDomainResult d = ml_domain_from_name (
				ml_slice_sub (line, 2, k - 2)
			);

			if (d.result == ML_OK)
			{
				l.tag    = line.data [0];
				l.domain = d.domain;
				l.body   = ml_slice_sub (line, k + 1, line.len - k - 1);
			}
		}
	}

	return l;
}

/* The next line of text from pos; pos moves past its newline. */
static MlSlice next_line (MlSlice text, uint64_t *pos)
{
	uint64_t end = *pos;
	MlSlice  line;

	while (end < text.len && text.data [end] != '\n')
	{
		end++;
	}

	line = ml_slice_sub (text, *pos, end - *pos);
	*pos = end + 1;
	return line;
}

MlResult ml_stdlib_load (MlEnv *env)
{
	MlResult    r     = ML_OK;
	MlArena     *arena = ml_arena_app ();
	MlArenaMark mark  = ml_arena_mark (arena);

	for (uint64_t i = 0; i < ml_stdlib_source_count && r == ML_OK; i++)
	{
		MlSlice  text = ml_slice_cstr (ml_stdlib_sources [i].text);
		uint64_t pos  = 0;

		while (pos < text.len && r == ML_OK)
		{
			LibLine l = lib_line (next_line (text, &pos));

			if (l.tag == 'm')
			{
				r = define_line (env, arena, l.domain, l.body);
			}
		}
	}

	env->library = ml_env_snapshot (env);
	ml_arena_rewind (arena, mark);

	return r;
}

/* Splits an assert body at " => ": the expression and the text it gives. */
static MlSlice assert_want (MlSlice body, MlSlice *expr)
{
	MlSlice want = ml_slice_cstr ("true");

	*expr = body;

	for (uint64_t k = 0; k + 4 <= body.len; k++)
	{
		if (
			   want.len == 4
			&& ml_slice_equal (
				ml_slice_sub (body, k, 4),
				ml_slice_cstr (" => ")
			)
		)
		{
			*expr = ml_slice_sub (body, 0, k);
			want  = ml_slice_sub (body, k + 4, body.len - k - 4);
			k     = body.len;
		}
	}

	return want;
}

MlResult ml_stdlib_check (MlStr *report, uint64_t *problems)
{
	MlResult  r = ML_OK;
	MlSession s;
	MlResults res;

	*problems = 0;
	ml_session_init (&s);
	ml_results_init (&res);
	res.free_symbols     = 1;
	res.env.free_symbols = 1;

	/* the asserts of every file, as lines of one session */
	for (uint64_t i = 0; i < ml_stdlib_source_count && r == ML_OK; i++)
	{
		MlSlice  text = ml_slice_cstr (ml_stdlib_sources [i].text);
		uint64_t pos  = 0;

		while (pos < text.len && r == ML_OK)
		{
			LibLine l = lib_line (next_line (text, &pos));
			MlSlice expr;

			if (l.tag == 'a')
			{
				assert_want (l.body, &expr);
				r = ml_session_append (&s, ML_LINE_MATH, l.domain, expr);
			}
		}
	}

	if (r == ML_OK)
	{
		r = ml_results_update (&res, &s, 0);
	}

	/* the same lines again, to pair each with the text it must give */
	uint64_t line = 0;

	for (uint64_t i = 0; i < ml_stdlib_source_count && r == ML_OK; i++)
	{
		MlSlice  text = ml_slice_cstr (ml_stdlib_sources [i].text);
		uint64_t pos  = 0;

		while (pos < text.len && r == ML_OK)
		{
			LibLine l = lib_line (next_line (text, &pos));

			if (l.tag == 'a')
			{
				MlSlice      expr;
				MlSlice      want = assert_want (l.body, &expr);
				MlResultView v    = ml_results_get (&res, line);
				MlStr        got;

				ml_str_init (&got);
				r = ml_choose (
					v.kind == ML_RESULT_ERROR,
					ml_str_append (&got, ml_slice_cstr ("! ")),
					ML_OK
				);
				r = ml_choose (r == ML_OK, ml_str_append (&got, v.text), r);

				if (r == ML_OK && !ml_slice_equal (ml_str_view (got), want))
				{
					r = ml_str_appendf (
						report,
						"%s: `%.*s` gives `%.*s`, expected `%.*s`\n",
						ml_stdlib_sources [i].name,
						(int32_t)expr.len,
						expr.data,
						(int32_t)got.len,
						got.data,
						(int32_t)want.len,
						want.data
					);
					(*problems)++;
				}

				ml_str_free (&got);
				line++;
			}
		}
	}

	ml_results_free (&res);
	ml_session_free (&s);
	return r;
}
