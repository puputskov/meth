#include <matlog/session/session.h>

static const char *const domain_names[ML_DOMAIN_COUNT] =
{
	"algebra", "equations", "matrix", "vector", "lambda", "pga2", "pga3", "boolean"
};

const char *ml_domain_name (MlDomain domain)
{
	int32_t known = (int32_t)domain >= 0 && domain < ML_DOMAIN_COUNT;

	return known ? domain_names[domain] : "unknown";
}

MlDomainResult ml_domain_from_name (MlSlice name)
{
	MlDomainResult res = { .result = ML_ERR_PARSE, .domain = ML_DOMAIN_ALGEBRA };
	uint32_t       i;

	for (i = 0; i < ML_DOMAIN_COUNT && res.result != ML_OK; i++)
	{
		if (ml_slice_equal (name, ml_slice_cstr (domain_names[i])))
		{
			res.result = ML_OK;
			res.domain = (MlDomain)i;
		}
	}

	return res;
}

void ml_session_init (MlSession *s)
{
	MlLineVec_init (&s->lines);
	ml_str_init (&s->path);
	s->cursor = 0;
	s->domain = ML_DOMAIN_ALGEBRA;
	s->dirty  = 0;
}

void ml_session_free (MlSession *s)
{
	uint64_t i;

	for (i = 0; i < s->lines.len; i++)
	{
		ml_str_free (&s->lines.data[i].text);
	}

	MlLineVec_free (&s->lines);
	ml_str_free (&s->path);
	s->cursor = 0;
}

MlResult ml_session_set_path (MlSession *s, MlSlice path)
{
	MlStr    copy;
	MlResult r;

	ml_str_init (&copy);
	r = ml_str_append (&copy, path);

	if (r == ML_OK)
	{
		ml_str_free (&s->path);
		s->path = copy;
	}

	else
	{
		ml_str_free (&copy);
	}

	return r;
}

MlLineView ml_session_get (const MlSession *s, uint64_t idx)
{
	MlLineView view = { .kind = ML_LINE_NONE, .domain = ML_DOMAIN_ALGEBRA, .text = { .data = "", .len = 0 } };

	if (idx < s->lines.len)
	{
		MlLine line = s->lines.data[idx];

		view.kind   = line.kind;
		view.domain = line.domain;
		view.text   = ml_str_view (line.text);
	}

	return view;
}

/* Builds a line owning a copy of text. On failure nothing is left allocated. */
static MlResult line_make (MlLine *out, MlLineKind kind, MlDomain domain, MlSlice text)
{
	MlResult r = ML_OK;

	out->kind   = kind;
	out->domain = domain;
	ml_str_init (&out->text);

	if (kind == ML_LINE_NONE || (int32_t)domain < 0 || domain >= ML_DOMAIN_COUNT)
	{
		r = ML_ERR_INVALID;
	}

	if (r == ML_OK)
	{
		r = ml_str_append (&out->text, text);
	}

	if (r != ML_OK)
	{
		ml_str_free (&out->text);
	}

	return r;
}

MlResult ml_session_insert (MlSession *s, uint64_t idx, MlLineKind kind, MlDomain domain, MlSlice text)
{
	MlResult r    = idx <= s->lines.len ? ML_OK : ML_ERR_RANGE;
	MlLine   line = { .kind = ML_LINE_NONE, .domain = ML_DOMAIN_ALGEBRA, .text = { .data = NULL, .len = 0, .cap = 0 } };

	if (r == ML_OK)
	{
		r = line_make (&line, kind, domain, text);
	}

	if (r == ML_OK)
	{
		r = MlLineVec_insert (&s->lines, idx, line);

		if (r != ML_OK)
		{
			ml_str_free (&line.text);
		}
	}

	if (r == ML_OK)
	{
		s->dirty = 1;
	}

	return r;
}

MlResult ml_session_append (MlSession *s, MlLineKind kind, MlDomain domain, MlSlice text)
{
	return ml_session_insert (s, s->lines.len, kind, domain, text);
}

MlResult ml_session_replace (MlSession *s, uint64_t idx, MlLineKind kind, MlDomain domain, MlSlice text)
{
	MlResult r    = idx < s->lines.len ? ML_OK : ML_ERR_RANGE;
	MlLine   line = { .kind = ML_LINE_NONE, .domain = ML_DOMAIN_ALGEBRA, .text = { .data = NULL, .len = 0, .cap = 0 } };

	if (r == ML_OK)
	{
		r = line_make (&line, kind, domain, text);
	}

	if (r == ML_OK)
	{
		ml_str_free (&s->lines.data[idx].text);
		s->lines.data[idx] = line;
		s->dirty           = 1;
	}

	return r;
}

MlResult ml_session_delete (MlSession *s, uint64_t idx)
{
	MlResult r = idx < s->lines.len ? ML_OK : ML_ERR_RANGE;

	if (r == ML_OK)
	{
		ml_str_free (&s->lines.data[idx].text);
		r = MlLineVec_remove (&s->lines, idx);
	}

	if (r == ML_OK)
	{
		s->dirty = 1;
		ml_session_set_cursor (s, s->cursor);
	}

	return r;
}

void ml_session_set_cursor (MlSession *s, uint64_t idx)
{
	uint64_t last = s->lines.len > 0 ? s->lines.len - 1 : 0;

	s->cursor = idx < last ? idx : last;
}

void ml_session_move_cursor (MlSession *s, int64_t delta)
{
	uint64_t target = s->cursor;

	if (delta < 0)
	{
		uint64_t back = (uint64_t)(-(delta + 1)) + 1;

		target = back > s->cursor ? 0 : s->cursor - back;
	}

	else if (delta > 0)
	{
		target = (uint64_t)delta > UINT64_MAX - s->cursor ? UINT64_MAX : s->cursor + (uint64_t)delta;
	}

	ml_session_set_cursor (s, target);
}

void ml_session_set_domain (MlSession *s, MlDomain domain)
{
	s->domain = domain;
}
