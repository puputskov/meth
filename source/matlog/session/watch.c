#include <matlog/session/watch.h>

#include <matlog/core/mem.h>

void ml_watch_rows_free (MlWatchRowVec *rows)
{
	for (uint64_t i = 0; i < rows->len; i++)
	{
		ml_str_free (&rows->data [i].name);
		ml_str_free (&rows->data [i].type);
		ml_str_free (&rows->data [i].value);
	}

	MlWatchRowVec_free (rows);
}

/* The type of a variable, as the watch shows it. */
static MlResult type_of (MlStr *out, MlValue v)
{
	MlResult r = ML_OK;

	if (v.kind == ML_VALUE_MATRIX && v.matrix.cols == 1)
	{
		r = ml_str_appendf (out, "vector %u", (uint32_t)v.matrix.rows);
	}

	else if (v.kind == ML_VALUE_MATRIX)
	{
		r = ml_str_appendf (
			out,
			"matrix %ux%u",
			(uint32_t)v.matrix.rows,
			(uint32_t)v.matrix.cols
		);
	}

	else if (v.kind == ML_VALUE_APPROX)
	{
		r = ml_str_append (out, ml_slice_cstr ("approx"));
	}

	else if (v.kind == ML_VALUE_EXPR)
	{
		r = ml_str_append (out, ml_slice_cstr ("expr"));
	}

	else if (v.kind == ML_VALUE_QUANTITY)
	{
		r = ml_str_append (out, ml_slice_cstr ("quantity"));
	}

	else if (v.kind == ML_VALUE_COMPLEX)
	{
		r = ml_str_append (out, ml_slice_cstr ("complex"));
	}

	else if (v.kind == ML_VALUE_QUAT)
	{
		r = ml_str_append (out, ml_slice_cstr ("quaternion"));
	}

	else
	{
		r = ml_str_append (out, ml_slice_cstr ("exact"));
	}

	return r;
}

/* The type of a function: its name and parameters, f(x, y). */
static MlResult function_type (MlStr *out, MlBinding b)
{
	MlResult r = ml_str_append (out, b.name);

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr (" ("));
	}

	for (uint64_t i = 0; i < ml_binding_param_count (b) && r == ML_OK; i++)
	{
		if (i > 0)
		{
			r = ml_str_append (out, ml_slice_cstr (", "));
		}

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_binding_param (b, i));
		}
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr (")"));
	}

	return r;
}

/* Adds a row (the strings are copied). */
static MlResult push_row (
	MlWatchRowVec *out,
	MlSlice       name,
	MlSlice       type,
	MlSlice       value,
	int32_t       fresh
)
{
	MlResult   r;
	MlWatchRow row;

	ml_str_init (&row.name);
	ml_str_init (&row.type);
	ml_str_init (&row.value);
	row.fresh = fresh;
	r         = ml_str_append (&row.name, name);

	if (r == ML_OK)
	{
		r = ml_str_append (&row.type, type);
	}

	if (r == ML_OK)
	{
		r = ml_str_append (&row.value, value);
	}

	if (r == ML_OK)
	{
		r = MlWatchRowVec_push (out, row);
	}

	if (r != ML_OK)
	{
		ml_str_free (&row.name);
		ml_str_free (&row.type);
		ml_str_free (&row.value);
	}

	return r;
}

/* 1 when no binding after index i (before end) has the same name. */
static int32_t is_newest (const MlEnv *env, uint64_t i, uint64_t end)
{
	int32_t newest = 1;

	for (uint64_t j = i + 1; j < end && newest; j++)
	{
		newest = !ml_slice_equal (
			env->bindings.data [j].name,
			env->bindings.data [i].name
		);
	}

	return newest;
}

static MlResult env_rows (
	MlWatchRowVec   *out,
	const MlResults *res,
	uint64_t        begin,
	uint64_t        end
)
{
	MlResult r     = ML_OK;
	MlArena  arena;

	ml_arena_init (&arena, 0);

	for (uint64_t i = 0; i < end && r == ML_OK; i++)
	{
		MlBinding b = res->env.bindings.data [i];

		if (is_newest (&res->env, i, end))
		{
			MlStr type;
			MlStr value;

			ml_str_init (&type);
			ml_str_init (&value);

			if (b.kind == ML_BIND_FUNC)
			{
				r = function_type (&type, b);
				r = ml_choose (r == ML_OK, ml_str_append (&value, b.source), r);
			}

			else
			{
				r = type_of (&type, b.value);
				r = ml_choose (
					r == ML_OK,
					ml_value_format (&value, &arena, b.value),
					r
				);
			}

			if (r == ML_OK)
			{
				r = push_row (
					out,
					b.name,
					ml_str_view (type),
					ml_str_view (value),
					i >= begin
				);
			}

			ml_str_free (&value);
			ml_str_free (&type);
		}
	}

	ml_arena_free (&arena);
	return r;
}

static int32_t logic_newest (
	const MlLogicDefVec *defs,
	uint64_t            i,
	uint64_t            end
)
{
	int32_t newest = 1;

	for (uint64_t j = i + 1; j < end && newest; j++)
	{
		newest = !(
			   defs->data [j].domain == defs->data [i].domain
			&& ml_slice_equal (
				   ml_str_view (defs->data [j].name),
				   ml_str_view (defs->data [i].name)
			   )
		);
	}

	return newest;
}

static MlResult logic_rows (
	MlWatchRowVec   *out,
	const MlResults *res,
	uint64_t        begin,
	uint64_t        end
)
{
	MlResult r = ML_OK;

	for (uint64_t i = 0; i < end && r == ML_OK; i++)
	{
		MlLogicDef d = res->defs.data [i];

		if (logic_newest (&res->defs, i, end))
		{
			r = push_row (
				out,
				ml_str_view (d.name),
				ml_slice_cstr (ml_domain_name ((MlDomain)d.domain)),
				ml_str_view (d.text),
				i >= begin
			);
		}
	}

	return r;
}

MlResult ml_watch_rows (
	MlWatchRowVec   *out,
	const MlResults *res,
	uint64_t        line
)
{
	MlResult r        = ML_OK;
	uint64_t env_from = ml_choose (
		line < res->lines.len,
		res->lines.data [line].snapshot,
		ml_env_snapshot (&res->env)
	);
	uint64_t env_to   = ml_choose (
		line + 1 < res->lines.len,
		res->lines.data [line + 1].snapshot,
		ml_env_snapshot (&res->env)
	);
	uint64_t log_from = ml_choose (
		line < res->lines.len,
		res->lines.data [line].logic,
		res->defs.len
	);
	uint64_t log_to   = ml_choose (
		line + 1 < res->lines.len,
		res->lines.data [line + 1].logic,
		res->defs.len
	);

	r = env_rows (out, res, env_from, env_to);

	if (r == ML_OK)
	{
		r = logic_rows (out, res, log_from, log_to);
	}

	return r;
}
