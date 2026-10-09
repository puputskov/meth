#include <matlog/ui/editor.h>

#include <matlog/core/utf8.h>

static const char *const state_names [ML_STATE_COUNT] =
{
	[ML_STATE_NORMAL]  = "NORMAL",
	[ML_STATE_INSERT]  = "INSERT",
	[ML_STATE_NOTE]    = "NOTE",
	[ML_STATE_COMMAND] = "COMMAND",
	[ML_STATE_VISUAL]  = "VISUAL"
};

typedef struct DomainKey
{
	uint32_t key;
	MlDomain domain;
} DomainKey;

/* The key after g that selects a domain. */
static const DomainKey domain_keys [] =
{
	{ .key = 'a', .domain = ML_DOMAIN_ALGEBRA },
	{ .key = 'e', .domain = ML_DOMAIN_EQUATIONS },
	{ .key = 'm', .domain = ML_DOMAIN_MATRIX },
	{ .key = 'v', .domain = ML_DOMAIN_VECTOR },
	{ .key = 'l', .domain = ML_DOMAIN_LAMBDA },
	{ .key = 'b', .domain = ML_DOMAIN_BOOLEAN },
	{ .key = '2', .domain = ML_DOMAIN_PGA2 },
	{ .key = '3', .domain = ML_DOMAIN_PGA3 }
};

const char *ml_input_state_name (MlInputState state)
{
	return ml_choose (
		(int32_t)state >= 0 && state < ML_STATE_COUNT,
		state_names [state],
		"UNKNOWN"
	);
}

/* Frees the text of every line of v and empties it. */
static void lines_clear (MlLineVec *v)
{
	for (uint64_t i = 0; i < v->len; i++)
	{
		ml_str_free (&v->data [i].text);
	}

	MlLineVec_clear (v);
}

static void lines_free (MlLineVec *v)
{
	lines_clear (v);
	MlLineVec_free (v);
}

void ml_editor_init (MlEditor *ed)
{
	ml_session_init (&ed->session);
	ml_results_init (&ed->results);
	ed->results_rev   = ed->session.revision;
	ed->steps_open    = 0;
	ed->steps_line    = 0;
	ml_str_init (&ed->steps);
	ml_lineedit_init (&ed->edit);
	ml_rewriter_init (&ed->rewriter, ML_DOMAIN_ALGEBRA);
	MlLineVec_init (&ed->yank);
	ml_str_init (&ed->message);
	ed->count         = 0;
	ed->visual_anchor = 0;
	ed->state         = ML_STATE_NORMAL;
	ed->pending       = 0;
	ed->edit_index    = 0;
	ed->edit_existing = 0;
	ed->edit_kind     = ML_LINE_MATH;
	ed->edit_domain   = ML_DOMAIN_ALGEBRA;
	ed->quit          = 0;
}

void ml_editor_free (MlEditor *ed)
{
	ml_results_free (&ed->results);
	ml_str_free (&ed->steps);
	ml_session_free (&ed->session);
	ml_lineedit_free (&ed->edit);
	ml_rewriter_free (&ed->rewriter);
	lines_free (&ed->yank);
	ml_str_free (&ed->message);
}

static MlResult set_message (MlEditor *ed, const char *text)
{
	ml_str_clear (&ed->message);
	return ml_str_append (&ed->message, ml_slice_cstr (text));
}

/* Starts typing a line of the given kind, loading text into the buffer. */
static MlResult begin_line (
	MlEditor     *ed,
	MlInputState state,
	MlLineKind   kind,
	MlDomain     domain,
	uint64_t     index,
	int32_t      existing,
	MlSlice      text
)
{
	MlResult r = ml_lineedit_set (&ed->edit, text);

	if (r == ML_OK)
	{
		ed->state         = state;
		ed->edit_kind     = kind;
		ed->edit_domain   = domain;
		ed->edit_index    = index;
		ed->edit_existing = existing;
		ml_rewriter_set_domain (
			&ed->rewriter,
			ml_choose (state == ML_STATE_NOTE, ML_REWRITE_NOTES, domain)
		);
	}

	return r;
}

/* Index of a new line written below the cursor line. */
static uint64_t line_below_cursor (const MlEditor *ed)
{
	MlBlock b = ml_session_block (&ed->session, ed->session.cursor);

	return ml_choose (
		ed->session.lines.len > 0,
		ml_choose (
			b.folded && b.last > b.first,
			b.last + 1,
			ed->session.cursor + 1
		),
		0
	);
}

MlResult ml_editor_begin (MlEditor *ed)
{
	MlResult r = ml_editor_refresh (ed);

	ml_str_clear (&ed->message);

	if (r == ML_OK)
	{
		r = begin_line (
			ed,
			ML_STATE_INSERT,
			ML_LINE_MATH,
			ed->session.domain,
			ed->session.lines.len,
			0,
			ml_slice ("", 0)
		);
	}

	return r;
}

/* Writes the typed line into the session and puts the cursor on it. */
static MlResult commit_line (MlEditor *ed)
{
	MlResult   r    = ML_OK;
	MlSlice    text = ml_str_view (ed->edit.text);
	MlLineKind kind = ed->edit_kind;

	if (ed->state == ML_STATE_INSERT || ed->state == ML_STATE_NOTE)
	{
		r    = ml_rewriter_flush (&ed->rewriter, &ed->edit);
		text = ml_str_view (ed->edit.text);
	}

	if (kind == ML_LINE_MATH && text.len == 0)
	{
		kind = ML_LINE_BLANK;
	}

	else if (kind == ML_LINE_BLANK && text.len > 0)
	{
		kind = ML_LINE_MATH;
	}

	if (r == ML_OK && ed->edit_existing)
	{
		r = ml_session_replace (
			&ed->session,
			ed->edit_index,
			kind,
			ed->edit_domain,
			text
		);
	}

	else if (r == ML_OK)
	{
		r = ml_session_insert (
			&ed->session,
			ed->edit_index,
			kind,
			ed->edit_domain,
			text
		);
	}

	if (r == ML_OK)
	{
		ml_session_set_cursor (&ed->session, ed->edit_index);
	}

	return r;
}

