#include <meth/web/web.h>

#include "internal.h"

#include <meth/core/json.h>
#include <meth/export/export.h>
#include <meth/export/math.h>
#include <meth/session/file.h>
#include <meth/session/steps.h>
#include <meth/texts/texts.h>
#include <meth/ui/complete.h>
#include <meth/ui/ghost.h>
#include <meth/ui/highlight.h>
#include <meth/ui/theme.h>


/* ---- names ------------------------------------------------------------- */

static const char *const kind_names [] = {
	[ML_LINE_NONE]    = "none",
	[ML_LINE_MATH]    = "math",
	[ML_LINE_NOTE]    = "note",
	[ML_LINE_INCLUDE] = "include",
	[ML_LINE_BLANK]   = "blank",
	[ML_LINE_UNKNOWN] = "unknown"
};

static const char *const plot_names [] = {
	[ML_PLOT_NONE]      = "none",
	[ML_PLOT_HISTOGRAM] = "histogram",
	[ML_PLOT_SCATTER]   = "scatter",
	[ML_PLOT_LINE]      = "line"
};

static MlLineKind kind_from_name (MlSlice name)
{
	MlLineKind kind = ML_LINE_NONE;

	for (uint64_t k = 1; k < ml_count (kind_names); k++)
	{
		if (ml_slice_equal (name, ml_slice_cstr (kind_names [k])))
		{
			kind = (MlLineKind)k;
		}
	}

	return kind;
}

/* ---- the answer -------------------------------------------------------- */

static void begin (MlWeb *w)
{
	ml_str_clear (&w->out);
}

MlSlice ml_web_answer (const MlWeb *w)
{
	return ml_str_view (w->out);
}

MlResult ml_web_init (MlWeb *w)
{
	ml_session_init (&w->session);
	ml_results_init (&w->results);
	ml_str_init (&w->out);
	ml_docs_init (&w->docs);
	w->docs_loaded = 0;
	return ML_OK;
}

void ml_web_free (MlWeb *w)
{
	ml_str_free (&w->out);
	ml_docs_free (&w->docs);
	ml_results_free (&w->results);
	ml_session_free (&w->session);
}

/* ---- lines ------------------------------------------------------------- */

static MlResult add_plot (MlResult r, MlStr *o, const MlPlotOwned *p)
{
	r = lit (r, o, "{\"kind\":\"");
	r = lit (r, o, plot_names [p->kind]);
	r = lit (r, o, "\",\"count\":");
	r = num (r, o, p->count);
	r = lit (r, o, ",\"lo\":");
	r = ml_choose (r == ML_OK, ml_json_number (o, p->lo), r);
	r = lit (r, o, ",\"hi\":");
	r = ml_choose (r == ML_OK, ml_json_number (o, p->hi), r);
	r = lit (r, o, ",\"xs\":[");

	for (uint64_t i = 0; i < p->xs.len; i++)
	{
		r = lit (r, o, ml_choose (i > 0, ",", ""));
		r = ml_choose (r == ML_OK, ml_json_number (o, p->xs.data [i]), r);
	}

	r = lit (r, o, "],\"ys\":[");

	for (uint64_t i = 0; i < p->ys.len; i++)
	{
		r = lit (r, o, ml_choose (i > 0, ",", ""));
		r = ml_choose (r == ML_OK, ml_json_number (o, p->ys.data [i]), r);
	}

	return lit (r, o, "]}");
}

/* The steps of line i as items (see stepitems.c), or an empty array. */
static MlResult add_step_items (MlResult r, MlWeb *w, uint64_t i)
{
	MlStr       raw;
	MlLineSteps st = { .result = ML_OK, .found = 0 };

	ml_str_init (&raw);
	st = ml_line_steps_raw (&raw, &w->results, &w->session, i);
	r  = ml_choose (r == ML_OK, st.result, r);
	r  = ml_choose (
		r == ML_OK && st.found,
		web_step_items (&w->out, ml_str_view (raw)),
		lit (r, &w->out, "[]")
	);
	ml_str_free (&raw);
	return r;
}

