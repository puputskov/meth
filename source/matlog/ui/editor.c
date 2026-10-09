#include <matlog/ui/editor.h>

#include <matlog/core/utf8.h>

static const char *const state_names [ML_STATE_COUNT] =
{
	[ML_STATE_MATH]    = "MATH",
	[ML_STATE_NOTE]    = "NOTE",
	[ML_STATE_COMMAND] = "COMMAND",
	[ML_STATE_SELECT]  = "SELECT"
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
	ed->anchor        = 0;
	ed->state         = ML_STATE_NOTE;
	ed->selecting     = 0;
	ed->sel_anchor    = 0;
	ed->sel_line      = 0;
	ed->yank_text     = 0;
	ed->edit_index    = 0;
	ed->edit_existing = 0;
	ed->edit_kind     = ML_LINE_MATH;
	ed->edit_domain   = ML_DOMAIN_ALGEBRA;
	ed->picking       = 0;
	ed->pick          = ML_DOMAIN_ALGEBRA;
	ed->quit          = 0;
	ed->page_rows     = 10;
}

void ml_editor_set_page (MlEditor *ed, uint64_t rows)
{
	ed->page_rows = ml_choose (rows > 0, rows, 1);
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

		if (existing)
		{
			ml_session_set_cursor (&ed->session, index);
		}

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
			ML_STATE_MATH,
			ML_LINE_MATH,
			ed->session.domain,
			ed->session.lines.len,
			0,
			ml_slice ("", 0)
		);
	}

	return r;
}

/* A new empty note at the end of the session. */
static MlResult new_note (MlEditor *ed)
{
	return begin_line (
		ed,
		ML_STATE_NOTE,
		ML_LINE_NOTE,
		ed->session.domain,
		ed->session.lines.len,
		0,
		ml_slice ("", 0)
	);
}

MlResult ml_editor_begin_note (MlEditor *ed)
{
	MlResult r = ml_editor_refresh (ed);

	ml_str_clear (&ed->message);

	if (r == ML_OK)
	{
		r = new_note (ed);
	}

	return r;
}

/* Writes the typed line into the session and puts the cursor on it. */
static MlResult commit_line (MlEditor *ed)
{
	MlResult   r    = ML_OK;
	MlSlice    text = ml_str_view (ed->edit.text);
	MlLineKind kind = ed->edit_kind;

	if (ed->state == ML_STATE_MATH || ed->state == ML_STATE_NOTE)
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
		MlLineView old = ml_session_get (&ed->session, ed->edit_index);

		/* an unchanged line is no change: nothing to undo */
		if (
			   old.kind != kind
			|| old.domain != ed->edit_domain
			|| !ml_slice_equal (old.text, text)
		)
		{
			r = ml_session_replace (
				&ed->session,
				ed->edit_index,
				kind,
				ed->edit_domain,
				text
			);
		}
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

/* The domain of the line being typed (and of the next ones), or of a note's
   neighbours. */
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

	else
	{
		/* a note has no domain: the keys choose the one for new lines */
		MlDomain next = next_domain (ed->session.domain, key);

		set_domain (ed, next);
		ed->edit_domain = next;
		r = set_message (ed, ml_domain_name (next));
	}

	return r;
}

/* Puts the cursor of the line being edited at the column col (or before). */
static void edit_to_column (MlEditor *ed, uint64_t col)
{
	ml_lineedit_home (&ed->edit);

	while (
		   ed->edit.cursor < ed->edit.text.len
		&& ml_lineedit_cursor_col (&ed->edit) < col
	)
	{
		ml_lineedit_right (&ed->edit);
	}
}

/* Selects the line idx (a folded block whole): no line is being edited. */
static void enter_select (MlEditor *ed, uint64_t idx)
{
	ml_lineedit_clear (&ed->edit);
	ml_session_set_cursor (&ed->session, idx);
	ed->anchor    = ed->session.cursor;
	ed->state     = ML_STATE_SELECT;
	ed->selecting = 0;
	ed->picking   = 0;
}

/* The line at idx, or a line of kind ML_LINE_NONE (also for includes). */
static MlLineView line_at (const MlEditor *ed, uint64_t idx)
{
	MlLineView v = ml_session_get (&ed->session, idx);

	if (v.kind == ML_LINE_INCLUDE)
	{
		v.kind = ML_LINE_NONE;
	}

	return v;
}

/* Where the cursor lands in the line that is moved to. */
typedef enum LineSpot
{
	LINE_SPOT_COLUMN,
	LINE_SPOT_START,
	LINE_SPOT_END
} LineSpot;

/* The input state that edits a line of this kind. */
static MlInputState state_for (MlLineKind kind)
{
	return ml_choose (kind == ML_LINE_NOTE, ML_STATE_NOTE, ML_STATE_MATH);
}

/* Starts editing the line idx of the session, with its own kind. */
static MlResult edit_line_at (MlEditor *ed, uint64_t idx, MlSlice text)
{
	MlLineView v = ml_session_get (&ed->session, idx);

	return begin_line (ed, state_for (v.kind), v.kind, v.domain, idx, 1, text);
}

/*
 * Up (dir < 0) and Down (dir > 0): the neighbouring line is edited next, the
 * column is kept. Nothing happens at the first and the last line.
 */
static MlResult line_move (MlEditor *ed, int32_t dir, LineSpot spot)
{
	MlResult   r      = ML_OK;
	int32_t    stored = ed->edit_existing || ed->edit.text.len > 0;
	int32_t    exists = ed->edit_existing;
	uint64_t   col    = ml_lineedit_cursor_col (&ed->edit);
	uint64_t   cur    = ed->edit_index;
	int32_t    can    = ml_choose (dir < 0, cur > 0, 1);
	uint64_t   next   = ml_choose (dir < 0, cur - 1, cur + exists);
	MlLineView v      = line_at (ed, ml_choose (can, next, 0));
	MlStr      text;

	ml_str_init (&text);

	if (can && v.kind != ML_LINE_NONE)
	{
		r = ml_str_append (&text, v.text);

		/* a new line that gets stored pushes the lines below it down */
		if (r == ML_OK && stored)
		{
			r = commit_line (ed);
		}

		if (r == ML_OK)
		{
			uint64_t at = next + (dir > 0 && !exists && stored);
			MlBlock  tb = ml_session_block (&ed->session, at);

			/* a folded block is not edited, it is selected */
			if (tb.folded && tb.last > tb.first)
			{
				enter_select (ed, tb.first);
			}

			else
			{
				r = edit_line_at (ed, at, ml_str_view (text));
			}
		}

		if (r == ML_OK && ed->state == ML_STATE_SELECT)
		{
			col = 0;
		}

		else if (r == ML_OK && spot == LINE_SPOT_COLUMN)
		{
			edit_to_column (ed, col);
		}

		else if (r == ML_OK && spot == LINE_SPOT_START)
		{
			ml_lineedit_home (&ed->edit);
		}
	}

	ml_str_free (&text);

	return r;
}

/* Lines of the same sort (note or math) can be joined. */
static int32_t joinable (MlLineKind a, MlLineKind b)
{
	return (
		   a != ML_LINE_NONE
		&& b != ML_LINE_NONE
		&& (
			   a == b
			|| a == ML_LINE_BLANK
			|| b == ML_LINE_BLANK
		)
	);
}