/*
 * Enter while typing: commit and go on with the next line, or finish an edit.
 */
static MlResult finish_line (MlEditor *ed)
{
	MlResult     r        = commit_line (ed);
	MlInputState state    = ed->state;
	int32_t      existing = ed->edit_existing;
	uint64_t     next     = ed->edit_index + 1;

	if (r == ML_OK && existing)
	{
		ed->state = ML_STATE_NORMAL;
		ml_lineedit_clear (&ed->edit);
	}

	else if (r == ML_OK)
	{
		r = begin_line (
			ed,
			state,
			ml_choose (
				state == ML_STATE_NOTE,
				ML_LINE_NOTE,
				ML_LINE_MATH
			),
			ml_choose (
				state == ML_STATE_NOTE,
				ed->edit_domain,
				ed->session.domain
			),
			next,
			0,
			ml_slice ("", 0)
		);
	}

	return r;
}

/* Esc while typing: keep what was typed and return to Normal. */
static MlResult leave_line (MlEditor *ed)
{
	MlResult r = ML_OK;

	if (ed->edit_existing || ed->edit.text.len > 0)
	{
		r = commit_line (ed);
	}

	if (r == ML_OK)
	{
		ed->state = ML_STATE_NORMAL;
		ml_lineedit_clear (&ed->edit);
		ml_session_set_cursor (&ed->session, ed->session.cursor);
	}

	return r;
}

static void set_domain (MlEditor *ed, MlDomain domain);

/* Alt+Left and Alt+Right scroll through the domains. */
static int32_t is_domain_cycle (MlKey key)
{
	return (
		   (key.code == ML_KEY_LEFT || key.code == ML_KEY_RIGHT)
		&& (key.mods & ML_MOD_ALT)
	);
}

static MlDomain next_domain (MlDomain domain, MlKey key)
{
	uint32_t count = ML_DOMAIN_COUNT;
	uint32_t step  = ml_choose (key.code == ML_KEY_RIGHT, 1, count - 1);

	return (MlDomain)(((uint32_t)domain + step) % count);
}

/* In Normal: the domain for new lines. */
static MlResult cycle_domain (MlEditor *ed, MlKey key)
{
	MlDomain next = next_domain (ed->session.domain, key);

	set_domain (ed, next);
	return set_message (ed, ml_domain_name (next));
}

/* While typing a math line: the domain of that line (and of the next ones). */
static MlResult cycle_line_domain (MlEditor *ed, MlKey key)
{
	MlResult r = ML_OK;

	if (ed->edit_kind == ML_LINE_MATH || ed->edit_kind == ML_LINE_BLANK)
	{
		MlDomain next = next_domain (ed->edit_domain, key);

		ed->edit_domain = next;
		ml_rewriter_set_domain (&ed->rewriter, next);

		if (!ed->edit_existing)
		{
			ed->session.domain = next;
		}

		r = set_message (ed, ml_domain_name (next));
	}

	return r;
}

static MlResult text_key (MlEditor *ed, MlKey key)
{
	MlResult     r   = ML_OK;
	MlEditResult res =
	{
		.result  = ML_OK,
		.handled = 0
	};

	if (key.code == ML_KEY_ESCAPE)
	{
		r = leave_line (ed);
	}

	else if (key.code == ML_KEY_ENTER)
	{
		r = finish_line (ed);
	}

	else if (is_domain_cycle (key))
	{
		r = cycle_line_domain (ed, key);
	}

	else if (ed->state == ML_STATE_INSERT || ed->state == ML_STATE_NOTE)
	{
		res = ml_rewriter_handle_key (&ed->rewriter, &ed->edit, key);
		r   = res.result;
	}

	else
	{
		res = ml_lineedit_handle_key (&ed->edit, key);
		r   = res.result;
	}

	return r;
}

/* Copies the lines first..last of the session into out (emptied first). */
static MlResult collect_lines (
	const MlEditor *ed,
	MlLineVec      *out,
	uint64_t       first,
	uint64_t       last
)
{
	MlResult r = ML_OK;

	lines_clear (out);

	for (uint64_t i = first; i <= last && r == ML_OK; i++)
	{
		MlLineView v = ml_session_get (&ed->session, i);
		MlLine     l =
		{
			.kind   = v.kind,
			.domain = v.domain,
			.text =
			{
				.data = NULL,
				.len  = 0,
				.cap  = 0
			}
		};

		r = ml_choose (
			v.kind != ML_LINE_NONE,
			ml_str_append (&l.text, v.text),
			ML_ERR_RANGE
		);

		if (r == ML_OK)
		{
			r = MlLineVec_push (out, l);
		}

		if (r != ML_OK)
		{
			ml_str_free (&l.text);
		}
	}

	return r;
}

/* The line register becomes the lines first..last. */
static MlResult yank_range (MlEditor *ed, uint64_t first, uint64_t last)
{
	return collect_lines (ed, &ed->yank, first, last);
}

/* Inserts copies of the lines of v at index at, one after the other. */
static MlResult insert_lines (MlEditor *ed, const MlLineVec *v, uint64_t at)
{
	MlResult r = ML_OK;

	for (uint64_t i = 0; i < v->len && r == ML_OK; i++)
	{
		r = ml_session_insert (
			&ed->session,
			at + i,
			v->data [i].kind,
			v->data [i].domain,
			ml_str_view (v->data [i].text)
		);
	}

	return r;
}

