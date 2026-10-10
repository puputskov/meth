#include <matlog/session/session.h>

static const char *const domain_names [ML_DOMAIN_COUNT] =
{
	[ML_DOMAIN_ALGEBRA]   = "algebra",
	[ML_DOMAIN_LAMBDA]    = "lambda",
	[ML_DOMAIN_PGA2]      = "pga2",
	[ML_DOMAIN_PGA3]      = "pga3",
	[ML_DOMAIN_BOOLEAN]   = "boolean"
};

const char *ml_domain_name (MlDomain domain)
{
	int32_t known = (int32_t)domain >= 0 && domain < ML_DOMAIN_COUNT;

	return ml_choose (
		known,
		domain_names [domain],
		"unknown"
	);
}

MlDomainResult ml_domain_from_name (MlSlice name)
{
	MlDomainResult res =
	{
		.result = ML_ERR_PARSE,
		.domain = ML_DOMAIN_ALGEBRA
	};

	for (uint32_t i = 0; i < ML_DOMAIN_COUNT && res.result != ML_OK; i++)
	{
		if (ml_slice_equal (name, ml_slice_cstr (domain_names [i])))
		{
			res.result = ML_OK;
			res.domain = (MlDomain)i;
		}
	}

	/* matrices, vectors and equations are algebra; old files say otherwise */
	if (
		   ml_slice_equal (name, ml_slice_cstr ("matrix"))
		|| ml_slice_equal (name, ml_slice_cstr ("vector"))
		|| ml_slice_equal (name, ml_slice_cstr ("equations"))
	)
	{
		res.result = ML_OK;
		res.domain = ML_DOMAIN_ALGEBRA;
	}

	return res;
}

void ml_session_init (MlSession *s)
{
	MlLineVec_init (&s->lines);
	ml_str_init (&s->path);
	s->cursor   = 0;
	s->domain   = ML_DOMAIN_ALGEBRA;
	s->dirty    = 0;
	s->revision = 0;
	s->group    = 0;
	MlUndoVec_init (&s->undo);
	MlUndoVec_init (&s->redo);
}

void ml_session_free (MlSession *s)
{

	for (uint64_t i = 0; i < s->lines.len; i++)
	{
		ml_str_free (&s->lines.data [i].text);
	}

	ml_session_clear_history (s);
	MlUndoVec_free (&s->undo);
	MlUndoVec_free (&s->redo);
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
	MlLineView view =
	{
		.kind   = ML_LINE_NONE,
		.domain = ML_DOMAIN_ALGEBRA,
		.text =
		{
			.data = "",
			.len  = 0
		},
		.folded = 0
	};

	if (idx < s->lines.len)
	{
		MlLine line = s->lines.data [idx];

		view.kind   = line.kind;
		view.domain = line.domain;
		view.text   = ml_str_view (line.text);
		view.folded = line.folded;
	}

	return view;
}

