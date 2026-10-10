#include <matlog/session/results.h>

#include <matlog/docs/stdlib.h>

#include <matlog/core/mem.h>
#include <matlog/logic/bool.h>
#include <matlog/logic/lambda.h>
#include <matlog/math/eval.h>
#include <matlog/platform/fs.h>
#include <matlog/platform/path.h>
#include <matlog/session/mlog.h>

/* A file being run: the session itself, or a file it includes. */
typedef struct Frame
{
	MlSession own;  /* the included file; unused for the top frame */
	MlStr     path; /* where relative includes of the file start */
	uint64_t  next; /* the next line to run */
	int32_t   top;  /* the session given to ml_results_update */
} Frame;

ML_VEC_DEFINE (FrameVec, Frame)

void ml_results_init (MlResults *r)
{
	ml_env_init (&r->env);
	ml_stdlib_load (&r->env);
	r->free_symbols = 0;
	MlLogicDefVec_init (&r->defs);
	MlLineResultVec_init (&r->lines);
}

/* Frees the lines from idx on and drops them. */
static void drop_from (MlResults *r, uint64_t idx)
{
	for (uint64_t i = idx; i < r->lines.len; i++)
	{
		ml_str_free (&r->lines.data [i].text);
		ml_str_free (&r->lines.data [i].block);
		ml_plot_owned_free (&r->lines.data [i].plot);
	}

	if (idx < r->lines.len)
	{
		r->lines.len = idx;
	}
}

void ml_results_free (MlResults *r)
{
	drop_from (r, 0);
	MlLineResultVec_free (&r->lines);
	ml_logic_defs_free (&r->defs);
	ml_env_free (&r->env);
}

MlResultView ml_results_get (const MlResults *r, uint64_t idx)
{
	MlResultView view = {
		.kind      = ML_RESULT_NONE,
		.text = {
			.data = "",
			.len  = 0
		},
		.block = {
			.data = "",
			.len  = 0
		},
		.code = {
			.data = "",
			.len  = 0
		},
		.plot      = NULL,
		.value     = ML_VALUE_NONE,
		.err_start = 0,
		.err_len   = 0,
		.snapshot  = 0
	};

	if (idx < r->lines.len)
	{
		MlLineResult lr = r->lines.data [idx];

		view.kind      = lr.kind;
		view.text      = ml_str_view (lr.text);
		view.block     = ml_str_view (lr.block);
		view.plot      = ml_choose (
			lr.plot.kind != ML_PLOT_NONE,
			&r->lines.data [idx].plot,
			NULL
		);
		view.code      = ml_slice_cstr (lr.code);
		view.value     = lr.value;
		view.err_start = lr.err_start;
		view.err_len   = lr.err_len;
		view.snapshot  = lr.snapshot;
	}

	return view;
}

static void frame_free (Frame *f)
{
	if (!f->top)
	{
		ml_session_free (&f->own);
	}

	ml_str_free (&f->path);
}

static MlLineResult result_make (uint64_t snapshot, uint64_t logic)
{
	MlLineResult lr = {
		.kind      = ML_RESULT_NONE,
		.text = {
			.data = NULL,
			.len  = 0,
			.cap  = 0
		},
		.block = {
			.data = NULL,
			.len  = 0,
			.cap  = 0
		},
		.code      = "",
		.value     = ML_VALUE_NONE,
		.err_start = 0,
		.err_len   = 0,
		.snapshot  = snapshot,
		.logic     = logic
	};

	ml_plot_owned_init (&lr.plot);

	return lr;
}

/* Makes lr an error with the given message. */
static MlResult result_error (
	MlLineResult *lr,
	MlSlice      message,
	const char   *code,
	uint64_t     start,
	uint64_t     len
)
{
	MlResult r = ml_str_append (&lr->text, message);

	if (r == ML_OK)
	{
		lr->kind      = ML_RESULT_ERROR;
		lr->code      = code;
		lr->err_start = start;
		lr->err_len   = len;
	}

	return r;
}

/* The text of an evaluated line: the value, or the message of the error. */
static MlResult result_from_value (
	MlLineResult *lr,
	MlArena      *arena,
	MlValue      v,
	MlSlice      source
)
{
	MlResult r = ML_OK;

	if (v.kind == ML_VALUE_ERROR)
	{
		r = ml_error_message (&lr->text, v, source);

		if (r == ML_OK)
		{
			lr->kind      = ML_RESULT_ERROR;
			lr->code      = ml_error_code (v.error);
			lr->err_start = v.error_start;
			lr->err_len   = v.error_len;
		}
	}

	else if (v.kind != ML_VALUE_NONE)
	{
		r = ml_value_format (&lr->text, arena, v);

		if (r == ML_OK && v.kind == ML_VALUE_MATRIX)
		{
			r = ml_mat_typeset (&lr->block, v.matrix);
		}

		if (r == ML_OK && v.kind == ML_VALUE_PLOT)
		{
			r = ml_plot_owned_set (&lr->plot, v.plot);
		}

		if (r == ML_OK)
		{
			lr->kind  = ML_RESULT_VALUE;
			lr->value = v.kind;
		}
	}

	return r;
}