/* The kind a line joined from a and b has. */
static MlLineKind joined_kind (MlLineKind a, MlLineKind b, uint64_t len)
{
	return ml_choose (
		len == 0,
		ML_LINE_BLANK,
		ml_choose (a == ML_LINE_BLANK, b, a)
	);
}

/*
 * Backspace at the start of a line (dir < 0) joins it with the previous
 * line, Delete at its end (dir > 0) joins the next line into it. The cursor
 * stays where the two met.
 */
static MlResult line_join (MlEditor *ed, int32_t dir)
{
	MlResult   r      = ml_rewriter_flush (&ed->rewriter, &ed->edit);
	MlStr      joined;
	MlLineView a;
	MlLineView b;
	MlLineKind kind   = ML_LINE_NONE;
	int32_t    done   = 0;
	uint64_t   at     = 0;
	uint64_t   target = ed->edit_index;
	int32_t    stored = ed->edit_existing || ed->edit.text.len > 0;

	ml_str_init (&joined);

	if (r == ML_OK && dir < 0 && ed->edit_index > 0)
	{
		a = line_at (ed, ed->edit_index - 1);
		b = (MlLineView)
		{
			.kind   = ed->edit_kind,
			.domain = ed->edit_domain,
			.text   = ml_str_view (ed->edit.text)
		};
		kind = joined_kind (a.kind, b.kind, a.text.len + b.text.len);

		/* an empty line goes back into the line before it, of any kind */
		if (
			   joinable (a.kind, b.kind)
			|| (a.kind != ML_LINE_NONE && b.text.len == 0)
		)
		{
			target = ed->edit_index - 1;
			at     = a.text.len;
			r      = ml_str_append (&joined, a.text);

			if (r == ML_OK)
			{
				r = ml_str_append (&joined, b.text);
			}

			if (r == ML_OK && ed->edit_existing)
			{
				r = ml_session_delete (&ed->session, ed->edit_index);
			}

			if (r == ML_OK)
			{
				r = ml_session_replace (
					&ed->session,
					target,
					kind,
					a.domain,
					ml_str_view (joined)
				);
				done = (r == ML_OK);
			}
		}
	}

	else if (r == ML_OK && dir > 0 && stored)
	{
		r      = commit_line (ed);
		target = ed->edit_index;
		a      = line_at (ed, target);
		b      = line_at (ed, target + 1);

		if (r == ML_OK && joinable (a.kind, b.kind))
		{
			kind = joined_kind (a.kind, b.kind, a.text.len + b.text.len);
			at   = a.text.len;
			r    = ml_str_append (&joined, a.text);

			if (r == ML_OK)
			{
				r = ml_str_append (&joined, b.text);
			}

			if (r == ML_OK)
			{
				r = ml_session_delete (&ed->session, target + 1);
			}

			if (r == ML_OK)
			{
				r = ml_session_replace (
					&ed->session,
					target,
					kind,
					ml_choose (a.kind == ML_LINE_BLANK, b.domain, a.domain),
					ml_str_view (joined)
				);
				done = (r == ML_OK);
			}
		}
	}

	if (r == ML_OK && done)
	{
		r = edit_line_at (ed, target, ml_str_view (joined));
		ed->edit.cursor = at;
	}

	ml_str_free (&joined);

	return r;
}

/*
 * Page Up and Page Down: a page of lines up or down, keeping the column. At
 * the first or the last line the cursor goes to the start or the end of it.
 */
static MlResult page_lines (MlEditor *ed, int32_t dir)
{
	MlResult r     = ML_OK;
	int32_t  moved = 0;

	for (uint64_t i = 0; i < ed->page_rows && r == ML_OK; i++)
	{
		int32_t can = ml_choose (
			dir < 0,
			ed->edit_index > 0,
			ed->edit_index + 1 < ed->session.lines.len
		);

		if (can)
		{
			r     = line_move (ed, dir, LINE_SPOT_COLUMN);
			moved = 1;
		}
	}

	if (r == ML_OK && !moved && dir < 0)
	{
		ml_lineedit_home (&ed->edit);
	}

	else if (r == ML_OK && !moved)
	{
		ml_lineedit_end (&ed->edit);
	}

	return r;
}

/*
 * The keys that cross the edge of a line: Up and Down, Left at the start,
 * Right at the end, Backspace at the start and Delete at the end.
 */
static MlEditResult line_edge_key (MlEditor *ed, MlKey key)
{
	MlEditResult res =
	{
		.result  = ML_OK,
		.handled = 0
	};
	int32_t      plain = key.mods == 0;
	int32_t      word  = key.mods == ML_MOD_CTRL;
	int32_t      start = ed->edit.cursor == 0;
	int32_t      end   = ed->edit.cursor == ed->edit.text.len;

	if (plain && key.code == ML_KEY_PAGE_UP)
	{
		res.result  = page_lines (ed, -1);
		res.handled = 1;
	}

	else if (plain && key.code == ML_KEY_PAGE_DOWN)
	{
		res.result  = page_lines (ed, 1);
		res.handled = 1;
	}

	else if (word && start && key.code == ML_KEY_LEFT)
	{
		res.result  = line_move (ed, -1, LINE_SPOT_END);
		res.handled = 1;
	}

	else if (word && end && key.code == ML_KEY_RIGHT)
	{
		res.result  = line_move (ed, 1, LINE_SPOT_START);
		res.handled = 1;
	}

	else if (word && start && key.code == ML_KEY_BACKSPACE)
	{
		res.result  = line_join (ed, -1);
		res.handled = 1;
	}

	else if (word && end && key.code == ML_KEY_DELETE)
	{
		res.result  = line_join (ed, 1);
		res.handled = 1;
	}

	else if (plain && key.code == ML_KEY_UP)
	{
		res.result  = line_move (ed, -1, LINE_SPOT_COLUMN);
		res.handled = 1;
	}

	else if (plain && key.code == ML_KEY_DOWN)
	{
		res.result  = line_move (ed, 1, LINE_SPOT_COLUMN);
		res.handled = 1;
	}

	else if (plain && start && key.code == ML_KEY_LEFT)
	{
		res.result  = line_move (ed, -1, LINE_SPOT_END);
		res.handled = 1;
	}

	else if (plain && end && key.code == ML_KEY_RIGHT)
	{
		res.result  = line_move (ed, 1, LINE_SPOT_START);
		res.handled = 1;
	}

	else if (plain && start && key.code == ML_KEY_BACKSPACE)
	{
		res.result  = line_join (ed, -1);
		res.handled = 1;
	}

	else if (plain && end && key.code == ML_KEY_DELETE)
	{
		res.result  = line_join (ed, 1);
		res.handled = 1;
	}

	return res;
}

/*
 * Enter: the text after the cursor becomes a new line below, of the same
 * kind; at the end of a math line the new line is a fresh one of the session
 * domain.
 */