static MlResult add_result (
	MlResult      r,
	MlWeb         *w,
	uint64_t      i,
	MlLineView    line,
	MlResultView  res
)
{
	MlStr *o = &w->out;

	switch (res.kind)
	{
		case ML_RESULT_VALUE:
		{
			r = lit (r, o, "{\"kind\":\"value\",\"text\":");
			r = str (r, o, res.text);
			r = lit (r, o, ",\"type\":\"");
			r = lit (
				r,
				o,
				ml_choose (
					   line.domain == ML_DOMAIN_BOOLEAN
					|| line.domain == ML_DOMAIN_LAMBDA,
					ml_domain_name (line.domain),
					ml_value_type_name (res.value)
				)
			);
			r = lit (r, o, "\"");

			if (res.block.len > 0)
			{
				r = lit (r, o, ",\"block\":");
				r = rows (r, o, res.block);
			}

			if (res.steps.len > 0)
			{
				r = lit (r, o, ",\"steps\":");
				r = rows (r, o, res.steps);
				r = lit (r, o, ",\"stepItems\":");
				r = add_step_items (r, w, i);
			}

			if (res.plot != NULL && res.plot->kind != ML_PLOT_NONE)
			{
				r = lit (r, o, ",\"plot\":");
				r = add_plot (r, o, res.plot);
			}

			r = lit (r, o, "}");
		} break;

		case ML_RESULT_ERROR:
		{
			r = lit (r, o, "{\"kind\":\"error\",\"text\":");
			r = str (r, o, res.text);
			r = lit (r, o, ",\"code\":");
			r = str (r, o, res.code);
			r = lit (r, o, ",\"start\":");
			r = num (r, o, res.err_start);
			r = lit (r, o, ",\"len\":");
			r = num (r, o, res.err_len);
			r = lit (r, o, "}");
		} break;

		default:
		{
			r = lit (r, o, "{\"kind\":\"none\"}");
		} break;
	}

	return r;
}

static MlResult add_line (MlResult r, MlWeb *w, uint64_t i)
{
	MlLineView line  = ml_session_get (&w->session, i);
	MlBlock    block = ml_session_block (&w->session, i);

	r = lit (r, &w->out, "{\"i\":");
	r = num (r, &w->out, i);
	r = lit (r, &w->out, ",\"kind\":\"");
	r = lit (r, &w->out, kind_names [line.kind]);
	r = lit (r, &w->out, "\"");

	if (line.kind == ML_LINE_MATH)
	{
		r = lit (r, &w->out, ",\"domain\":\"");
		r = lit (r, &w->out, ml_domain_name (line.domain));
		r = lit (r, &w->out, "\"");
	}

	r = lit (r, &w->out, ",\"text\":");
	r = str (r, &w->out, line.text);
	r = lit (r, &w->out, ",\"first\":");
	r = num (r, &w->out, block.first);
	r = lit (r, &w->out, ",\"last\":");
	r = num (r, &w->out, block.last);
	r = lit (r, &w->out, ",\"wants_steps\":");
	r = flag (r, &w->out, ml_line_wants_steps (&w->session, i));
	r = lit (r, &w->out, ",\"result\":");
	r = add_result (r, w, i, line, ml_results_get (&w->results, i));
	return lit (r, &w->out, "}");
}

MlResult ml_web_lines (MlWeb *w, uint64_t from)
{
	MlResult r = ML_OK;
	uint64_t n = w->session.lines.len;

	begin (w);
	from = ml_choose (from < n, from, n);
	r    = lit (r, &w->out, "{\"from\":");
	r    = num (r, &w->out, from);
	r    = lit (r, &w->out, ",\"count\":");
	r    = num (r, &w->out, n);
	r    = lit (r, &w->out, ",\"lines\":[");

	for (uint64_t i = from; i < n; i++)
	{
		r = lit (r, &w->out, ml_choose (i > from, ",", ""));
		r = add_line (r, w, i);
	}

	return lit (r, &w->out, "]}");
}