/* 1 when the text is empty or only spaces and tabs. */
static int32_t is_blank (MlSlice text)
{
	int32_t blank = 1;

	for (uint64_t i = 0; i < text.len; i++)
	{
		blank = blank && (text.data [i] == ' ' || text.data [i] == '\t');
	}

	return blank;
}

/*
 * Runs one math line of the algebra domain: its definitions are kept in the
 * environment, and lr gets the result.
 */
static MlResult run_math (
	MlResults    *res,
	MlArena      *arena,
	MlLineResult *lr,
	MlSlice      source
)
{
	MlResult      r    = ML_OK;
	MlParseResult p    = ml_parse (source);
	MlArenaMark   mark = ml_arena_mark (arena);

	if (is_blank (source))
	{
		/* an empty line has no result */
		ml_ast_free (&p.ast);
	}

	else if (p.error.code != ML_PARSE_OK)
	{
		r = result_error (
			lr,
			ml_slice_cstr (ml_parse_error_text (p.error.code)),
			ml_parse_code_name (p.error.code),
			p.error.start,
			p.error.len
		);
	}

	else
	{
		MlValue v = ml_eval (
			arena,
			&res->env,
			ml_env_snapshot (&res->env),
			source,
			&p.ast
		);

		r = ml_eval_commit (&res->env, source, &p.ast, v);

		if (r == ML_OK)
		{
			r = result_from_value (lr, arena, v, source);
		}

		ml_ast_free (&p.ast);
	}

	ml_arena_rewind (arena, mark);
	return r;
}

/* Runs a line of the boolean or the lambda domain; lr gets the result. */
static MlResult run_logic (
	MlResults    *res,
	MlLineResult *lr,
	MlSlice      source,
	MlDomain     domain
)
{
	MlResult   r = ML_OK;
	MlLogicOut o;

	ml_logic_out_init (&o);
	r = ml_choose (
		domain == ML_DOMAIN_BOOLEAN,
		ml_bool_run (&res->defs, res->defs.len, source, &o),
		ml_lambda_run (&res->defs, res->defs.len, source, &o)
	);

	if (r == ML_OK && o.error != ML_LOGIC_OK)
	{
		r = result_error (
			lr,
			ml_str_view (o.text),
			ml_logic_error_code (o.error),
			o.err_start,
			o.err_len
		);
	}

	else if (r == ML_OK && o.value)
	{
		r = ml_str_append (&lr->text, ml_str_view (o.text));

		if (r == ML_OK)
		{
			r = ml_str_append (&lr->block, ml_str_view (o.block));
		}

		if (r == ML_OK)
		{
			lr->kind  = ML_RESULT_VALUE;
			lr->value = ML_VALUE_EXACT;
		}
	}

	if (r == ML_OK && o.def_name.len > 0)
	{
		r = ml_logic_defs_add (
			&res->defs,
			(int32_t)domain,
			ml_str_view (o.def_name),
			ml_str_view (o.def_text)
		);
	}

	ml_logic_out_free (&o);
	return r;
}

/* 1 when a frame on the stack is the file at path. */
static int32_t is_running (const FrameVec *st, MlSlice path)
{
	int32_t found = 0;

	for (uint64_t i = 0; i < st->len; i++)
	{
		found = (
			   found
			|| ml_slice_equal (ml_str_view (st->data [i].path), path)
		);
	}

	return found;
}

/*
 * Starts running the included file `name`: pushes a frame for it, or says in
 * *err why not. ML_ERR_NOMEM is the only failure.
 */
static MlResult include_enter (
	FrameVec    *st,
	MlSlice     name,
	MlErrorKind *err
)
{
	MlSlice  dir = ml_path_dir (ml_str_view (st->data [st->len - 1].path));
	MlResult r   = ML_OK;
	Frame    f;
	MlStr    text;

	memset (&f, 0, sizeof f);
	ml_str_init (&f.path);
	ml_str_init (&text);
	ml_session_init (&f.own);
	*err = ML_EVAL_NONE;

	if (st->len >= ML_RESULTS_MAX_INCLUDES)
	{
		*err = ML_EVAL_DEPTH;
	}

	if (r == ML_OK && *err == ML_EVAL_NONE)
	{
		r = ml_path_join (&f.path, dir, name);
	}

	if (r == ML_OK && *err == ML_EVAL_NONE)
	{
		if (is_running (st, ml_str_view (f.path)))
		{
			*err = ML_EVAL_INCLUDE_LOOP;
		}

		else if (ml_fs_read_file (&text, ml_str_view (f.path)) != ML_OK)
		{
			*err = ML_EVAL_NO_FILE;
		}
	}

	if (r == ML_OK && *err == ML_EVAL_NONE)
	{
		r = ml_mlog_parse (&f.own, ml_str_view (text));
	}

	if (r == ML_OK && *err == ML_EVAL_NONE)
	{
		r = FrameVec_push (st, f);
	}

	if (r != ML_OK || *err != ML_EVAL_NONE)
	{
		frame_free (&f);
	}

	ml_str_free (&text);
	return r;
}