/* Puts the register at index at, reps times, and the cursor on its start. */
static MlResult paste_lines (MlEditor *ed, uint64_t at, uint64_t reps)
{
	MlResult r = ML_OK;

	for (uint64_t k = 0; k < reps && r == ML_OK; k++)
	{
		r = insert_lines (ed, &ed->yank, at + k * ed->yank.len);
	}

	if (r == ML_OK && ed->yank.len > 0)
	{
		ml_session_set_cursor (&ed->session, at);
	}

	return r;
}

static MlResult paste_line (MlEditor *ed, int32_t below)
{
	return paste_lines (
		ed,
		ml_choose (
			below,
			line_below_cursor (ed),
			ed->session.cursor
		),
		ml_choose (ed->count > 0, ed->count, 1)
	);
}

/* Removes the lines first..last; the cursor goes to where they were. */
static MlResult remove_range (MlEditor *ed, uint64_t first, uint64_t last)
{
	MlResult r = ML_OK;

	for (uint64_t i = last + 1; i > first && r == ML_OK; i--)
	{
		r = ml_session_delete (&ed->session, i - 1);
	}

	ml_session_set_cursor (&ed->session, first);
	return r;
}

/* The last line of a range of n lines from the cursor, inside the session. */
static uint64_t range_end (const MlEditor *ed, uint64_t n)
{
	MlBlock  b    = ml_session_block (&ed->session, ed->session.cursor);
	uint64_t last = ed->session.cursor + n - 1;

	/* on a folded block the whole block is the line */
	if (n == 1 && b.folded && b.last > b.first)
	{
		last = b.last;
	}

	return ml_choose (
		last >= ed->session.lines.len,
		ed->session.lines.len - 1,
		last
	);
}

static MlResult delete_range (MlEditor *ed, uint64_t first, uint64_t last)
{
	MlResult r = yank_range (ed, first, last);

	if (r == ML_OK)
	{
		r = remove_range (ed, first, last);
	}

	return r;
}

static MlResult delete_line (MlEditor *ed)
{
	MlResult r = ML_OK;

	if (ed->session.lines.len > 0)
	{
		r = delete_range (
			ed,
			ed->session.cursor,
			range_end (ed, ml_choose (ed->count > 0, ed->count, 1))
		);
	}

	return r;
}

/*
 * Moves the lines first..last by delta (clamped to the session); the cursor
 * and the selection move with them.
 */
static MlResult shift_lines (
	MlEditor *ed,
	uint64_t first,
	uint64_t last,
	int64_t  delta
)
{
	MlResult r   = ML_OK;
	uint64_t n   = last - first + 1;
	uint64_t max = ed->session.lines.len - n;
	uint64_t to  = first;

	if (delta < 0)
	{
		to = ml_choose (
			(uint64_t)(-delta) > first,
			0,
			first - (uint64_t)(-delta)
		);
	}

	else if (delta > 0)
	{
		to = ml_choose (
			first + (uint64_t)delta > max,
			max,
			first + (uint64_t)delta
		);
	}

	if (to != first)
	{
		r = ml_session_move (&ed->session, first, last, to);
	}

	if (r == ML_OK && to != first)
	{
		ed->session.cursor += to;
		ed->session.cursor -= first;
		ed->visual_anchor  += to;
		ed->visual_anchor  -= first;
	}

	return r;
}

static void set_domain (MlEditor *ed, MlDomain domain)
{
	ml_session_set_domain (&ed->session, domain);
	ml_rewriter_set_domain (&ed->rewriter, domain);
}

/* The key after g: a domain, or g again for the first line. */
static MlResult goto_key (MlEditor *ed, uint32_t ch)
{
	MlResult r     = ML_OK;
	int32_t  found = 0;

	for (uint64_t i = 0; i < ml_count (domain_keys) && !found; i++)
	{
		found = domain_keys [i].key == ch;

		if (found)
		{
			set_domain (ed, domain_keys [i].domain);
			r = set_message (ed, ml_domain_name (domain_keys [i].domain));
		}
	}

	if (!found && ch == 'g')
	{
		ml_session_set_cursor (
			&ed->session,
			ml_choose (ed->count > 0, ed->count - 1, 0)
		);
	}

	else if (!found)
	{
		r = set_message (ed, "g: unknown domain key");
	}

	return r;
}

static MlResult edit_cursor_line (MlEditor *ed)
{
	MlResult   r = ML_OK;
	MlLineView v = ml_session_get (&ed->session, ed->session.cursor);

	if (v.kind == ML_LINE_NONE)
	{
		r = ml_editor_begin (ed);
	}

	else
	{
		/* a folded block is opened to be edited */
		ml_session_set_folded (
			&ed->session,
			ml_session_block (&ed->session, ed->session.cursor).first,
			0
		);
		r = begin_line (
			ed,
			ml_choose (
				v.kind == ML_LINE_NOTE,
				ML_STATE_NOTE,
				ML_STATE_INSERT
			),
			v.kind,
			v.domain,
			ed->session.cursor,
			1,
			v.text
		);
	}

	return r;
}

static MlResult begin_command (MlEditor *ed)
{
	MlResult r = ml_lineedit_set (&ed->edit, ml_slice ("", 0));

	if (r == ML_OK)
	{
		ed->state = ML_STATE_COMMAND;
	}

	return r;
}

static MlResult fold_block (MlEditor *ed, int32_t how);
static void fold_all (MlEditor *ed, int32_t how);
static void jump_block (MlEditor *ed, int32_t forward);
static void normalize_cursor (MlEditor *ed);

/* The key after z: za toggles, zo opens, zc closes, zM / zR all blocks. */
static MlResult zed_key (MlEditor *ed, uint32_t ch)
{
	MlResult r = ML_OK;

	if (ch == 'a')
	{
		r = fold_block (ed, 2);
	}

	else if (ch == 'o')
	{
		r = fold_block (ed, 0);
	}

	else if (ch == 'c')
	{
		r = fold_block (ed, 1);
	}

	else if (ch == 'M')
	{
		fold_all (ed, 1);
	}

	else if (ch == 'R')
	{
		fold_all (ed, 0);
	}

	else
	{
		r = set_message (ed, "z: unknown key");
	}

	return r;
}