/* ---- the session ------------------------------------------------------- */

MlResult ml_web_load (MlWeb *w, MlSlice file_text)
{
	MlSession fresh;
	MlResult  r;

	ml_session_init (&fresh);
	r = ml_file_parse (&fresh, file_text);

	if (r == ML_OK)
	{
		ml_session_free (&w->session);
		w->session = fresh;
		ml_session_clear_history (&w->session);
		w->session.dirty = 0;
		r = ml_results_update (&w->results, &w->session, 0);
	}

	else
	{
		ml_session_free (&fresh);
	}

	return ml_choose (r == ML_OK, ml_web_lines (w, 0), r);
}

MlResult ml_web_save (MlWeb *w)
{
	begin (w);
	return ml_file_format (&w->out, &w->session);
}

static MlResult apply (
	MlWeb      *w,
	MlWebEdit  op,
	uint64_t   idx,
	MlLineKind kind,
	MlDomain   domain,
	MlSlice    text,
	int32_t    join
)
{
	MlResult r = ML_OK;

	if (op != ML_WEB_TYPE && !join)
	{
		ml_session_checkpoint (&w->session);
	}

	switch (op)
	{
		case ML_WEB_REPLACE:
		case ML_WEB_TYPE:
		{
			r = ml_session_replace (&w->session, idx, kind, domain, text);
		} break;

		case ML_WEB_INSERT:
		{
			r = ml_session_insert (&w->session, idx, kind, domain, text);
		} break;

		default:
		{
			r = ml_session_delete (&w->session, idx);
		} break;
	}

	return r;
}

MlResult ml_web_edit (
	MlWeb     *w,
	MlWebEdit flags,
	uint64_t  idx,
	MlSlice   kind,
	MlSlice   domain,
	MlSlice   text
)
{
	MlResult       r     = ML_OK;
	int32_t        join  = ((int32_t)flags & (int32_t)ML_WEB_JOIN) != 0;
	MlWebEdit      op    = (MlWebEdit)((int32_t)flags & ~(int32_t)ML_WEB_JOIN);
	MlLineKind     k     = kind_from_name (kind);
	MlDomainResult d     = ml_domain_from_name (domain);
	uint64_t       from  = idx;
	uint64_t       n     = w->session.lines.len;
	int32_t        typed = op == ML_WEB_REPLACE || op == ML_WEB_TYPE;
	int32_t        edit  = typed || op == ML_WEB_INSERT;
	int32_t        line  = typed || op == ML_WEB_DELETE;
	int32_t        bad   = (
		   (edit && k == ML_LINE_NONE)
		|| (edit && k == ML_LINE_MATH && d.result != ML_OK)
		|| (line && idx >= n)
		|| (op == ML_WEB_INSERT && idx > n)
	);

	if (op == ML_WEB_UNDO || op == ML_WEB_REDO)
	{
		MlUndoResult u = ml_choose (
			op == ML_WEB_UNDO,
			ml_session_undo (&w->session),
			ml_session_redo (&w->session)
		);

		r    = u.result;
		from = 0;
	}

	else if (op == ML_WEB_CHECKPOINT)
	{
		ml_session_checkpoint (&w->session);
		from = n;
	}

	else if (bad)
	{
		r = ML_ERR_INVALID;
	}

	else
	{
		r = apply (w, op, idx, k, d.domain, text, join);
	}

	if (r == ML_OK)
	{
		r = ml_results_update (&w->results, &w->session, from);
	}

	return ml_choose (r == ML_OK, ml_web_lines (w, from), r);
}

MlResult ml_web_set_symbols (MlWeb *w, int32_t on)
{
	MlResult r;

	w->results.free_symbols     = on != 0;
	w->results.env.free_symbols = on != 0;
	r = ml_results_update (&w->results, &w->session, 0);
	return ml_choose (r == ML_OK, ml_web_lines (w, 0), r);
}