static MlResult line_split_to (
	MlEditor   *ed,
	MlLineKind kind,
	MlDomain   domain
)
{
	MlResult r = ml_rewriter_flush (&ed->rewriter, &ed->edit);
	MlStr    tail;

	ml_str_init (&tail);

	if (r == ML_OK)
	{
		r = ml_str_append (
			&tail,
			ml_slice_sub (
				ml_str_view (ed->edit.text),
				ed->edit.cursor,
				ed->edit.text.len - ed->edit.cursor
			)
		);
	}

	if (r == ML_OK)
	{
		ed->edit.text.len = ed->edit.cursor;
		r = commit_line (ed);
	}

	if (r == ML_OK)
	{
		r = begin_line (
			ed,
			state_for (kind),
			kind,
			domain,
			ed->edit_index + 1,
			0,
			ml_str_view (tail)
		);
		ml_lineedit_home (&ed->edit);
	}

	ml_str_free (&tail);

	return r;
}

/*
 * Enter: a note goes on with a note. A math line goes on with a math line
 * of the same block, and an empty one ends the block: it stays as a blank
 * line and a note follows.
 */
static MlResult line_split (MlEditor *ed)
{
	int32_t note  = ed->state == ML_STATE_NOTE;
	int32_t empty = ed->edit.text.len == 0;

	return line_split_to (
		ed,
		ml_choose (note || empty, ML_LINE_NOTE, ML_LINE_MATH),
		ml_choose (
			note || empty || ed->edit.cursor < ed->edit.text.len,
			ed->edit_domain,
			ed->session.domain
		)
	);
}

/* Is it Ctrl+Enter, Shift+Enter or Ctrl+B: choose the block to start. */
static int32_t is_block_key (MlKey key)
{
	return (
		   (
			   key.code == ML_KEY_ENTER
			&& (key.mods & (ML_MOD_CTRL | ML_MOD_SHIFT))
		)
		|| (
			   key.code == ML_KEY_CHAR
			&& key.cp == 'b'
			&& (key.mods & ML_MOD_CTRL)
		)
	);
}

/* The domain starting with the letter after from, or from itself. */
static MlDomain domain_by_letter (MlDomain from, uint32_t ch)
{
	MlDomain found = from;
	int32_t  hit   = 0;

	for (uint32_t i = 1; i <= ML_DOMAIN_COUNT && !hit; i++)
	{
		MlDomain d    = (MlDomain)(((uint32_t)from + i) % ML_DOMAIN_COUNT);
		MlSlice  name = ml_slice_cstr (ml_domain_name (d));

		hit = name.len > 0 && (uint32_t)name.data [0] == ch;

		if (hit)
		{
			found = d;
		}
	}

	return found;
}

/* The keys while the block is chosen: move, pick by letter, Enter, Esc. */
static MlResult pick_key (MlEditor *ed, MlKey key)
{
	MlResult r = ML_OK;

	if (key.code == ML_KEY_ESCAPE)
	{
		ed->picking = 0;
	}

	else if (key.code == ML_KEY_LEFT || key.code == ML_KEY_UP)
	{
		ed->pick = (MlDomain)(((uint32_t)ed->pick + ML_DOMAIN_COUNT - 1)
			% ML_DOMAIN_COUNT);
	}

	else if (
		   key.code == ML_KEY_RIGHT
		|| key.code == ML_KEY_DOWN
		|| key.code == ML_KEY_TAB
	)
	{
		ed->pick = (MlDomain)(((uint32_t)ed->pick + 1) % ML_DOMAIN_COUNT);
	}

	else if (key.code == ML_KEY_CHAR && !(key.mods & ML_MOD_CTRL))
	{
		ed->pick = domain_by_letter (ed->pick, key.cp);
	}

	else if (key.code == ML_KEY_ENTER)
	{
		ed->picking = 0;
		set_domain (ed, ed->pick);

		if (ed->edit.text.len == 0)
		{
			/* an empty line becomes the first line of the block */
			ed->state       = ML_STATE_MATH;
			ed->edit_kind   = ML_LINE_MATH;
			ed->edit_domain = ed->pick;
		}

		else
		{
			r = line_split_to (ed, ML_LINE_MATH, ed->pick);
		}
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
	ed->yank_text = 0;

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
		ed->anchor  += to;
		ed->anchor  -= first;
	}

	return r;
}

static void set_domain (MlEditor *ed, MlDomain domain)
{
	ml_session_set_domain (&ed->session, domain);
	ml_rewriter_set_domain (&ed->rewriter, domain);
}