/* The second key of d, y, g, z, ] and [. */
static MlResult pending_key (MlEditor *ed, uint32_t first, uint32_t ch)
{
	MlResult r = ML_OK;

	if (first == 'd' && ch == 'd')
	{
		r = delete_line (ed);
	}

	else if (first == 'y' && ch == 'y' && ed->session.lines.len > 0)
	{
		r = yank_range (
			ed,
			ed->session.cursor,
			range_end (ed, ml_choose (ed->count > 0, ed->count, 1))
		);
	}

	else if (first == 'g')
	{
		r = goto_key (ed, ch);
	}

	else if (first == 'z')
	{
		r = zed_key (ed, ch);
	}

	else if (first == ']' && ch == ']')
	{
		jump_block (ed, 1);
	}

	else if (first == '[' && ch == '[')
	{
		jump_block (ed, 0);
	}

	return r;
}

/* Shows the steps of the cursor line under it, or hides them again. */
static MlResult toggle_steps (MlEditor *ed)
{
	MlResult    r    = ML_OK;
	MlLineSteps st;
	int32_t     hide = ed->steps_open && ed->steps_line == ed->session.cursor;

	ed->steps_open = 0;
	ml_str_clear (&ed->steps);

	if (!hide)
	{
		st = ml_line_steps (
			&ed->steps,
			&ed->results,
			&ed->session,
			ed->session.cursor
		);
		r  = st.result;

		if (r == ML_OK && st.found)
		{
			ed->steps_open = 1;
			ed->steps_line = ed->session.cursor;
		}

		else if (r == ML_OK)
		{
			r = set_message (ed, "no steps for this line");
		}
	}

	return r;
}

/* A folded block: the lines after its first are hidden. */
static int32_t is_folded (MlBlock b)
{
	return b.folded && b.last > b.first;
}

/* The cursor never rests on a line that is hidden in a folded block. */
static void normalize_cursor (MlEditor *ed)
{
	MlBlock b = ml_session_block (&ed->session, ed->session.cursor);

	if (is_folded (b) && ed->session.cursor != b.first)
	{
		ml_session_set_cursor (&ed->session, b.first);
	}
}

/* One line down; a folded block is passed in one step. */
static void step_down (MlEditor *ed)
{
	MlBlock b = ml_session_block (&ed->session, ed->session.cursor);

	ml_session_set_cursor (
		&ed->session,
		ml_choose (is_folded (b), b.last + 1, ed->session.cursor + 1)
	);
	normalize_cursor (ed);
}

static void step_up (MlEditor *ed)
{
	ml_session_move_cursor (&ed->session, -1);
	normalize_cursor (ed);
}

static void move_down (MlEditor *ed, uint64_t n)
{
	for (uint64_t i = 0; i < n; i++)
	{
		step_down (ed);
	}
}

static void move_up (MlEditor *ed, uint64_t n)
{
	for (uint64_t i = 0; i < n; i++)
	{
		step_up (ed);
	}
}

/* Folds, unfolds or toggles the block of the cursor line. */
static MlResult fold_block (MlEditor *ed, int32_t how)
{
	MlResult r = ML_OK;
	MlBlock  b = ml_session_block (&ed->session, ed->session.cursor);

	if (b.kind == ML_BLOCK_NONE || b.last == b.first)
	{
		r = set_message (ed, "nothing to fold: the block is one line");
	}

	else
	{
		/* how: 0 unfold, 1 fold, 2 toggle */
		ml_session_set_folded (
			&ed->session,
			b.first,
			ml_choose (how == 2, !b.folded, how)
		);
		normalize_cursor (ed);
	}

	return r;
}

/* Folds (how 1) or unfolds (how 0) every block. */
static void fold_all (MlEditor *ed, int32_t how)
{
	uint64_t i = 0;

	while (i < ed->session.lines.len)
	{
		MlBlock b = ml_session_block (&ed->session, i);

		ml_session_set_folded (
			&ed->session,
			b.first,
			ml_choose (b.last > b.first, how, 0)
		);
		i = b.last + 1;
	}

	normalize_cursor (ed);
}

/* ]] and [[ : the start of the next block, or of this one or the one before. */
static void jump_block (MlEditor *ed, int32_t forward)
{
	MlBlock b = ml_session_block (&ed->session, ed->session.cursor);

	if (b.kind != ML_BLOCK_NONE && forward)
	{
		if (b.last + 1 < ed->session.lines.len)
		{
			ml_session_set_cursor (&ed->session, b.last + 1);
		}
	}

	else if (b.kind != ML_BLOCK_NONE && ed->session.cursor > b.first)
	{
		ml_session_set_cursor (&ed->session, b.first);
	}

	else if (b.kind != ML_BLOCK_NONE && b.first > 0)
	{
		ml_session_set_cursor (
			&ed->session,
			ml_session_block (&ed->session, b.first - 1).first
		);
	}

	normalize_cursor (ed);
}

/* Takes back (or does again) the last user action. */
static MlResult undo_redo (MlEditor *ed, int32_t redo)
{
	MlResult     r = ML_OK;
	MlUndoResult u = ml_choose (
		redo,
		ml_session_redo (&ed->session),
		ml_session_undo (&ed->session)
	);

	r = u.result;

	if (r == ML_OK && u.did)
	{
		ml_session_set_cursor (&ed->session, u.where);
		r = set_message (ed, ml_choose (redo, "redone", "undone"));
	}

	else if (r == ML_OK)
	{
		r = set_message (
			ed,
			ml_choose (
				redo,
				"nothing to redo",
				"nothing to undo"
			)
		);
	}

	return r;
}