/* Builds a line owning a copy of text. On failure nothing is left allocated. */
static MlResult line_make (
	MlLine     *out,
	MlLineKind kind,
	MlDomain   domain,
	MlSlice    text
)
{
	MlResult r = ML_OK;

	out->kind   = kind;
	out->domain = domain;
	out->folded = 0;
	ml_str_init (&out->text);

	if (
		   kind == ML_LINE_NONE
		|| (int32_t)domain < 0
		|| domain >= ML_DOMAIN_COUNT
	)
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

/* ---- operations and history ---- */

static MlUndoOp op_make (MlUndoKind kind, uint64_t idx)
{
	MlUndoOp op =
	{
		.kind  = kind,
		.idx   = idx,
		.last  = idx,
		.dest  = idx,
		.line  =
		{
			.kind   = ML_LINE_NONE,
			.domain = ML_DOMAIN_ALGEBRA,
			.text =
			{
				.data = NULL,
				.len  = 0,
				.cap  = 0
			}
		},
		.group = 0
	};

	return op;
}

static void op_free (MlUndoOp *op)
{
	ml_str_free (&op->line.text);
}

static void history_free (MlUndoVec *h)
{
	for (uint64_t i = 0; i < h->len; i++)
	{
		op_free (&h->data [i]);
	}

	MlUndoVec_clear (h);
}

void ml_session_clear_history (MlSession *s)
{
	history_free (&s->undo);
	history_free (&s->redo);
}

void ml_session_checkpoint (MlSession *s)
{
	s->group++;
}

/* Moves the lines first..last so they start at dest; the lines shift. */
static MlResult move_lines (
	MlSession *s,
	uint64_t  first,
	uint64_t  last,
	uint64_t  dest
)
{
	MlResult r = ML_OK;
	uint64_t n = last - first + 1;

	if (first > last || last >= s->lines.len || dest + n > s->lines.len)
	{
		r = ML_ERR_RANGE;
	}

	else if (dest != first)
	{
		MlLine *block = (MlLine *)malloc (n * sizeof (MlLine));

		if (block == NULL)
		{
			r = ML_ERR_NOMEM;
		}

		else
		{
			MlLine *data = s->lines.data;

			memcpy (block, data + first, n * sizeof (MlLine));

			if (dest > first)
			{
				memmove (
					data + first,
					data + last + 1,
					(dest - first) * sizeof (MlLine)
				);
			}

			else
			{
				memmove (
					data + dest + n,
					data + dest,
					(first - dest) * sizeof (MlLine)
				);
			}

			memcpy (data + dest, block, n * sizeof (MlLine));
			free (block);
		}
	}

	return r;
}

/*
 * Does the operation and gives the one that takes it back. The line of the
 * operation moves into the session or into the inverse; on a failure
 * nothing has happened and the operation still owns it.
 */
static MlResult apply (MlSession *s, MlUndoOp op, MlUndoOp *inverse)
{
	MlResult r = ML_OK;

	if (op.kind == ML_UNDO_INSERT)
	{
		r = MlLineVec_insert (&s->lines, op.idx, op.line);
		*inverse = op_make (ML_UNDO_DELETE, op.idx);
	}

	else if (op.kind == ML_UNDO_DELETE && op.idx < s->lines.len)
	{
		*inverse      = op_make (ML_UNDO_INSERT, op.idx);
		inverse->line = s->lines.data [op.idx];
		r             = MlLineVec_remove (&s->lines, op.idx);
	}

	else if (op.kind == ML_UNDO_REPLACE && op.idx < s->lines.len)
	{
		*inverse                = op_make (ML_UNDO_REPLACE, op.idx);
		inverse->line           = s->lines.data [op.idx];
		s->lines.data [op.idx]  = op.line;
	}

	else if (op.kind == ML_UNDO_MOVE)
	{
		uint64_t n = op.last - op.idx + 1;

		r = move_lines (s, op.idx, op.last, op.dest);
		*inverse      = op_make (ML_UNDO_MOVE, op.dest);
		inverse->last = op.dest + n - 1;
		inverse->dest = op.idx;
	}

	else
	{
		r = ML_ERR_RANGE;
	}

	if (r == ML_OK)
	{
		s->dirty = 1;
		s->revision++;
	}

	return r;
}

/* Drops the oldest group when the history is too long. */
static void history_trim (MlUndoVec *h)
{
	if (h->len > ML_UNDO_MAX)
	{
		uint64_t group = h->data [0].group;
		uint64_t n     = 0;

		while (n < h->len && h->data [n].group == group)
		{
			op_free (&h->data [n]);
			n++;
		}

		memmove (h->data, h->data + n, (h->len - n) * sizeof (MlUndoOp));
		h->len -= n;
	}
}

/*
 * A change made by the user: applied, and its inverse remembered. When the
 * inverse cannot be remembered the history is dropped, the change stays.
 */
static MlResult change (MlSession *s, MlUndoOp op)
{
	MlUndoOp inverse;
	MlResult r = apply (s, op, &inverse);

	if (r == ML_OK)
	{
		history_free (&s->redo);
		inverse.group = s->group;

		if (MlUndoVec_push (&s->undo, inverse) != ML_OK)
		{
			op_free (&inverse);
			ml_session_clear_history (s);
		}

		history_trim (&s->undo);
	}

	return r;
}

MlResult ml_session_insert (
	MlSession  *s,
	uint64_t   idx,
	MlLineKind kind,
	MlDomain   domain,
	MlSlice    text
)
{
	MlResult r = ml_choose (
		idx <= s->lines.len,
		ML_OK,
		ML_ERR_RANGE
	);
	MlUndoOp op = op_make (ML_UNDO_INSERT, idx);

	if (r == ML_OK)
	{
		r = line_make (&op.line, kind, domain, text);
	}

	if (r == ML_OK)
	{
		r = change (s, op);

		if (r != ML_OK)
		{
			op_free (&op);
		}
	}

	return r;
}

MlResult ml_session_append (
	MlSession  *s,
	MlLineKind kind,
	MlDomain   domain,
	MlSlice    text
)
{
	return ml_session_insert (s, s->lines.len, kind, domain, text);
}

MlResult ml_session_replace (
	MlSession  *s,
	uint64_t   idx,
	MlLineKind kind,
	MlDomain   domain,
	MlSlice    text
)
{
	MlResult r = ml_choose (
		idx < s->lines.len,
		ML_OK,
		ML_ERR_RANGE
	);
	MlUndoOp op = op_make (ML_UNDO_REPLACE, idx);

	if (r == ML_OK)
	{
		r = line_make (&op.line, kind, domain, text);
	}

	if (r == ML_OK)
	{
		r = change (s, op);

		if (r != ML_OK)
		{
			op_free (&op);
		}
	}

	return r;
}

MlResult ml_session_delete (MlSession *s, uint64_t idx)
{
	MlResult r = change (s, op_make (ML_UNDO_DELETE, idx));

	if (r == ML_OK)
	{
		ml_session_set_cursor (s, s->cursor);
	}

	return r;
}

MlResult ml_session_move (
	MlSession *s,
	uint64_t  first,
	uint64_t  last,
	uint64_t  dest
)
{
	MlUndoOp op = op_make (ML_UNDO_MOVE, first);

	op.last = last;
	op.dest = dest;
	return change (s, op);
}

/* Takes back (or redoes) the last group of the stack `from`. */
static MlUndoResult step_history (MlSession *s, MlUndoVec *from, MlUndoVec *to)
{
	MlUndoResult res =
	{
		.result = ML_OK,
		.did    = 0,
		.where  = 0
	};

	uint64_t group = ml_choose (
		from->len > 0,
		from->data [from->len - 1].group,
		0
	);

	while (
		   res.result == ML_OK
		&& from->len > 0
		&& from->data [from->len - 1].group == group
	)
	{
		MlUndoOp op = from->data [from->len - 1];
		MlUndoOp inverse;

		res.result = apply (s, op, &inverse);

		if (res.result == ML_OK)
		{
			from->len--;
			inverse.group = group;
			res.did       = 1;
			res.where     = ml_choose (
				op.kind == ML_UNDO_MOVE,
				op.dest,
				op.idx
			);

			if (MlUndoVec_push (to, inverse) != ML_OK)
			{
				op_free (&inverse);
				res.result = ML_ERR_NOMEM;
			}
		}
	}

	return res;
}

MlUndoResult ml_session_undo (MlSession *s)
{
	return step_history (s, &s->undo, &s->redo);
}

MlUndoResult ml_session_redo (MlSession *s)
{
	return step_history (s, &s->redo, &s->undo);
}

int32_t ml_block_continues (
	MlLineKind a_kind,
	MlDomain   a_domain,
	MlLineKind b_kind,
	MlDomain   b_domain
)
{
	return (
		   (
			   a_kind == ML_LINE_MATH
			&& b_kind == ML_LINE_MATH
			&& a_domain == b_domain
		)
		|| (a_kind == ML_LINE_NOTE && b_kind == ML_LINE_NOTE)
		|| (a_kind == ML_LINE_INCLUDE && b_kind == ML_LINE_INCLUDE)
		|| (a_kind == ML_LINE_UNKNOWN && b_kind == ML_LINE_UNKNOWN)
	);
}

static MlBlockKind block_kind_of (MlLineKind kind)
{
	MlBlockKind k = ML_BLOCK_NONE;

	k = ml_choose (kind == ML_LINE_MATH, ML_BLOCK_CODE, k);
	k = ml_choose (kind == ML_LINE_NOTE, ML_BLOCK_TEXT, k);
	k = ml_choose (kind == ML_LINE_INCLUDE, ML_BLOCK_INCLUDE, k);
	k = ml_choose (kind == ML_LINE_BLANK, ML_BLOCK_BLANK, k);
	k = ml_choose (kind == ML_LINE_UNKNOWN, ML_BLOCK_UNKNOWN, k);
	return k;
}

MlBlock ml_session_block (const MlSession *s, uint64_t idx)
{
	MlBlock b =
	{
		.kind   = ML_BLOCK_NONE,
		.domain = ML_DOMAIN_ALGEBRA,
		.first  = idx,
		.last   = idx,
		.folded = 0
	};

	if (idx < s->lines.len)
	{
		MlLine line = s->lines.data [idx];

		b.kind   = block_kind_of (line.kind);
		b.domain = line.domain;

		while (
			   b.first > 0
			&& ml_block_continues (
				s->lines.data [b.first - 1].kind,
				s->lines.data [b.first - 1].domain,
				line.kind,
				line.domain
			)
		)
		{
			b.first--;
		}

		while (
			   b.last + 1 < s->lines.len
			&& ml_block_continues (
				line.kind,
				line.domain,
				s->lines.data [b.last + 1].kind,
				s->lines.data [b.last + 1].domain
			)
		)
		{
			b.last++;
		}

		b.folded = s->lines.data [b.first].folded;
	}

	return b;
}

void ml_session_set_folded (MlSession *s, uint64_t idx, int32_t folded)
{
	if (idx < s->lines.len)
	{
		s->lines.data [idx].folded = folded;
	}
}

void ml_session_set_cursor (MlSession *s, uint64_t idx)
{
	uint64_t last = ml_choose (
		s->lines.len > 0,
		s->lines.len - 1,
		0
	);

	s->cursor = ml_choose (
		idx < last,
		idx,
		last
	);
}

void ml_session_move_cursor (MlSession *s, int64_t delta)
{
	uint64_t target = s->cursor;

	if (delta < 0)
	{
		uint64_t back = (uint64_t)(-(delta + 1)) + 1;

		target = ml_choose (
			back > s->cursor,
			0,
			s->cursor - back
		);
	}

	else if (delta > 0)
	{
		target = ml_choose (
			(uint64_t)delta > UINT64_MAX - s->cursor,
			UINT64_MAX,
			s->cursor + (uint64_t)delta
		);
	}

	ml_session_set_cursor (s, target);
}

void ml_session_set_domain (MlSession *s, MlDomain domain)
{
	s->domain = domain;
}