/* Runs the line `view` of the top of the stack; the line result goes to lr. */
static MlResult run_line (
	MlResults    *res,
	FrameVec     *st,
	MlArena      *arena,
	MlLineResult *lr,
	MlLineView   view
)
{
	MlResult r = ML_OK;

	if (view.kind == ML_LINE_MATH && view.domain == ML_DOMAIN_ALGEBRA)
	{
		r = run_math (res, arena, lr, view.text);
	}

	else if (
		   view.kind == ML_LINE_MATH
		&& (
			   view.domain == ML_DOMAIN_BOOLEAN
			|| view.domain == ML_DOMAIN_LAMBDA
		)
	)
	{
		r = run_logic (res, lr, view.text, view.domain);
	}

	else if (view.kind == ML_LINE_INCLUDE)
	{
		MlErrorKind err;

		r = include_enter (st, view.text, &err);

		if (r == ML_OK && err != ML_EVAL_NONE)
		{
			r = result_error (
				lr,
				ml_slice_cstr (ml_error_text (err)),
				ml_error_code (err),
				0,
				view.text.len
			);
		}
	}

	return r;
}

/* Runs the top session from line `from` on, including what it includes. */
static MlResult run_all (MlResults *res, const MlSession *top, uint64_t from)
{
	MlResult    r           = ML_OK;
	FrameVec    st;
	MlArena     *arena      = ml_arena_app ();
	MlArenaMark mark        = ml_arena_mark (arena);
	Frame       first;
	uint64_t    pending_len = 0;

	FrameVec_init (&st);
	memset (&first, 0, sizeof first);
	ml_str_init (&first.path);
	first.top  = 1;
	first.next = from;
	r          = ml_str_append (&first.path, ml_str_view (top->path));

	if (r == ML_OK)
	{
		r = FrameVec_push (&st, first);
	}

	else
	{
		ml_str_free (&first.path);
	}

	while (r == ML_OK && st.len > 0)
	{
		uint64_t         fi     = st.len - 1;
		int32_t          is_top = st.data [fi].top;
		const MlSession *cur    = ml_choose (
			is_top,
			top,
			&st.data [fi].own
		);

		if (st.data [fi].next >= cur->lines.len)
		{
			frame_free (&st.data [fi]);
			st.len--;
		}

		else
		{
			MlLineView   view = ml_session_get (cur, st.data [fi].next);
			MlLineResult lr   = result_make (
				ml_env_snapshot (&res->env),
				res->defs.len
			);

			st.data [fi].next++;
			r = run_line (res, &st, arena, &lr, view);

			if (r == ML_OK && is_top)
			{
				pending_len = view.text.len;
				r           = MlLineResultVec_push (&res->lines, lr);
			}

			else if (
				   r == ML_OK
				&& lr.kind == ML_RESULT_ERROR
				&& view.kind == ML_LINE_INCLUDE
				&& res->lines.len > 0
				&& res->lines.data [res->lines.len - 1].kind == ML_RESULT_NONE
			)
			{
				/* a failed include inside an included file is reported on
				   the include line of the session that started it */
				r = result_error (
					&res->lines.data [res->lines.len - 1],
					ml_str_view (lr.text),
					lr.code,
					0,
					pending_len
				);
			}

			if (r != ML_OK || !is_top)
			{
				ml_str_free (&lr.text);
				ml_str_free (&lr.block);
				ml_plot_owned_free (&lr.plot);
			}
		}
	}

	for (uint64_t i = 0; i < st.len; i++)
	{
		frame_free (&st.data [i]);
	}

	FrameVec_free (&st);
	ml_arena_rewind (arena, mark);
	return r;
}

MlResult ml_results_update (MlResults *r, const MlSession *s, uint64_t from)
{
	uint64_t keep = ml_choose (
		from < r->lines.len,
		from,
		r->lines.len
	);

	if (keep < r->lines.len)
	{
		ml_env_truncate (&r->env, r->lines.data [keep].snapshot);
		ml_logic_defs_truncate (&r->defs, r->lines.data [keep].logic);
	}

	if (keep == 0)
	{
		/* starting over also gives back the numbers of the old variables */
		ml_env_free (&r->env);
		ml_env_init (&r->env);
		ml_stdlib_load (&r->env);
		r->env.free_symbols = r->free_symbols;
		ml_logic_defs_truncate (&r->defs, 0);
	}

	drop_from (r, keep);
	return run_all (r, s, keep);
}