/* The selected lines of the Visual state, first and last. */
static uint64_t selection_first (const MlEditor *ed)
{
	return ml_choose (
		ed->visual_anchor < ed->session.cursor,
		ed->visual_anchor,
		ed->session.cursor
	);
}

static uint64_t selection_last (const MlEditor *ed)
{
	return ml_choose (
		ed->visual_anchor > ed->session.cursor,
		ed->visual_anchor,
		ed->session.cursor
	);
}

static int32_t is_alt_arrow (MlKey key, MlKeyCode code)
{
	return key.code == code && (key.mods & ML_MOD_ALT);
}

static MlResult begin_command (MlEditor *ed);

/* The keys of the Visual state: whole lines are selected. */
static MlResult visual_key (MlEditor *ed, MlKey key)
{
	MlResult r     = ML_OK;
	uint64_t first = selection_first (ed);
	uint64_t last  = selection_last (ed);
	uint32_t ch    = ml_choose (
		key.code == ML_KEY_CHAR && !(key.mods & (ML_MOD_CTRL | ML_MOD_ALT)),
		key.cp,
		0
	);
	uint64_t reps  = ml_choose (ed->count > 0, ed->count, 1);

	if (ch >= '1' && ch <= '9')
	{
		ed->count = ed->count * 10 + (ch - '0');
	}

	else if (ch == '0' && ed->count > 0)
	{
		ed->count = ed->count * 10;
	}

	else
	{
		if (ed->pending != 0)
		{
			/* only gg is a two key command here */
			ed->pending = 0;
			ml_session_set_cursor (
				&ed->session,
				ml_choose (
					ch == 'g',
					ml_choose (ed->count > 0, ed->count - 1, 0),
					ed->session.cursor
				)
			);
		}

		else if (key.code == ML_KEY_ESCAPE || ch == 'v' || ch == 'V')
		{
			ed->state = ML_STATE_NORMAL;
		}

		else if (is_alt_arrow (key, ML_KEY_DOWN))
		{
			r = shift_lines (ed, first, last, 1);
		}

		else if (is_alt_arrow (key, ML_KEY_UP))
		{
			r = shift_lines (ed, first, last, -1);
		}

		else if (ch == 'j' || key.code == ML_KEY_DOWN)
		{
			move_down (ed, reps);
		}

		else if (ch == 'k' || key.code == ML_KEY_UP)
		{
			move_up (ed, reps);
		}

		else if (ch == 'G')
		{
			ml_session_set_cursor (
				&ed->session,
				ml_choose (ed->count > 0, ed->count - 1, ed->session.lines.len)
			);
		}

		else if (ch == 'g')
		{
			ed->pending = ch;
		}

		else if (ch == 'o')
		{
			uint64_t t = ed->visual_anchor;

			ed->visual_anchor = ed->session.cursor;
			ml_session_set_cursor (&ed->session, t);
		}

		else if (ch == 'd' || ch == 'x')
		{
			r         = delete_range (ed, first, last);
			ed->state = ML_STATE_NORMAL;
		}

		else if (ch == 'y')
		{
			r = yank_range (ed, first, last);
			ml_session_set_cursor (&ed->session, first);
			ed->state = ML_STATE_NORMAL;
		}

		else if (ch == 'p' || ch == 'P')
		{
			/* the selection is replaced; the register stays as it was */
			r = remove_range (ed, first, last);

			if (r == ML_OK)
			{
				r = paste_lines (ed, first, 1);
			}

			ed->state = ML_STATE_NORMAL;
		}

		else if (ch == ':')
		{
			r = begin_command (ed);

			if (r == ML_OK)
			{
				r = ml_str_appendf (
					&ed->edit.text,
					"%u,%u",
					(uint32_t)(first + 1),
					(uint32_t)(last + 1)
				);
				ed->edit.cursor = ed->edit.text.len;
			}
		}

		if (ed->pending == 0)
		{
			ed->count = 0;
		}
	}

	return r;
}

static MlResult normal_key (MlEditor *ed, MlKey key)
{
	MlResult r    = ML_OK;
	uint32_t ch   = ml_choose (
		key.code == ML_KEY_CHAR && !(key.mods & (ML_MOD_CTRL | ML_MOD_ALT)),
		key.cp,
		0
	);
	uint64_t reps = ml_choose (ed->count > 0, ed->count, 1);
	uint64_t at   = ed->session.cursor;

	if (ed->pending != 0)
	{
		uint32_t first = ed->pending;

		ed->pending = 0;
		r           = pending_key (ed, first, ch);
	}

	else if (ch >= '1' && ch <= '9')
	{
		ed->count = ed->count * 10 + (ch - '0');
	}

	else if (ch == '0' && ed->count > 0)
	{
		ed->count = ed->count * 10;
	}

	else if (is_domain_cycle (key))
	{
		r = cycle_domain (ed, key);
	}

	else if (is_alt_arrow (key, ML_KEY_DOWN) && ed->session.lines.len > 0)
	{
		r = shift_lines (ed, at, range_end (ed, 1), 1);
	}

	else if (is_alt_arrow (key, ML_KEY_UP) && ed->session.lines.len > 0)
	{
		r = shift_lines (ed, at, range_end (ed, 1), -1);
	}

	else if (ch == 'j' || key.code == ML_KEY_DOWN)
	{
		move_down (ed, reps);
	}

	else if (ch == 'k' || key.code == ML_KEY_UP)
	{
		move_up (ed, reps);
	}

	else if (ch == 'G')
	{
		ml_session_set_cursor (
			&ed->session,
			ml_choose (ed->count > 0, ed->count - 1, ed->session.lines.len)
		);
	}

	else if (
		   ch == 'd'
		|| ch == 'y'
		|| ch == 'g'
		|| ch == 'z'
		|| ch == ']'
		|| ch == '['
	)
	{
		ed->pending = ch;
	}

	else if (ch == 'v' || ch == 'V')
	{
		if (ed->session.lines.len > 0)
		{
			ed->state         = ML_STATE_VISUAL;
			ed->visual_anchor = ed->session.cursor;
		}
	}

	else if (ch == 'u')
	{
		r = undo_redo (ed, 0);
	}

	else if (
		   key.code == ML_KEY_CHAR
		&& key.cp == 'r'
		&& (key.mods & ML_MOD_CTRL)
	)
	{
		r = undo_redo (ed, 1);
	}

	else if (ch == 's')
	{
		r = toggle_steps (ed);
	}

	else if (ch == 'p')
	{
		r = paste_line (ed, 1);
	}

	else if (ch == 'P')
	{
		r = paste_line (ed, 0);
	}

	else if (key.code == ML_KEY_ENTER)
	{
		r = edit_cursor_line (ed);
	}

	else if (ch == 'i')
	{
		r = begin_line (
			ed,
			ML_STATE_INSERT,
			ML_LINE_MATH,
			ed->session.domain,
			line_below_cursor (ed),
			0,
			ml_slice ("", 0)
		);
	}

	else if (ch == 'n')
	{
		r = begin_line (
			ed,
			ML_STATE_NOTE,
			ML_LINE_NOTE,
			ed->session.domain,
			line_below_cursor (ed),
			0,
			ml_slice ("", 0)
		);
	}

	else if (ch == ':')
	{
		r = begin_command (ed);
	}

	/* a number belongs to the command after it; pending keys keep it */
	if (ed->pending == 0 && !(ch >= '0' && ch <= '9'))
	{
		ed->count = 0;
	}

	return r;
}

