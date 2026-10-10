#include <meth/session/results.h>
#include <meth/session/steps.h>

#include <meth/docs/stdlib.h>

#include <meth/core/mem.h>
#include <meth/logic/bool.h>
#include <meth/logic/lambda.h>
#include <meth/math/eval.h>
#include <meth/platform/fs.h>
#include <meth/platform/path.h>
#include <meth/proof/proof.h>
#include <meth/session/mlog.h>

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
		ml_str_free (&r->lines.data [i].steps);
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
		.steps = {
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
		view.steps     = ml_str_view (lr.steps);
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
		.steps = {
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
/*
 * 1 when the line is fine with units: a line of the algebra domain that fails
 * only because a unit name is a name there gets the error "units belong to
 * the physics domain". Nothing is kept.
 */
static int32_t units_work (MlResults *res, MlArena *arena, MlSlice source)
{
	MlParseResult p  = ml_parse_units (source, 1);
	int32_t       ok = 0;

	if (p.error.code == ML_PARSE_OK && p.ast.root != ML_NODE_NIL_INDEX)
	{
		MlValue v = ml_eval (
			arena,
			&res->env,
			ml_env_snapshot (&res->env),
			source,
			&p.ast
		);

		ok = v.kind != ML_VALUE_ERROR && v.kind != ML_VALUE_NONE;
	}

	ml_ast_free (&p.ast);
	return ok;
}

static MlResult run_math (
	MlResults    *res,
	MlArena      *arena,
	MlLineResult *lr,
	MlSlice      source,
	MlDomain     domain
)
{
	int32_t       units = ml_domain_units (domain);
	MlResult      r     = ML_OK;
	MlParseResult p     = ml_parse_mode (source, units, ml_domain_pga (domain));
	MlArenaMark   mark  = ml_arena_mark (arena);

	if (is_blank (source))
	{
		/* an empty line has no result */
		ml_ast_free (&p.ast);
	}

	else if (
		   p.error.code != ML_PARSE_OK
		&& domain == ML_DOMAIN_ALGEBRA
		&& units_work (res, arena, source)
	)
	{
		r = result_error (
			lr,
			ml_slice_cstr (ml_error_text (ML_EVAL_UNITS_IN_ALGEBRA)),
			ml_error_code (ML_EVAL_UNITS_IN_ALGEBRA),
			0,
			source.len
		);
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

		if (
			   domain == ML_DOMAIN_ALGEBRA
			&& v.kind == ML_VALUE_ERROR
			&& v.error == ML_EVAL_UNDEFINED_NAME
			&& units_work (res, arena, source)
		)
		{
			v = ml_value_error (
				ML_EVAL_UNITS_IN_ALGEBRA,
				v.error_start,
				v.error_len
			);
		}

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

	if (view.kind == ML_LINE_MATH && ml_domain_is_math (view.domain))
	{
		r = run_math (res, arena, lr, view.text, view.domain);
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

/* 1 when the line is a line of the proof domain. */
static int32_t is_proof_line (MlLineView view)
{
	return view.kind == ML_LINE_MATH && view.domain == ML_DOMAIN_PROOF;
}

/* The mark and the note of a checked line as the result of its line. */
static MlResult proof_result (MlLineResult *lr, const MlProofLine *pl)
{
	MlResult r = ML_OK;

	switch (pl->mark)
	{
		case ML_PROOF_VALID:
		{
			r = ml_str_append (&lr->text, ml_slice_cstr ("\xE2\x9C\x93 "));
			r = ml_choose (
				r == ML_OK,
				ml_str_append (&lr->text, ml_str_view (pl->note)),
				r
			);
			lr->kind  = ML_RESULT_VALUE;
			lr->value = ML_VALUE_EXACT;
		} break;

		case ML_PROOF_UNCHECKED:
		case ML_PROOF_SORRY:
		{
			r = ml_str_append (&lr->text, ml_slice_cstr ("? "));
			r = ml_choose (
				r == ML_OK,
				ml_str_append (&lr->text, ml_str_view (pl->note)),
				r
			);
			lr->kind  = ML_RESULT_VALUE;
			lr->value = ML_VALUE_EXACT;
		} break;

		case ML_PROOF_INVALID:
		case ML_PROOF_SYNTAX:
		{
			r = result_error (
				lr,
				ml_str_view (pl->note),
				ml_choose (
					pl->mark == ML_PROOF_INVALID,
					"proof_invalid",
					"proof_syntax"
				),
				0,
				0
			);
		} break;

		default:
		{
			r = ML_OK;
		} break;
	}

	return r;
}

/*
 * Checks the lines of the proof domain as one proof (the state of a theorem
 * runs from line to line) and gives the lines from `keep` on their verdicts.
 */
static MlResult run_proofs (MlResults *res, const MlSession *s, uint64_t keep)
{
	MlResult      r     = ML_OK;
	MlStr         text;
	MlProofReport rep;
	int32_t       any   = 0;

	ml_str_init (&text);
	ml_proof_report_init (&rep);

	for (uint64_t i = 0; i < s->lines.len && r == ML_OK; i++)
	{
		MlLineView view = ml_session_get (s, i);

		if (is_proof_line (view))
		{
			any = 1;
			r   = ml_str_append (&text, view.text);
		}

		r = ml_choose (
			r == ML_OK,
			ml_str_append (&text, ml_slice_cstr ("\n")),
			r
		);
	}

	if (r == ML_OK && any)
	{
		r = ml_proof_check (&rep, ml_str_view (text));
	}

	for (uint64_t i = keep; any && i < res->lines.len && r == ML_OK; i++)
	{
		if (is_proof_line (ml_session_get (s, i)) && i < rep.lines.len)
		{
			r = proof_result (&res->lines.data [i], &rep.lines.data [i]);
		}
	}

	ml_proof_report_free (&rep);
	ml_str_free (&text);
	return r;
}

MlResult ml_results_update (MlResults *r, const MlSession *s, uint64_t from)
{
	MlResult res;
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
	res = run_all (r, s, keep);

	if (res == ML_OK)
	{
		res = run_proofs (r, s, keep);
	}

	/* a line of steps (...) keeps its steps with its result */
	for (uint64_t i = keep; i < r->lines.len && res == ML_OK; i++)
	{
		if (ml_line_wants_steps (s, i))
		{
			res = ml_line_steps (&r->lines.data [i].steps, r, s, i).result;
		}
	}

	return res;
}