static MlResult edit_cursor_line (MlEditor *ed)
{
	MlResult   r = ML_OK;
	MlLineView v = ml_session_get (&ed->session, ed->session.cursor);

	if (v.kind == ML_LINE_NONE)
	{
		r = new_note (ed);
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
			state_for (v.kind),
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
/* Ctrl+Z is undo, Ctrl+Shift+Z and Ctrl+Y redo; 0 for another key. */
static int32_t undo_key (MlKey key)
{
	int32_t ctrl = key.code == ML_KEY_CHAR && (key.mods & ML_MOD_CTRL);

	return ml_choose (
		ctrl && (key.cp == 'z' || key.cp == 'Z'),
		ml_choose (key.mods & ML_MOD_SHIFT, 2, 1),
		ml_choose (ctrl && (key.cp == 'y' || key.cp == 'r'), 2, 0)
	);
}

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

/* The selected lines, first and last; a folded block is taken whole. */
static uint64_t selection_first (const MlEditor *ed)
{
	uint64_t at = ml_choose (
		ed->anchor < ed->session.cursor,
		ed->anchor,
		ed->session.cursor
	);
	MlBlock  b  = ml_session_block (&ed->session, at);

	return ml_choose (b.folded, b.first, at);
}

static uint64_t selection_last (const MlEditor *ed)
{
	uint64_t at = ml_choose (
		ed->anchor > ed->session.cursor,
		ed->anchor,
		ed->session.cursor
	);
	MlBlock  b  = ml_session_block (&ed->session, at);

	return ml_choose (b.folded, b.last, at);
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

	else if (is_cmd (cmd, "fold") || is_cmd (cmd, "unfold"))
	{
		r = fold_block (ed, is_cmd (cmd, "fold"));
	}

	else if (is_cmd (cmd, "foldall") || is_cmd (cmd, "unfoldall"))
	{
		fold_all (ed, is_cmd (cmd, "foldall"));
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
/*
 * Writes the typed line into the session, unless it is an empty new one, and
 * goes on editing it.
 */
static MlResult settle (MlEditor *ed)
{
	MlResult r       = ML_OK;
	int32_t  editing = ed->state == ML_STATE_MATH || ed->state == ML_STATE_NOTE;

	if (editing && (ed->edit_existing || ed->edit.text.len > 0))
	{
		r = commit_line (ed);

		if (r == ML_OK)
		{
			ed->edit_existing = 1;
		}
	}

	else if (editing)
	{
		ml_session_set_cursor (&ed->session, ed->edit_index);
	}

	return r;
}

MlResult ml_editor_commit (MlEditor *ed)
{
	MlResult r = settle (ed);

	if (r == ML_OK)
	{
		r = ml_editor_refresh (ed);
	}

	return r;
}

/*
 * Edits the cursor line (a new note when there is none). A folded block is
 * not edited, it is selected.
 */
static MlResult resume (MlEditor *ed)
{
	MlResult r = ML_OK;
	MlBlock  b;

	normalize_cursor (ed);
	b             = ml_session_block (&ed->session, ed->session.cursor);
	ed->selecting = 0;
	ed->picking   = 0;

	if (b.folded && b.last > b.first)
	{
		enter_select (ed, b.first);
	}

	else
	{
		r = edit_cursor_line (ed);
	}

	return r;
}

/* Opens the folded block of the cursor line, to edit it. */
static void unfold_cursor (MlEditor *ed)
{
	ml_session_set_folded (
		&ed->session,
		ml_session_block (&ed->session, ed->session.cursor).first,
		0
	);
}

/* A key with Ctrl (and not Alt) and this lower case letter. */
static int32_t is_ctrl_key (MlKey key, uint32_t letter)
{
	return (
		   key.code == ML_KEY_CHAR
		&& key.cp == letter
		&& (key.mods & ML_MOD_CTRL)
		&& !(key.mods & ML_MOD_ALT)
	);
}

MlTextRange ml_editor_selection (const MlEditor *ed)
{
	uint64_t cl    = ed->edit_index;
	uint64_t cb    = ed->edit.cursor;
	int32_t  first = (
		   ed->sel_line < cl
		|| (ed->sel_line == cl && ed->sel_anchor <= cb)
	);
	MlTextRange r =
	{
		.first_line = ml_choose (first, ed->sel_line, cl),
		.first_byte = ml_choose (first, ed->sel_anchor, cb),
		.last_line  = ml_choose (first, cl, ed->sel_line),
		.last_byte  = ml_choose (first, cb, ed->sel_anchor)
	};

	r.valid = (
		   ed->selecting
		&& (r.first_line != r.last_line || r.first_byte != r.last_byte)
		&& (ed->state == ML_STATE_MATH || ed->state == ML_STATE_NOTE)
	);

	return r;
}

/* The kind a stored line of this text has. */
static MlLineKind stored_kind (MlLineKind kind, uint64_t len)
{
	return ml_choose (
		len == 0 && kind == ML_LINE_MATH,
		ML_LINE_BLANK,
		kind
	);
}

/* Edits the line at idx with the cursor at the byte. */
static MlResult goto_pos (MlEditor *ed, uint64_t idx, uint64_t byte)
{
	MlResult r = ML_OK;
	MlStr    text;

	ml_str_init (&text);

	if (idx != ed->edit_index)
	{
		r = ml_str_append (&text, line_at (ed, idx).text);

		if (r == ML_OK)
		{
			r = settle (ed);
		}

		if (r == ML_OK)
		{
			r = edit_line_at (ed, idx, ml_str_view (text));
		}
	}

	ed->edit.cursor = ml_choose (
		byte < ed->edit.text.len,
		byte,
		ed->edit.text.len
	);
	ml_str_free (&text);

	return r;
}

/* Removes the selected text; the cursor is where it was. */
static MlResult delete_selection (MlEditor *ed)
{
	MlResult    r = settle (ed);
	MlTextRange s = ml_editor_selection (ed);

	if (r == ML_OK && s.valid && s.first_line == s.last_line)
	{
		r = ml_rewriter_flush (&ed->rewriter, &ed->edit);

		if (r == ML_OK)
		{
			r = ml_str_erase (
				&ed->edit.text,
				s.first_byte,
				s.last_byte - s.first_byte
			);
			ed->edit.cursor = s.first_byte;
		}
	}

	else if (
		   r == ML_OK
		&& s.valid
		&& s.first_byte == 0
		&& s.last_line + 1 == ed->session.lines.len
		&& s.last_byte == line_at (ed, s.last_line).text.len
	)
	{
		/* all the lines to the end of the text: they are removed */
		for (uint64_t i = s.last_line + 1; i > s.first_line && r == ML_OK; i--)
		{
			r = ml_session_delete (&ed->session, i - 1);
		}

		if (r == ML_OK)
		{
			ml_session_set_cursor (&ed->session, s.first_line);
			r = resume (ed);
		}
	}

	else if (r == ML_OK && s.valid)
	{
		MlLineView a      = line_at (ed, s.first_line);
		MlLineView b      = line_at (ed, s.last_line);
		MlStr      joined;
		MlLineKind kind   = ml_choose (s.first_byte == 0, b.kind, a.kind);
		MlDomain   domain = ml_choose (s.first_byte == 0, b.domain, a.domain);

		ml_str_init (&joined);
		r = ml_str_append (&joined, ml_slice_sub (a.text, 0, s.first_byte));

		if (r == ML_OK)
		{
			r = ml_str_append (
				&joined,
				ml_slice_sub (b.text, s.last_byte, b.text.len - s.last_byte)
			);
		}

		for (uint64_t i = s.last_line; i > s.first_line && r == ML_OK; i--)
		{
			r = ml_session_delete (&ed->session, i);
		}

		if (r == ML_OK)
		{
			r = ml_session_replace (
				&ed->session,
				s.first_line,
				stored_kind (kind, joined.len),
				domain,
				ml_str_view (joined)
			);
		}

		if (r == ML_OK)
		{
			r = edit_line_at (ed, s.first_line, ml_str_view (joined));
			ed->edit.cursor = s.first_byte;
		}

		ml_str_free (&joined);
	}

	ed->selecting = 0;

	return r;
}

/*
 * The selected text goes into the register. Whole lines (the selection starts
 * at the start of a line and ends at the start of a later one) keep their
 * kinds and are pasted as lines; other text is pasted inline.
 */
static MlResult copy_selection (MlEditor *ed)
{
	MlResult    r = settle (ed);
	MlTextRange s = ml_editor_selection (ed);
	int32_t     whole = (
		   s.first_byte == 0
		&& s.last_byte == 0
		&& s.last_line > s.first_line
	);

	lines_clear (&ed->yank);

	for (uint64_t l = s.first_line; l <= s.last_line && r == ML_OK; l++)
	{
		MlLineView v  = line_at (ed, l);
		uint64_t   lo = ml_choose (l == s.first_line, s.first_byte, 0);
		uint64_t   hi = ml_choose (l == s.last_line, s.last_byte, v.text.len);
		MlLine     piece =
		{
			.kind   = ml_choose (whole, v.kind, ML_LINE_NOTE),
			.domain = ml_choose (whole, v.domain, ML_DOMAIN_ALGEBRA),
			.text   =
			{
				.data = NULL,
				.len  = 0,
				.cap  = 0
			}
		};

		if (!(whole && l == s.last_line))
		{
			r = ml_str_append (&piece.text, ml_slice_sub (v.text, lo, hi - lo));

			if (r == ML_OK)
			{
				r = MlLineVec_push (&ed->yank, piece);
			}

			if (r != ML_OK)
			{
				ml_str_free (&piece.text);
			}
		}
	}

	ed->yank_text = !whole;

	return r;
}

/* Puts the register's text at the cursor, in place of the selection. */
static MlResult paste_text (MlEditor *ed)
{
	MlResult    r = ML_OK;
	MlStr       tail;
	uint64_t    n = ed->yank.len;

	ml_str_init (&tail);

	if (ml_editor_selection (ed).valid)
	{
		r = delete_selection (ed);
	}

	if (r == ML_OK)
	{
		r = ml_rewriter_flush (&ed->rewriter, &ed->edit);
	}

	if (r == ML_OK && n == 1)
	{
		r = ml_lineedit_insert (
			&ed->edit,
			ml_str_view (ed->yank.data [0].text)
		);
	}

	else if (r == ML_OK && n > 1)
	{
		MlLineKind kind   = ml_choose (
			ed->state == ML_STATE_NOTE,
			ML_LINE_NOTE,
			ML_LINE_MATH
		);
		MlDomain   domain = ed->edit_domain;
		uint64_t   at     = ed->edit_index;

		r = ml_str_append (
			&tail,
			ml_slice_sub (
				ml_str_view (ed->edit.text),
				ed->edit.cursor,
				ed->edit.text.len - ed->edit.cursor
			)
		);
		ed->edit.text.len = ed->edit.cursor;

		if (r == ML_OK)
		{
			r = ml_lineedit_insert (
				&ed->edit,
				ml_str_view (ed->yank.data [0].text)
			);
		}

		if (r == ML_OK)
		{
			r = commit_line (ed);
		}

		for (uint64_t i = 1; i < n && r == ML_OK; i++)
		{
			MlStr text;

			ml_str_init (&text);
			r = ml_str_append (&text, ml_str_view (ed->yank.data [i].text));

			if (r == ML_OK && i + 1 == n)
			{
				r = ml_str_append (&text, ml_str_view (tail));
			}

			if (r == ML_OK)
			{
				r = ml_session_insert (
					&ed->session,
					at + i,
					stored_kind (kind, text.len),
					domain,
					ml_str_view (text)
				);
			}

			if (r == ML_OK && i + 1 == n)
			{
				uint64_t cursor = ed->yank.data [i].text.len;

				r = edit_line_at (ed, at + i, ml_str_view (text));
				ed->edit.cursor = cursor;
			}

			ml_str_free (&text);
		}
	}

	ml_str_free (&tail);

	return r;
}

/* The keys that work on the whole line being edited, not on its text. */
static MlResult row_copy (MlEditor *ed)
{
	MlResult r = ML_OK;

	if (ml_editor_selection (ed).valid)
	{
		r = copy_selection (ed);
	}

	else
	{
		r = settle (ed);
	}

	if (r == ML_OK && !ml_editor_selection (ed).valid && ed->edit_existing)
	{
		r = yank_range (ed, ed->session.cursor, range_end (ed, 1));
	}

	if (r == ML_OK)
	{
		r = set_message (ed, "copied");
	}

	return r;
}

static MlResult row_cut (MlEditor *ed)
{
	MlResult r = ML_OK;

	if (ml_editor_selection (ed).valid)
	{
		r = copy_selection (ed);

		if (r == ML_OK)
		{
			r = delete_selection (ed);
		}
	}

	else
	{
		r = settle (ed);

		if (r == ML_OK && ed->edit_existing)
		{
			r = delete_range (ed, ed->session.cursor, range_end (ed, 1));
		}

		if (r == ML_OK && ed->edit_existing)
		{
			r = resume (ed);
		}
	}

	return r;
}

static MlResult row_paste (MlEditor *ed)
{
	MlResult r = ML_OK;

	if (ed->yank.len == 0)
	{
		r = set_message (ed, "nothing to paste");
	}

	else if (ed->yank_text)
	{
		r = paste_text (ed);
	}

	else
	{
		MlTextRange sel = ml_editor_selection (ed);
		int32_t     in  = sel.valid && sel.first_byte == 0;

		if (sel.valid)
		{
			r = delete_selection (ed);
		}

		if (r == ML_OK)
		{
			r = settle (ed);
		}

		/* lines replace whole lines, else they go below the cursor line */
		if (r == ML_OK)
		{
			r = paste_lines (
				ed,
				ml_choose (
					in,
					ml_choose (
						sel.first_line < ed->session.lines.len,
						sel.first_line,
						ed->session.lines.len
					),
					ml_choose (
						ed->edit_existing,
						line_below_cursor (ed),
						ed->edit_index
					)
				),
				1
			);
		}

		if (r == ML_OK)
		{
			r = resume (ed);
		}
	}

	return r;
}

/* Alt+Up and Alt+Down: the line (the block, when folded) moves. */
static uint64_t moved_index (
	uint64_t x,
	uint64_t first,
	uint64_t last,
	int64_t  d
)
{
	uint64_t n  = last - first + 1;
	uint64_t at = x;

	if (x >= first && x <= last)
	{
		at = (uint64_t)((int64_t)x + d);
	}

	else if (d < 0 && x >= first - (uint64_t)(-d) && x < first)
	{
		at = x + n;
	}

	else if (d > 0 && x > last && x <= last + (uint64_t)d)
	{
		at = x - n;
	}

	return at;
}

static MlResult row_move (MlEditor *ed, int64_t delta)
{
	MlResult    r    = ML_OK;
	MlTextRange sel  = ml_editor_selection (ed);
	uint64_t    col  = ml_lineedit_cursor_col (&ed->edit);
	uint64_t    pos  = ed->edit.cursor;
	int32_t     had  = sel.valid;
	/* a selection that ends at the start of a line leaves that line out */
	int32_t     excl = (
		   had
		&& sel.last_byte == 0
		&& sel.last_line > sel.first_line
	);
	uint64_t    anchor_line = ed->sel_line;
	uint64_t    anchor_byte = ed->sel_anchor;

	r = settle (ed);

	if (r == ML_OK && ed->edit_existing)
	{
		uint64_t first = ml_choose (had, sel.first_line, ed->session.cursor);
		uint64_t last  = ml_choose (had, sel.last_line, range_end (ed, 1));
		uint64_t n;
		uint64_t room;
		int64_t  d;

		last = ml_choose (excl, last - 1, last);
		n    = last - first + 1;
		room = ed->session.lines.len - n;

		if (delta < 0)
		{
			d = -(int64_t)ml_choose (
				(uint64_t)(-delta) > first,
				first,
				(uint64_t)(-delta)
			);
		}

		else
		{
			d = (int64_t)ml_choose (
				first + (uint64_t)delta > room,
				room - first,
				(uint64_t)delta
			);
		}

		if (d != 0)
		{
			r = ml_session_move (
				&ed->session,
				first,
				last,
				(uint64_t)((int64_t)first + d)
			);
		}

		if (r == ML_OK)
		{
			uint64_t end   = ed->session.lines.len - 1;
			uint64_t line  = ed->edit_index;
			uint64_t from  = anchor_line;
			int32_t  limit = 0;

			line = ml_choose (
				excl && line == last + 1 && pos == 0,
				(uint64_t)((int64_t)line + d),
				moved_index (line, first, last, d)
			);
			from = ml_choose (
				excl && from == last + 1 && anchor_byte == 0,
				(uint64_t)((int64_t)from + d),
				moved_index (from, first, last, d)
			);

			/* the start of a line past the end is the end of the text */
			if (line > end)
			{
				line  = end;
				limit = 1;
			}

			ml_session_set_cursor (&ed->session, line);
			r = edit_line_at (ed, line, line_at (ed, line).text);

			if (r == ML_OK && had)
			{
				ed->selecting   = 1;
				ed->sel_line    = ml_choose (from > end, end, from);
				ed->sel_anchor  = anchor_byte;
				ed->edit.cursor = ml_choose (limit, ed->edit.text.len, pos);
			}

			else if (r == ML_OK)
			{
				edit_to_column (ed, col);
			}
		}
	}

	return r;
}

/* Alt+Enter: folds the block of the line, which is then selected. */
static MlResult row_fold (MlEditor *ed)
{
	MlResult r = settle (ed);

	if (r == ML_OK && ed->edit_existing)
	{
		r = fold_block (ed, 2);

		if (r == ML_OK && ml_session_block (
			&ed->session,
			ed->session.cursor
		).folded)
		{
			enter_select (ed, ed->session.cursor);
		}
	}

	return r;
}

/* Ctrl+A: all the text. */
static MlResult select_all (MlEditor *ed)
{
	MlResult r = settle (ed);

	if (r == ML_OK && ed->session.lines.len > 0 && ed->edit_existing)
	{
		uint64_t last = ed->session.lines.len - 1;

		r = goto_pos (ed, last, line_at (ed, last).text.len);

		if (r == ML_OK)
		{
			ed->selecting  = 1;
			ed->sel_line   = 0;
			ed->sel_anchor = 0;
		}
	}

	return r;
}

/* Ctrl+L: the line, with its line break. */
static MlResult select_line (MlEditor *ed)
{
	MlResult r = settle (ed);

	if (r == ML_OK && ed->edit_existing)
	{
		uint64_t at   = ed->edit_index;
		int32_t  more = at + 1 < ed->session.lines.len;

		ed->selecting  = 1;
		ed->sel_line   = at;
		ed->sel_anchor = 0;
		r = goto_pos (
			ed,
			ml_choose (more, at + 1, at),
			ml_choose (more, 0, ed->edit.text.len)
		);
	}

	return r;
}

/*
 * Shift with an arrow, Home or End moves the cursor and keeps the other end
 * of the selection where it was; Ctrl too moves by words. Past the first and
 * the last line Up and Down go to the start and the end of the text.
 */
static MlResult select_move (MlEditor *ed, MlKey key)
{
	MlResult r     = ML_OK;
	int32_t  word  = (key.mods & ML_MOD_CTRL) != 0;
	int32_t  above = ed->edit_index > 0;
	int32_t  below = ed->edit_index + 1 < ed->session.lines.len;

	if (!ed->selecting)
	{
		r = settle (ed);

		if (r == ML_OK && ed->edit_existing)
		{
			ed->selecting  = 1;
			ed->sel_line   = ed->edit_index;
			ed->sel_anchor = ed->edit.cursor;
		}
	}

	if (r == ML_OK && key.code == ML_KEY_LEFT && ed->edit.cursor > 0)
	{
		if (word)
		{
			ml_lineedit_word_left (&ed->edit);
		}

		else
		{
			ml_lineedit_left (&ed->edit);
		}
	}

	else if (r == ML_OK && key.code == ML_KEY_LEFT && above)
	{
		r = line_move (ed, -1, LINE_SPOT_END);
	}

	else if (
		   r == ML_OK
		&& key.code == ML_KEY_RIGHT
		&& ed->edit.cursor < ed->edit.text.len
	)
	{
		if (word)
		{
			ml_lineedit_word_right (&ed->edit);
		}

		else
		{
			ml_lineedit_right (&ed->edit);
		}
	}

	else if (r == ML_OK && key.code == ML_KEY_RIGHT && below)
	{
		r = line_move (ed, 1, LINE_SPOT_START);
	}

	else if (r == ML_OK && key.code == ML_KEY_UP)
	{
		if (above)
		{
			r = line_move (ed, -1, LINE_SPOT_COLUMN);
		}

		else
		{
			ml_lineedit_home (&ed->edit);
		}
	}

	else if (r == ML_OK && key.code == ML_KEY_DOWN)
	{
		if (below)
		{
			r = line_move (ed, 1, LINE_SPOT_COLUMN);
		}

		else
		{
			ml_lineedit_end (&ed->edit);
		}
	}

	else if (
		   r == ML_OK
		&& (key.code == ML_KEY_PAGE_UP || key.code == ML_KEY_PAGE_DOWN)
	)
	{
		MlKey step =
		{
			.code = ml_choose (
				key.code == ML_KEY_PAGE_UP,
				ML_KEY_UP,
				ML_KEY_DOWN
			),
			.cp   = 0,
			.mods = ML_MOD_SHIFT
		};

		for (uint64_t i = 0; i < ed->page_rows && r == ML_OK; i++)
		{
			r = select_move (ed, step);
		}
	}

	else if (r == ML_OK && key.code == ML_KEY_HOME)
	{
		ml_lineedit_home (&ed->edit);
	}

	else if (r == ML_OK && key.code == ML_KEY_END)
	{
		ml_lineedit_end (&ed->edit);
	}

	return r;
}

static MlResult row_goto (MlEditor *ed, int32_t last)
{
	MlResult r = settle (ed);

	if (r == ML_OK)
	{
		ml_session_set_cursor (
			&ed->session,
			ml_choose (last, ed->session.lines.len, 0)
		);
		r = resume (ed);
	}

	return r;
}

static MlResult row_block (MlEditor *ed, int32_t forward)
{
	MlResult r = settle (ed);

	if (r == ML_OK)
	{
		jump_block (ed, forward);
		r = resume (ed);
	}

	return r;
}

static MlResult row_command (MlEditor *ed)
{
	MlTextRange sel = ml_editor_selection (ed);
	MlResult    r   = settle (ed);

	if (r == ML_OK)
	{
		r = begin_command (ed);
	}

	/* several lines selected: the command line starts with their range */
	if (r == ML_OK && sel.valid && sel.first_line != sel.last_line)
	{
		r = ml_str_appendf (
			&ed->edit.text,
			"%u,%u",
			(uint32_t)(sel.first_line + 1),
			(uint32_t)(sel.last_line + 1 - (sel.last_byte == 0))
		);
		ed->edit.cursor = ed->edit.text.len;
	}

	return r;
}

/* Ctrl+S and Ctrl+Q */
static MlResult row_file (MlEditor *ed, int32_t leave)
{
	MlResult r = settle (ed);

	if (r == ML_OK && leave)
	{
		r = quit (ed, 0);
	}

	else if (r == ML_OK)
	{
		r = save (ed, ml_slice ("", 0));
	}

	return r;
}

static MlResult row_steps (MlEditor *ed)
{
	MlResult r = settle (ed);

	if (r == ML_OK)
	{
		r = toggle_steps (ed);
	}

	/* the steps are drawn under a line that is not being edited */
	if (r == ML_OK && ed->steps_open)
	{
		enter_select (ed, ed->session.cursor);
	}

	return r;
}

/* The keys of free form editing that leave the text of the line alone. */
static MlEditResult row_key (MlEditor *ed, MlKey key)
{
	MlEditResult res =
	{
		.result  = ML_OK,
		.handled = 1
	};
	int32_t      ctrl = (key.mods & ML_MOD_CTRL) != 0;
	int32_t      alt  = (key.mods & ML_MOD_ALT) != 0;

	if (key.code == ML_KEY_DOWN && alt && !ctrl)
	{
		res.result = row_move (ed, 1);
	}

	else if (key.code == ML_KEY_UP && alt && !ctrl)
	{
		res.result = row_move (ed, -1);
	}

	else if (key.code == ML_KEY_ENTER && alt)
	{
		res.result = row_fold (ed);
	}

	else if (key.code == ML_KEY_DOWN && ctrl && !alt)
	{
		res.result = row_block (ed, 1);
	}

	else if (key.code == ML_KEY_UP && ctrl && !alt)
	{
		res.result = row_block (ed, 0);
	}

	else if (key.code == ML_KEY_HOME && ctrl)
	{
		res.result = row_goto (ed, 0);
	}

	else if (key.code == ML_KEY_END && ctrl)
	{
		res.result = row_goto (ed, 1);
	}

	else if (is_ctrl_key (key, 'l'))
	{
		res.result = select_line (ed);
	}

	else if (is_ctrl_key (key, 'a'))
	{
		res.result = select_all (ed);
	}

	else if (is_ctrl_key (key, 'c'))
	{
		res.result = row_copy (ed);
	}

	else if (is_ctrl_key (key, 'x'))
	{
		res.result = row_cut (ed);
	}

	else if (is_ctrl_key (key, 'v'))
	{
		res.result = row_paste (ed);
	}

	else if (is_ctrl_key (key, 'p'))
	{
		res.result = row_command (ed);
	}

	else if (is_ctrl_key (key, 's'))
	{
		res.result = row_file (ed, 0);
	}

	else if (is_ctrl_key (key, 'q'))
	{
		res.result = row_file (ed, 1);
	}

	else if (key.code == ML_KEY_F3)
	{
		res.result = row_steps (ed);
	}

	else
	{
		res.handled = 0;
	}

	return res;
}

/* Does the key keep a selection going, or act on it? */
static int32_t is_selection_key (MlKey key)
{
	int32_t move = (
		   (key.mods & ML_MOD_SHIFT)
		&& (
			   key.code == ML_KEY_LEFT
			|| key.code == ML_KEY_RIGHT
			|| key.code == ML_KEY_UP
			|| key.code == ML_KEY_DOWN
			|| key.code == ML_KEY_HOME
			|| key.code == ML_KEY_END
			|| key.code == ML_KEY_PAGE_UP
			|| key.code == ML_KEY_PAGE_DOWN
		)
	);
	int32_t ctrl = (
		   is_ctrl_key (key, 'a')
		|| is_ctrl_key (key, 'c')
		|| is_ctrl_key (key, 'x')
		|| is_ctrl_key (key, 'v')
		|| is_ctrl_key (key, 'l')
		|| is_ctrl_key (key, 'p')
	);
	int32_t alt = (
		   (key.code == ML_KEY_UP || key.code == ML_KEY_DOWN)
		&& (key.mods & ML_MOD_ALT)
	);
	int32_t edit = (
		   key.code == ML_KEY_BACKSPACE
		|| (
			   (key.code == ML_KEY_LEFT || key.code == ML_KEY_RIGHT)
			&& key.mods == 0
		)
		|| key.code == ML_KEY_DELETE
		|| key.code == ML_KEY_ENTER
		|| (
			   key.code == ML_KEY_CHAR
			&& !(key.mods & (ML_MOD_CTRL | ML_MOD_ALT))
		)
	);

	return move || ctrl || alt || edit;
}

/*
 * Selecting text, as in other editors: Shift with the movement keys selects,
 * Backspace, Delete and typing replace the selection, plain Left and Right
 * go to its start and end.
 */
static MlEditResult text_select_key (MlEditor *ed, MlKey key)
{
	MlEditResult res =
	{
		.result  = ML_OK,
		.handled = 0
	};
	MlTextRange  s    = ml_editor_selection (ed);
	int32_t      move = (
		   (key.mods & ML_MOD_SHIFT)
		&& !(key.mods & ML_MOD_ALT)
		&& (
			   key.code == ML_KEY_LEFT
			|| key.code == ML_KEY_RIGHT
			|| key.code == ML_KEY_UP
			|| key.code == ML_KEY_DOWN
			|| key.code == ML_KEY_HOME
			|| key.code == ML_KEY_END
			|| key.code == ML_KEY_PAGE_UP
			|| key.code == ML_KEY_PAGE_DOWN
		)
	);

	if (move)
	{
		res.result  = select_move (ed, key);
		res.handled = 1;
	}

	else if (
		   s.valid
		&& key.mods == 0
		&& (key.code == ML_KEY_BACKSPACE || key.code == ML_KEY_DELETE)
	)
	{
		res.result  = delete_selection (ed);
		res.handled = 1;
	}

	else if (
		   s.valid
		&& key.mods == 0
		&& (key.code == ML_KEY_LEFT || key.code == ML_KEY_RIGHT)
	)
	{
		int32_t left = key.code == ML_KEY_LEFT;

		ed->selecting = 0;
		res.result    = goto_pos (
			ed,
			ml_choose (left, s.first_line, s.last_line),
			ml_choose (left, s.first_byte, s.last_byte)
		);
		res.handled   = 1;
	}

	else if (
		   s.valid
		&& key.code == ML_KEY_CHAR
		&& !(key.mods & (ML_MOD_CTRL | ML_MOD_ALT))
	)
	{
		/* typing replaces the selection, then goes into the line */
		res.result = delete_selection (ed);
	}

	else
	{
		ed->selecting = 0;
	}

	return res;
}

/* The keys that edit the text of a line. */
static MlResult text_edit_key (MlEditor *ed, MlKey key)
{
	MlResult     r   = ML_OK;
	MlEditResult res =
	{
		.result  = ML_OK,
		.handled = 0
	};

	if (key.code == ML_KEY_ENTER)
	{
		if (ml_editor_selection (ed).valid)
		{
			r = delete_selection (ed);
		}

		ed->selecting = 0;

		if (r == ML_OK)
		{
			r = line_split (ed);
		}
	}

	else if (is_domain_cycle (key))
	{
		r = cycle_line_domain (ed, key);
	}

	else
	{
		res = text_select_key (ed, key);

		if (!res.handled)
		{
			res = line_edge_key (ed, key);
		}

		if (!res.handled)
		{
			res = ml_rewriter_handle_key (&ed->rewriter, &ed->edit, key);
		}

		r = res.result;
	}

	return r;
}

/* A key while a math line or a note is edited (the cursor is in a line). */
static MlResult text_key (MlEditor *ed, MlKey key)
{
	MlResult     r   = ML_OK;
	MlEditResult res =
	{
		.result  = ML_OK,
		.handled = 0
	};

	if (!is_selection_key (key))
	{
		ed->selecting = 0;
	}

	if (ed->picking)
	{
		r = pick_key (ed, key);
	}

	else if (key.code == ML_KEY_ESCAPE)
	{
		/*
		 * Esc keeps what was typed; in a block it also leaves the block: a
		 * new note starts below it
		 */
		ed->selecting = 0;
		r             = settle (ed);

		if (r == ML_OK && ed->state == ML_STATE_MATH && ed->edit_existing)
		{
			r = begin_line (
				ed,
				ML_STATE_NOTE,
				ML_LINE_NOTE,
				ed->session.domain,
				ml_session_block (&ed->session, ed->edit_index).last + 1,
				0,
				ml_slice ("", 0)
			);
		}

		else if (r == ML_OK && ed->state == ML_STATE_MATH)
		{
			/* an empty new line of the block becomes the note */
			ed->state     = ML_STATE_NOTE;
			ed->edit_kind = ML_LINE_NOTE;
			ml_rewriter_set_domain (&ed->rewriter, ML_REWRITE_NOTES);
		}
	}

	else if (undo_key (key) != 0)
	{
		/* the line being typed is kept, then the step before it undone */
		r = settle (ed);

		if (r == ML_OK)
		{
			r = undo_redo (ed, undo_key (key) == 2);
		}

		if (r == ML_OK)
		{
			r = resume (ed);
		}
	}

	else if (is_block_key (key))
	{
		ed->picking = 1;
		ed->pick    = ed->session.domain;
	}

	else if (is_ctrl_key (key, 'n'))
	{
		/* Ctrl+N turns the line being edited into a note, or back */
		int32_t note = ed->state != ML_STATE_NOTE;

		ed->state     = ml_choose (note, ML_STATE_NOTE, ML_STATE_MATH);
		ed->edit_kind = ml_choose (note, ML_LINE_NOTE, ML_LINE_MATH);
		ml_rewriter_set_domain (
			&ed->rewriter,
			ml_choose (note, ML_REWRITE_NOTES, ed->edit_domain)
		);
	}

	else
	{
		res = row_key (ed, key);
		r   = res.result;

		if (!res.handled)
		{
			r = text_edit_key (ed, key);
		}
	}

	return r;
}

/* The keys while whole lines are selected. */
static MlResult select_key (MlEditor *ed, MlKey key)
{
	MlResult r     = ML_OK;
	uint64_t first = selection_first (ed);
	uint64_t last  = selection_last (ed);
	int32_t  ctrl  = (key.mods & ML_MOD_CTRL) != 0;
	int32_t  alt   = (key.mods & ML_MOD_ALT) != 0;
	int32_t  shift = (key.mods & ML_MOD_SHIFT) != 0;

	if (undo_key (key) != 0)
	{
		r = undo_redo (ed, undo_key (key) == 2);

		if (r == ML_OK)
		{
			r = resume (ed);
		}
	}

	else if (key.code == ML_KEY_DOWN && shift)
	{
		move_down (ed, 1);
	}

	else if (key.code == ML_KEY_UP && shift)
	{
		move_up (ed, 1);
	}

	else if (key.code == ML_KEY_PAGE_DOWN && shift)
	{
		move_down (ed, ed->page_rows);
	}

	else if (key.code == ML_KEY_PAGE_UP && shift)
	{
		move_up (ed, ed->page_rows);
	}

	else if (key.code == ML_KEY_PAGE_DOWN || key.code == ML_KEY_PAGE_UP)
	{
		int32_t down = key.code == ML_KEY_PAGE_DOWN;

		ml_session_set_cursor (&ed->session, ml_choose (down, last, first));

		if (down)
		{
			move_down (ed, ed->page_rows);
		}

		else
		{
			move_up (ed, ed->page_rows);
		}

		r = resume (ed);
	}

	else if (key.code == ML_KEY_DOWN && alt)
	{
		r = shift_lines (ed, first, last, 1);
	}

	else if (key.code == ML_KEY_UP && alt)
	{
		r = shift_lines (ed, first, last, -1);
	}

	else if (key.code == ML_KEY_ENTER && alt)
	{
		r = fold_block (ed, 2);

		if (
			   r == ML_OK
			&& !ml_session_block (&ed->session, ed->session.cursor).folded
		)
		{
			r = resume (ed);
		}
	}

	else if (key.code == ML_KEY_BACKSPACE || key.code == ML_KEY_DELETE)
	{
		r = remove_range (ed, first, last);

		if (r == ML_OK)
		{
			r = resume (ed);
		}
	}

	else if (key.code == ML_KEY_F3)
	{
		r = toggle_steps (ed);
	}

	else if (is_ctrl_key (key, 'c'))
	{
		r = yank_range (ed, first, last);

		if (r == ML_OK)
		{
			r = set_message (ed, "lines copied");
		}
	}

	else if (is_ctrl_key (key, 'x'))
	{
		r = delete_range (ed, first, last);

		if (r == ML_OK)
		{
			r = resume (ed);
		}
	}

	else if (is_ctrl_key (key, 'v') && ed->yank.len > 0)
	{
		/*
		 * the selection is replaced, the register stays as it was; a folded
		 * block is only where the cursor is, the lines go after it
		 */
		MlBlock b    = ml_session_block (&ed->session, first);
		int32_t fold = b.folded && b.first == first && b.last == last;

		if (!fold)
		{
			r = remove_range (ed, first, last);
		}

		if (r == ML_OK)
		{
			r = paste_lines (ed, ml_choose (fold, last + 1, first), 1);
		}

		if (r == ML_OK)
		{
			r = resume (ed);
		}
	}

	else if (is_ctrl_key (key, 'p'))
	{
		MlBlock b = ml_session_block (&ed->session, first);

		r = begin_command (ed);

		/* one line, or one folded block, needs no range */
		if (r == ML_OK && first != last && !(b.folded && b.last == last))
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

	else if (
		   (
			   key.code == ML_KEY_UP
			|| key.code == ML_KEY_DOWN
			|| key.code == ML_KEY_LEFT
			|| key.code == ML_KEY_RIGHT
			|| key.code == ML_KEY_ESCAPE
			|| key.code == ML_KEY_ENTER
		)
		&& !ctrl
	)
	{
		/* an arrow goes on from the end of the selection */
		if (
			   key.code != ML_KEY_UP
			&& key.code != ML_KEY_DOWN
		)
		{
			unfold_cursor (ed);
		}

		if (key.code == ML_KEY_UP)
		{
			ml_session_set_cursor (&ed->session, first);
			step_up (ed);
		}

		else if (key.code == ML_KEY_DOWN)
		{
			ml_session_set_cursor (&ed->session, last);
			step_down (ed);
		}

		/* below the last line there is a new note */
		if (key.code == ML_KEY_DOWN && last + 1 >= ed->session.lines.len)
		{
			ed->selecting = 0;
			r             = new_note (ed);
		}

		else
		{
			r = resume (ed);
		}
	}

	else
	{
		MlEditResult res = row_key (ed, key);

		r = res.result;

		/* typing leaves the selection, and goes into the line */
		if (
			   !res.handled
			&& key.code == ML_KEY_CHAR
			&& !(key.mods & (ML_MOD_CTRL | ML_MOD_ALT))
		)
		{
			unfold_cursor (ed);
			r = resume (ed);

			if (r == ML_OK)
			{
				r = text_key (ed, key);
			}
		}
	}

	return r;
}

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
		ml_lineedit_clear (&ed->edit);
		r = resume (ed);
	}

	else if (key.code == ML_KEY_ENTER)
	{
		r = run_command (ed);
		ml_lineedit_clear (&ed->edit);

		if (r == ML_OK && ed->steps_open)
		{
			enter_select (ed, ed->session.cursor);
		}

		else if (r == ML_OK)
		{
			r = resume (ed);
		}
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

	if (ed->state == ML_STATE_SELECT)
	{
		r = select_key (ed, key);
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

	if (ed->state == ML_STATE_SELECT)
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

	else if (ed->picking)
	{
		r = ml_str_append (out, ml_slice_cstr ("-- BLOCK --"));

		for (uint32_t d = 0; d < ML_DOMAIN_COUNT && r == ML_OK; d++)
		{
			r = ml_str_appendf (
				out,
				ml_choose (d == (uint32_t)ed->pick, " [%s]", "  %s "),
				ml_domain_name ((MlDomain)d)
			);
		}
	}

	else
	{
		r = ml_str_appendf (
			out,
			"-- %s -- %s>",
			ml_input_state_name (ed->state),
			ml_domain_name (ml_choose (
				ed->state == ML_STATE_MATH,
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