static MlSlice trim (MlSlice s)
{
	uint64_t start = 0;
	uint64_t end   = s.len;

	while (start < end && s.data [start] == ' ')
	{
		start++;
	}

	while (end > start && s.data [end - 1] == ' ')
	{
		end--;
	}

	return ml_slice_sub (s, start, end - start);
}

static MlResult save (MlEditor *ed, MlSlice path)
{
	MlResult r = ML_OK;

	if (path.len > 0)
	{
		r = ml_session_set_path (&ed->session, path);
	}

	if (r == ML_OK)
	{
		r = ml_mlog_save (&ed->session);
	}

	if (r == ML_OK)
	{
		r = set_message (ed, "written");
	}

	else
	{
		r = set_message (ed, "cannot write (give a path: :w name.mlog)");
	}

	return r;
}

/* :open path loads the file, or starts a new session for a missing one. */
static MlResult open_session (MlEditor *ed, MlSlice path, int32_t force)
{
	MlResult r = ML_OK;

	if (path.len == 0)
	{
		r = set_message (ed, "open: give a path");
	}

	else if (ed->session.dirty && !force)
	{
		r = set_message (ed, "unsaved changes (:w saves, :open! discards)");
	}

	else
	{
		MlResult loaded = ml_mlog_load (&ed->session, path);

		if (loaded == ML_ERR_IO)
		{
			ml_session_free (&ed->session);
			ml_session_init (&ed->session);
			r = ml_session_set_path (&ed->session, path);
		}

		else
		{
			r = loaded;
		}

		if (r == ML_OK)
		{
			set_domain (ed, ed->session.domain);
			r = set_message (ed, ml_choose (
				loaded == ML_OK,
				"opened",
				"new file"
			));
		}
	}

	return r;
}

static MlResult quit (MlEditor *ed, int32_t force)
{
	MlResult r = ML_OK;

	if (ed->session.dirty && !force)
	{
		r = set_message (ed, "unsaved changes (:w saves, :q! discards)");
	}

	else
	{
		ed->quit = 1;
	}

	return r;
}

static MlResult set_mode (MlEditor *ed, MlSlice name)
{
	MlResult       r = ML_OK;
	MlDomainResult d = ml_domain_from_name (name);

	if (d.result == ML_OK)
	{
		set_domain (ed, d.domain);
		r = set_message (ed, ml_domain_name (d.domain));
	}

	else
	{
		r = set_message (ed, "unknown domain");
	}

	return r;
}

static int32_t is_cmd (MlSlice cmd, const char *name)
{
	return ml_slice_equal (cmd, ml_slice_cstr (name));
}

/* A range of lines in a command: 3  3,5  %  .  $  (1 based in the text). */
typedef struct Range
{
	int32_t  given;
	uint64_t first; /* 0 based, inclusive */
	uint64_t last;
	uint64_t used;  /* bytes of the text it took */
} Range;

typedef struct Address
{
	int32_t  valid;
	uint64_t line;  /* 0 based; the address 0 is the top, as 0 */
	uint64_t after; /* the same address as "after this many lines" */
	uint64_t used;
} Address;

/* An address at the start of s: a number, '.' (cursor) or '$' (the end). */
static Address parse_address (const MlEditor *ed, MlSlice s)
{
	Address a =
	{
		.valid = 0,
		.line  = 0,
		.after = 0,
		.used  = 0
	};
	uint64_t n    = ed->session.lines.len;

	if (s.len > 0 && s.data [0] == '.')
	{
		a.valid = 1;
		a.used  = 1;
		a.after = ed->session.cursor + 1;
	}

	else if (s.len > 0 && s.data [0] == '$')
	{
		a.valid = 1;
		a.used  = 1;
		a.after = n;
	}

	else
	{
		while (
			   a.used < s.len
			&& a.used < 18
			&& s.data [a.used] >= '0'
			&& s.data [a.used] <= '9'
		)
		{
			a.after = a.after * 10 + (uint64_t)(s.data [a.used] - '0');
			a.used++;
			a.valid = 1;
		}
	}

	if (a.valid)
	{
		a.after = ml_choose (a.after > n, n, a.after);
		a.line  = ml_choose (a.after > 0, a.after - 1, 0);
	}

	return a;
}