MlResult ml_web_domains (MlWeb *w)
{
	MlResult r;

	begin (w);
	r = lit (ML_OK, &w->out, "[");

	for (uint64_t d = 0; d < ML_DOMAIN_COUNT; d++)
	{
		r = lit (r, &w->out, ml_choose (d > 0, ",\"", "\""));
		r = lit (r, &w->out, ml_domain_name ((MlDomain)d));
		r = lit (r, &w->out, "\"");
	}

	return lit (r, &w->out, "]");
}

/* ---- the editor's services --------------------------------------------- */

/* The environment a line saw: the snapshot of its result, else everything. */
static uint64_t snapshot_at (const MlWeb *w, uint64_t idx)
{
	return ml_choose (
		idx < w->results.lines.len,
		w->results.lines.data [idx].snapshot,
		ml_env_snapshot (&w->results.env)
	);
}

MlResult ml_web_highlight (
	MlWeb    *w,
	MlSlice  domain,
	uint64_t idx,
	MlSlice  text,
	uint64_t err_start,
	uint64_t err_len
)
{
	MlDomainResult d = ml_domain_from_name (domain);
	MlRunVec       runs;
	MlResult       r = ML_OK;

	begin (w);
	MlRunVec_init (&runs);

	if (d.result != ML_OK)
	{
		r = ML_ERR_INVALID;
	}

	else
	{
		r = ml_highlight (
			&runs,
			d.domain,
			text,
			&w->results.env,
			snapshot_at (w, idx),
			err_start,
			err_len
		);
	}

	r = lit (r, &w->out, "[");

	for (uint64_t i = 0; i < runs.len; i++)
	{
		r = lit (r, &w->out, ml_choose (i > 0, ",[", "["));
		r = num (r, &w->out, runs.data [i].start);
		r = lit (r, &w->out, ",");
		r = num (r, &w->out, runs.data [i].len);
		r = lit (r, &w->out, ",\"");
		r = lit (r, &w->out, ml_class_name (runs.data [i].cls));
		r = lit (r, &w->out, "\"]");
	}

	r = lit (r, &w->out, "]");
	MlRunVec_free (&runs);
	return r;
}

MlResult ml_web_ghost (MlWeb *w, MlSlice domain, uint64_t idx, MlSlice text)
{
	MlDomainResult d     = ml_domain_from_name (domain);
	MlStr          value;
	MlStr          block;
	MlGhost        g     = { .result = ML_OK, .shown = 0 };
	MlResult       r     = ML_OK;

	begin (w);
	ml_str_init (&value);
	ml_str_init (&block);

	if (d.result != ML_OK)
	{
		r = ML_ERR_INVALID;
	}

	else if (d.domain == ML_DOMAIN_PROOF)
	{
		g = ml_ghost_proof (&value, &w->session, idx, text);
	}

	else
	{
		g = ml_ghost_block (
			&value,
			&block,
			&w->results,
			d.domain,
			idx,
			text
		);
	}

	r = ml_choose (r == ML_OK, g.result, r);
	r = lit (r, &w->out, "{\"shown\":");
	r = flag (r, &w->out, g.shown);
	r = lit (r, &w->out, ",\"text\":");
	r = str (r, &w->out, ml_str_view (value));
	r = lit (r, &w->out, ",\"block\":");
	r = rows (r, &w->out, ml_str_view (block));
	r = lit (r, &w->out, "}");
	ml_str_free (&value);
	ml_str_free (&block);
	return r;
}