static Range parse_range (const MlEditor *ed, MlSlice s)
{
	Range   rg =
	{
		.given = 0,
		.first = ed->session.cursor,
		.last  = ed->session.cursor,
		.used  = 0
	};
	Address a;

	if (s.len > 0 && s.data [0] == '%')
	{
		rg.given = 1;
		rg.used  = 1;
		rg.first = 0;
		rg.last  = ml_choose (
			ed->session.lines.len > 0,
			ed->session.lines.len - 1,
			0
		);
	}

	else
	{
		a = parse_address (ed, s);

		if (a.valid)
		{
			rg.given = 1;
			rg.first = a.line;
			rg.last  = a.line;
			rg.used  = a.used;

			if (a.used < s.len && s.data [a.used] == ',')
			{
				Address b = parse_address (
					ed,
					ml_slice_sub (s, a.used + 1, s.len - a.used - 1)
				);

				if (b.valid)
				{
					rg.last = b.line;
					rg.used = a.used + 1 + b.used;
				}
			}

			if (rg.last < rg.first)
			{
				uint64_t t = rg.first;

				rg.first = rg.last;
				rg.last  = t;
			}
		}
	}

	return rg;
}

/* The commands that work on a range of lines: d y m t and their long names. */
typedef enum RangeCommand
{
	RC_NONE = 0,
	RC_DELETE,
	RC_YANK,
	RC_MOVE,
	RC_COPY
} RangeCommand;

static RangeCommand range_command (MlSlice cmd)
{
	RangeCommand c = RC_NONE;

	c = ml_choose (is_cmd (cmd, "d") || is_cmd (cmd, "delete"), RC_DELETE, c);
	c = ml_choose (is_cmd (cmd, "y") || is_cmd (cmd, "yank"), RC_YANK, c);
	c = ml_choose (is_cmd (cmd, "m") || is_cmd (cmd, "move"), RC_MOVE, c);
	c = ml_choose (
		is_cmd (cmd, "t") || is_cmd (cmd, "co") || is_cmd (cmd, "copy"),
		RC_COPY,
		c
	);
	return c;
}

/* :[range]m dest moves the lines to after line dest. */
static MlResult move_to (MlEditor *ed, Range rg, MlSlice arg)
{
	MlResult r     = ML_OK;
	Address  dest  = parse_address (ed, arg);
	uint64_t n     = rg.last - rg.first + 1;
	uint64_t to    = rg.first;

	if (!dest.valid || dest.used != arg.len)
	{
		r = set_message (ed, "m needs a line number to move after");
	}

	else if (dest.after > rg.last + 1)
	{
		to = dest.after - n;
	}

	else if (dest.after <= rg.first)
	{
		to = dest.after;
	}

	if (r == ML_OK && dest.valid && dest.used == arg.len && to != rg.first)
	{
		r = ml_session_move (&ed->session, rg.first, rg.last, to);
	}

	if (r == ML_OK && dest.valid && dest.used == arg.len)
	{
		ml_session_set_cursor (&ed->session, to);
	}

	return r;
}

/* :[range]t dest puts copies of the lines after line dest. */
static MlResult copy_to (MlEditor *ed, Range rg, MlSlice arg)
{
	MlResult r    = ML_OK;
	Address  dest = parse_address (ed, arg);

	if (!dest.valid || dest.used != arg.len)
	{
		r = set_message (ed, "t needs a line number to copy after");
	}

	else
	{
		MlLineVec copies;

		MlLineVec_init (&copies);
		r = collect_lines (ed, &copies, rg.first, rg.last);

		if (r == ML_OK)
		{
			r = insert_lines (ed, &copies, dest.after);
		}

		if (r == ML_OK)
		{
			ml_session_set_cursor (&ed->session, dest.after);
		}

		lines_free (&copies);
	}

	return r;
}

/* d, y, m, t with the range they were given (the cursor line by default). */
static MlResult run_range_command (
	MlEditor     *ed,
	RangeCommand which,
	Range        rg,
	MlSlice      arg
)
{
	MlResult r = ML_OK;

	if (ed->session.lines.len == 0)
	{
		r = set_message (ed, "no lines");
	}

	else if (which == RC_DELETE)
	{
		r = delete_range (ed, rg.first, rg.last);
	}

	else if (which == RC_YANK)
	{
		r = yank_range (ed, rg.first, rg.last);
	}

	else if (which == RC_MOVE)
	{
		r = move_to (ed, rg, arg);
	}

	else
	{
		r = copy_to (ed, rg, arg);
	}

	return r;
}