MlResult ml_web_complete (MlWeb *w, MlSlice domain, uint64_t idx, MlSlice text)
{
	MlDomainResult d = ml_domain_from_name (domain);
	MlCompletion   c = ml_completion_none ();
	MlResult       r = ML_OK;

	begin (w);

	if (d.result != ML_OK)
	{
		r = ML_ERR_INVALID;
	}

	else
	{
		c = ml_complete_math (
			&w->results.env,
			snapshot_at (w, idx),
			text,
			ml_domain_units (d.domain)
		);
	}

	r = lit (r, &w->out, "{\"start\":");
	r = num (r, &w->out, c.token_start);
	r = lit (r, &w->out, ",\"items\":[");

	for (uint64_t i = 0; i < c.count; i++)
	{
		r = lit (r, &w->out, ml_choose (i > 0, ",", ""));
		r = str (r, &w->out, c.items [i]);
	}

	r = lit (r, &w->out, "],\"ghost\":");
	r = str (r, &w->out, c.ghost_text);
	r = lit (r, &w->out, ",\"space\":");
	r = flag (r, &w->out, c.append_space);
	return lit (r, &w->out, "}");
}

MlResult ml_web_typeset (MlWeb *w, MlSlice domain, MlSlice text)
{
	MlDomainResult d = ml_domain_from_name (domain);
	MlResult       r = ML_OK;

	begin (w);
	r = ml_choose (d.result == ML_OK, ML_OK, ML_ERR_INVALID);
	r = lit (r, &w->out, "<math>");

	if (r == ML_OK && d.domain == ML_DOMAIN_BOOLEAN)
	{
		r = ml_logic_typeset (&w->out, ML_MATH_MATHML, 0, text);
	}

	else if (r == ML_OK && d.domain == ML_DOMAIN_LAMBDA)
	{
		r = ml_logic_typeset (&w->out, ML_MATH_MATHML, 1, text);
	}

	else if (r == ML_OK)
	{
		r = ml_math_typeset (&w->out, ML_MATH_MATHML, text);
	}

	return lit (r, &w->out, "</math>");
}

MlResult ml_web_steps (MlWeb *w, uint64_t idx)
{
	MlResult    r = ML_OK;
	MlStr       text;
	MlLineSteps st = { .result = ML_OK, .found = 0 };

	begin (w);
	ml_str_init (&text);

	if (idx >= w->session.lines.len)
	{
		r = ML_ERR_INVALID;
	}

	else
	{
		st = ml_line_steps (&text, &w->results, &w->session, idx);
		r  = st.result;
	}

	r = lit (r, &w->out, "{\"found\":");
	r = flag (r, &w->out, st.found);
	r = lit (r, &w->out, ",\"rows\":");
	r = rows (r, &w->out, ml_str_view (text));

	if (r == ML_OK && st.found)
	{
		MlStr raw;

		ml_str_init (&raw);
		st = ml_line_steps_raw (&raw, &w->results, &w->session, idx);
		r  = lit (st.result, &w->out, ",\"items\":");
		r  = ml_choose (
			r == ML_OK,
			web_step_items (&w->out, ml_str_view (raw)),
			r
		);
		ml_str_free (&raw);
	}

	r = lit (r, &w->out, "}");
	ml_str_free (&text);
	return r;
}

MlResult ml_web_hint (MlWeb *w, uint64_t idx)
{
	MlResult r = ML_OK;
	MlStr    reason;

	begin (w);
	ml_str_init (&reason);

	if (idx >= w->session.lines.len)
	{
		r = ML_ERR_INVALID;
	}

	else
	{
		r = ml_proof_line_hint (&reason, &w->session, idx);
	}

	r = lit (r, &w->out, "{\"reason\":");
	r = str (r, &w->out, ml_str_view (reason));
	r = lit (r, &w->out, "}");
	ml_str_free (&reason);
	return r;
}

MlResult ml_web_export (MlWeb *w, MlSlice format, MlSlice words)
{
	MlExportFormat  f = ml_export_format_from_name (format);
	MlExportOptions o = ml_export_options (f);
	MlSlice         path = ml_slice ("", 0);
	MlResult        r = ML_OK;

	begin (w);

	if (f == ML_EXPORT_COUNT)
	{
		r = ML_ERR_INVALID;
	}

	else
	{
		r = ml_export_read_words (&o, words, &path);
	}

	if (r == ML_OK)
	{
		r = ml_export (&w->out, &w->session, &w->results, o);
	}

	return r;
}