/* Runs the command typed after ':'. */
static MlResult run_command (MlEditor *ed)
{
	MlResult r     = ML_OK;
	MlSlice  full  = trim (ml_str_view (ed->edit.text));
	Range    rg    = parse_range (ed, full);
	MlSlice  line  = ml_slice_sub (full, rg.used, full.len - rg.used);
	uint64_t split = 0;
	MlSlice  cmd;
	MlSlice  arg;

	while (split < line.len && line.data [split] != ' ')
	{
		split++;
	}

	/* m5 and t$ : a one letter command may touch its address */
	if (
		   line.len > 1
		&& (line.data [0] == 'm' || line.data [0] == 't')
		&& (
			   (line.data [1] >= '0' && line.data [1] <= '9')
			|| line.data [1] == '.'
			|| line.data [1] == '$'
		)
	)
	{
		split = 1;
	}

	cmd = ml_slice_sub (line, 0, split);
	arg = trim (ml_slice_sub (line, split, line.len - split));

	if (is_cmd (cmd, "q") || is_cmd (cmd, "quit"))
	{
		r = quit (ed, 0);
	}

	else if (is_cmd (cmd, "q!"))
	{
		r = quit (ed, 1);
	}

	else if (is_cmd (cmd, "w") || is_cmd (cmd, "write"))
	{
		r = save (ed, arg);
	}

	else if (is_cmd (cmd, "wq") || is_cmd (cmd, "x"))
	{
		r = save (ed, arg);

		if (r == ML_OK && !ed->session.dirty)
		{
			ed->quit = 1;
		}
	}

	else if (is_cmd (cmd, "open") || is_cmd (cmd, "e"))
	{
		r = open_session (ed, arg, 0);
	}

	else if (is_cmd (cmd, "open!") || is_cmd (cmd, "e!"))
	{
		r = open_session (ed, arg, 1);
	}

	else if (is_cmd (cmd, "mode"))
	{
		r = set_mode (ed, arg);
	}

	else if (is_cmd (cmd, "steps"))
	{
		r = toggle_steps (ed);
	}

	else if (is_cmd (cmd, "undo") || is_cmd (cmd, "u"))
	{
		r = undo_redo (ed, 0);
	}

	else if (is_cmd (cmd, "redo") || is_cmd (cmd, "red"))
	{
		r = undo_redo (ed, 1);
	}

	else if (range_command (cmd) != RC_NONE)
	{
		r = run_range_command (ed, range_command (cmd), rg, arg);
	}

	else if (is_cmd (cmd, "pga"))
	{
		r = set_mode (ed, ml_choose (
			is_cmd (arg, "2"),
			ml_slice_cstr ("pga2"),
			ml_choose (
				is_cmd (arg, "3"),
				ml_slice_cstr ("pga3"),
				arg
			)
		));
	}

	else if (rg.given && cmd.len == 0)
	{
		/* :12 goes to line 12, :3,5 to line 5 */
		ml_session_set_cursor (&ed->session, rg.last);
	}

	else if (cmd.len > 0)
	{
		r = set_message (ed, "unknown command");
	}

	return r;
}

MlCompletion ml_editor_completion (const MlEditor *ed)
{
	MlCompletion c = ml_completion_none ();

	if (
		   ed->state == ML_STATE_COMMAND
		&& ed->edit.cursor == ed->edit.text.len
	)
	{
		c = ml_complete_command (ml_str_view (ed->edit.text));
	}

	return c;
}

/* Types the ghost text of the completion, if there is one. */
static MlResult accept_completion (MlEditor *ed)
{
	MlResult     r = ML_OK;
	MlCompletion c = ml_editor_completion (ed);

	if (c.ghost < ML_MAX_CANDIDATES)
	{
		r = ml_lineedit_insert (&ed->edit, c.ghost_text);

		if (r == ML_OK && c.append_space)
		{
			r = ml_lineedit_insert_cp (&ed->edit, ' ');
		}
	}

	return r;
}

static MlResult command_key (MlEditor *ed, MlKey key)
{
	MlResult r = ML_OK;

	if (
		   key.code == ML_KEY_ESCAPE
		|| (
			   key.code == ML_KEY_BACKSPACE
			&& key.mods == 0
			&& ed->edit.text.len == 0
		)
	)
	{
		ed->state = ML_STATE_NORMAL;
		ml_lineedit_clear (&ed->edit);
	}

	else if (key.code == ML_KEY_ENTER)
	{
		r         = run_command (ed);
		ed->state = ML_STATE_NORMAL;
		ml_lineedit_clear (&ed->edit);
	}

	else if (
		   key.code == ML_KEY_TAB
		|| (
			   key.code == ML_KEY_RIGHT
			&& key.mods == 0
			&& ml_editor_completion (ed).ghost < ML_MAX_CANDIDATES
		)
	)
	{
		r = accept_completion (ed);
	}

	else
	{
		r = ml_lineedit_handle_key (&ed->edit, key).result;
	}

	return r;
}

MlResult ml_editor_refresh (MlEditor *ed)
{
	MlResult r = ML_OK;

	if (ed->results_rev != ed->session.revision)
	{
		r = ml_results_update (&ed->results, &ed->session, 0);
		ed->results_rev = ed->session.revision;
		ed->steps_open  = 0;
	}

	return r;
}

MlResult ml_editor_handle_key (MlEditor *ed, MlKey key)
{
	MlResult r = ML_OK;

	ml_str_clear (&ed->message);
	ml_session_checkpoint (&ed->session);

	if (ed->state == ML_STATE_NORMAL)
	{
		r = normal_key (ed, key);
	}

	else if (ed->state == ML_STATE_VISUAL)
	{
		r = visual_key (ed, key);
	}

	else if (ed->state == ML_STATE_COMMAND)
	{
		r = command_key (ed, key);
	}

	else
	{
		r = text_key (ed, key);
	}

	if (r == ML_OK)
	{
		r = ml_editor_refresh (ed);
	}

	if (ed->state == ML_STATE_NORMAL || ed->state == ML_STATE_VISUAL)
	{
		normalize_cursor (ed);
	}

	return r;
}

MlResult ml_editor_status (MlStr *out, const MlEditor *ed)
{
	MlResult r = ML_OK;

	if (ed->state == ML_STATE_COMMAND)
	{
		r = ml_str_append (out, ml_slice_cstr (":"));

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_str_view (ed->edit.text));
		}
	}

	else
	{
		r = ml_str_appendf (
			out,
			"-- %s -- %s>",
			ml_input_state_name (ed->state),
			ml_domain_name (ml_choose (
				ed->state == ML_STATE_INSERT,
				ed->edit_domain,
				ed->session.domain
			))
		);

		if (r == ML_OK && ed->message.len > 0)
		{
			r = ml_str_append (out, ml_slice_cstr ("  "));
		}

		if (r == ML_OK && ed->message.len > 0)
		{
			r = ml_str_append (out, ml_str_view (ed->message));
		}
	}

	return r;
}
