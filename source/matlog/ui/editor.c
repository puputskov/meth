#include <matlog/ui/editor.h>

#include <matlog/core/utf8.h>
#include <matlog/docs/examples.h>
#include <matlog/docs/snippets.h>
#include <matlog/export/export.h>
#include <matlog/platform/config.h>
#include <matlog/platform/fs.h>
#include <matlog/platform/path.h>
#include <matlog/session/options.h>
#include <matlog/session/settings.h>
#include <matlog/texts/texts.h>
#include <matlog/tutorial/lessons.h>

#include <stdio.h>

static const char *const state_names [ML_STATE_COUNT] = {
	[ML_STATE_MATH]    = "MATH",
	[ML_STATE_NOTE]    = "NOTE",
	[ML_STATE_COMMAND] = "COMMAND",
	[ML_STATE_SELECT]  = "SELECT",
	[ML_STATE_HELP]    = "HELP"
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
	ml_str_init (&ed->theme_files);
	ml_help_init (&ed->help);
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
	ed->mouse_down    = 0;
	ed->mouse_line    = 0;
	ed->mouse_byte    = 0;
	ed->highlight     = 0;
	ed->ghost         = 1;
	ed->watch         = 0;
	ed->theme         = ml_theme_default ();
	ed->colors        = ML_COLORS_24BIT;
	ml_keymap_init (&ed->keymap);
	ed->help.keys = &ed->keymap;
	ml_tutorial_init (&ed->tutorial);
	ml_str_init (&ed->last_command);
	ed->commands_run = 0;
	MlKeyVec_init (&ed->pending);
}

void ml_editor_set_size (MlEditor *ed, uint32_t cols, uint32_t rows)
{
	ml_editor_set_page (ed, ml_choose (rows > 2, rows - 2, 1));
	ml_help_set_view (&ed->help, cols, rows);
}

void ml_editor_set_page (MlEditor *ed, uint64_t rows)
{
	ed->page_rows = ml_choose (rows > 0, rows, 1);
}

void ml_editor_free (MlEditor *ed)
{
	ml_results_free (&ed->results);
	ml_str_free (&ed->steps);
	ml_str_free (&ed->theme_files);
	ml_help_free (&ed->help);
	ml_session_free (&ed->session);
	ml_lineedit_free (&ed->edit);
	ml_rewriter_free (&ed->rewriter);
	lines_free (&ed->yank);
	ml_str_free (&ed->message);
	ml_keymap_free (&ed->keymap);
	ml_str_free (&ed->last_command);
	MlKeyVec_free (&ed->pending);
}

static MlResult set_message (MlEditor *ed, const char *text)
{
	ml_str_clear (&ed->message);
	return ml_str_append (&ed->message, ml_slice_cstr (text));
}

/*
 * The message from a text of the catalog with its {name} places filled in:
 * say clears the message first, say_more adds a space and goes on.
 */
static MlResult say_text (
	MlEditor        *ed,
	int32_t         more,
	const char      *code,
	const MlTextArg *args,
	uint64_t        count
)
{
	MlResult r = ML_OK;

	if (more)
	{
		r = ml_str_append (&ed->message, ml_slice_cstr (" "));
	}

	else
	{
		ml_str_clear (&ed->message);
	}

	return ml_choose (
		r == ML_OK,
		ml_text_fill (&ed->message, ml_text (code), args, count),
		r
	);
}

static MlResult say1 (
	MlEditor   *ed,
	const char *code,
	const char *name,
	MlSlice    value
)
{
	MlTextArg a = { .name = name, .value = value };

	return say_text (ed, 0, code, &a, 1);
}

static MlResult say2 (
	MlEditor   *ed,
	int32_t    more,
	const char *code,
	const char *name1,
	MlSlice    value1,
	const char *name2,
	MlSlice    value2
)
{
	MlTextArg a [2] = {
		{ .name = name1, .value = value1 },
		{ .name = name2, .value = value2 }
	};

	return say_text (ed, more, code, a, 2);
}

/* A number as text in buf (at least 12 bytes). */
static MlSlice number_text (char *buf, uint32_t n)
{
	return ml_slice (buf, (uint64_t)snprintf (buf, 12, "%u", n));
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
static MlResult settle (MlEditor *ed);
static MlResult resume (MlEditor *ed);

/* What the key binding table (settings [keys]) makes of a key. */
static MlAction key_action (const MlEditor *ed, MlKey key)
{
	return ml_keymap_lookup (&ed->keymap, key);
}

static MlDomain next_domain (MlDomain domain, int32_t forward)
{
	uint32_t count = ML_DOMAIN_COUNT;
	uint32_t step  = ml_choose (forward, 1, count - 1);

	return (MlDomain)(((uint32_t)domain + step) % count);
}

/*
 * The domain of the line being typed (and of the next ones), or of a note's
 * neighbours.
 */
static MlResult cycle_line_domain (MlEditor *ed, int32_t forward)
{
	MlResult r = ML_OK;

	if (ed->edit_kind == ML_LINE_MATH || ed->edit_kind == ML_LINE_BLANK)
	{
		MlDomain next = next_domain (ed->edit_domain, forward);

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
		MlDomain next = next_domain (ed->session.domain, forward);

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

	/*
	 * An empty block at the very top has no line to go back into: it turns
	 * into an empty note, the plain line a document starts with.
	 */
	if (
		   r == ML_OK
		&& !done
		&& dir < 0
		&& ed->edit_index == 0
		&& ed->edit.text.len == 0
		&& ed->edit_kind != ML_LINE_NOTE
	)
	{
		if (ed->edit_existing)
		{
			r = ml_session_replace (
				&ed->session,
				0,
				ML_LINE_NOTE,
				ed->edit_domain,
				ml_slice ("", 0)
			);
		}

		if (r == ML_OK)
		{
			r = begin_line (
				ed,
				ML_STATE_NOTE,
				ML_LINE_NOTE,
				ed->edit_domain,
				0,
				ed->edit_existing,
				ml_slice ("", 0)
			);
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
	MlEditResult res = {
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

/*
 * Starts a block of a domain at the cursor: an empty line becomes its first
 * line, else the line is split at the cursor. The domain is remembered as the
 * one the next blocks start with.
 */
static MlResult start_block (MlEditor *ed, MlDomain domain)
{
	MlResult r = ML_OK;

	ed->picking = 0;
	set_domain (ed, domain);

	if (ed->edit.text.len == 0)
	{
		ed->state       = ML_STATE_MATH;
		ed->edit_kind   = ML_LINE_MATH;
		ed->edit_domain = domain;
	}

	else
	{
		r = line_split_to (ed, ML_LINE_MATH, domain);
	}

	return r;
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
		r = start_block (ed, ed->pick);
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
		MlLine     l = {
			.kind   = v.kind,
			.domain = v.domain,
			.text = {
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

/* Reads the names of the theme files again, for the completion of :theme. */
static void refresh_theme_files (MlEditor *ed)
{
	MlStr dir;

	ml_str_init (&dir);
	ml_str_clear (&ed->theme_files);

	if (ml_config_dir (&dir) == ML_OK)
	{
		ml_fs_list (
			&ed->theme_files,
			ml_str_view (dir),
			ml_slice_cstr (".theme")
		);
	}

	ml_str_free (&dir);
}

static MlResult begin_command (MlEditor *ed)
{
	MlResult r = ml_lineedit_set (&ed->edit, ml_slice ("", 0));

	if (r == ML_OK)
	{
		ed->state = ML_STATE_COMMAND;
		refresh_theme_files (ed);
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
			r = set_message (
				ed,
				ml_text ("messages.no_steps")
			);
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
		r = set_message (
			ed,
			ml_text ("messages.nothing_to_fold")
		);
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
		r = set_message (
			ed,
			ml_choose (
				redo,
				ml_text ("messages.redone"),
				ml_text ("messages.undone")
			)
		);
	}

	else if (r == ML_OK)
	{
		r = set_message (
			ed,
			ml_choose (
				redo,
				ml_text ("messages.nothing_to_redo"),
				ml_text ("messages.nothing_to_undo")
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
		r = set_message (
			ed,
			ml_text ("messages.written")
		);
	}

	else
	{
		r = set_message (
			ed,
			ml_text ("messages.cannot_write")
		);
	}

	return r;
}

/* :open path loads the file, or starts a new session for a missing one. */
static MlResult open_session (MlEditor *ed, MlSlice path, int32_t force)
{
	MlResult r = ML_OK;

	if (path.len == 0)
	{
		r = set_message (
			ed,
			ml_text ("messages.open_needs_path")
		);
	}

	else if (ed->session.dirty && !force)
	{
		r = set_message (
			ed,
			ml_text ("messages.unsaved_open")
		);
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
				ml_text ("messages.opened"),
				ml_text ("messages.new_file")
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
		r = set_message (
			ed,
			ml_text ("messages.unsaved_quit")
		);
	}

	else
	{
		ed->quit = 1;
	}

	return r;
}

/*
 * :symbols on makes a name that stands for nothing a symbol (x + y is the
 * expression x + y), :symbols off brings back the error for a mistyped name.
 * Without an argument the setting is shown.
 */
/*
 * :fix units makes the lines of the algebra domain that fail only because they
 * use units lines of the physics domain (their error says so).
 */
static MlResult fix_units (MlEditor *ed, MlSlice arg)
{
	MlResult r = ML_OK;
	uint64_t n = 0;

	if (!ml_slice_equal (arg, ml_slice_cstr ("units")))
	{
		r = set_message (ed, ml_text ("messages.fix_usage"));
	}

	else
	{
		for (uint64_t i = 0; i < ed->session.lines.len && r == ML_OK; i++)
		{
			MlLineView   v = ml_session_get (&ed->session, i);
			MlResultView e = ml_results_get (&ed->results, i);

			if (
				   v.kind == ML_LINE_MATH
				&& v.domain == ML_DOMAIN_ALGEBRA
				&& e.kind == ML_RESULT_ERROR
				&& ml_slice_equal (e.code, ml_slice_cstr ("units_in_algebra"))
			)
			{
				r = ml_session_replace (
					&ed->session,
					i,
					ML_LINE_MATH,
					ML_DOMAIN_PHYSICS,
					v.text
				);
				n++;
			}
		}

		if (r == ML_OK)
		{
			char count [ML_TEXT_NUMBER];

			r = ml_results_update (&ed->results, &ed->session, 0);
			r = ml_choose (
				r == ML_OK,
				say1 (
					ed,
					"messages.fix_units_done",
					"count",
					ml_text_number (count, n)
				),
				r
			);
		}
	}

	return r;
}

static MlResult set_symbols (MlEditor *ed, MlSlice arg)
{
	MlResult r = ML_OK;

	if (ml_slice_equal (arg, ml_slice_cstr ("on")))
	{
		ed->results.free_symbols     = 1;
		ed->results.env.free_symbols = 1;
	}

	else if (ml_slice_equal (arg, ml_slice_cstr ("off")))
	{
		ed->results.free_symbols     = 0;
		ed->results.env.free_symbols = 0;
	}

	if (r == ML_OK)
	{
		r = ml_results_update (&ed->results, &ed->session, 0);
	}

	if (r == ML_OK)
	{
		r = set_message (
			ed,
			ml_choose (
				ed->results.free_symbols,
				ml_text ("messages.symbols_on"),
				ml_text ("messages.symbols_off")
			)
		);
	}

	return r;
}

/* A theme name that is a plain file name, with no folders in it. */
static int32_t is_theme_name (MlSlice name)
{
	int32_t ok = name.len > 0 && name.len < ML_THEME_NAME_MAX;

	for (uint64_t i = 0; i < name.len; i++)
	{
		ok = (
			   ok
			&& name.data [i] != '/'
			&& name.data [i] != '\\'
			&& name.data [i] != '.'
			&& name.data [i] != ':'
		);
	}

	return ok;
}

/* Loads name.theme of the settings folder over the dark theme. */
static MlResult load_user_theme (MlEditor *ed, MlSlice name)
{
	MlResult r = ML_ERR_INVALID;
	MlStr    dir;
	MlStr    file;
	MlStr    path;
	MlStr    text;
	MlStr    report;

	ml_str_init (&dir);
	ml_str_init (&file);
	ml_str_init (&path);
	ml_str_init (&text);
	ml_str_init (&report);

	if (is_theme_name (name) && ml_config_dir (&dir) == ML_OK)
	{
		r = ml_str_append (&file, name);

		if (r == ML_OK)
		{
			r = ml_str_append (&file, ml_slice_cstr (".theme"));
		}

		if (r == ML_OK)
		{
			r = ml_path_join (&path, ml_str_view (dir), ml_str_view (file));
		}

		if (r == ML_OK && !ml_fs_exists (ml_str_view (path)))
		{
			r = ML_ERR_INVALID;
		}

		if (r == ML_OK)
		{
			r = ml_fs_read_file (&text, ml_str_view (path));
		}
	}

	if (r == ML_OK)
	{
		MlTheme  t   = ml_theme_default ();
		uint64_t bad = 0;

		r = ml_theme_apply_text (&t, ml_str_view (text), &report, &bad);

		if (r == ML_OK)
		{
			ml_theme_set_name (&t, name);
			ed->theme     = t;
			ed->highlight = 1;
			r = say1 (ed, "messages.theme_set", "name", name);
		}

		if (r == ML_OK && bad > 0)
		{
			char count [12];

			r = say2 (
				ed,
				1,
				"messages.theme_ignored",
				"count",
				number_text (count, (uint32_t)bad),
				"report",
				ml_str_view (report)
			);
		}
	}

	ml_str_free (&report);
	ml_str_free (&text);
	ml_str_free (&path);
	ml_str_free (&file);
	ml_str_free (&dir);
	return r;
}

/*
 * :theme name chooses a built in theme or a name.theme file of the settings
 * folder, :theme reload reads the current one again, :theme alone shows the
 * current name and the built in ones.
 */
static MlResult set_theme (MlEditor *ed, MlSlice arg)
{
	MlResult r    = ML_OK;
	MlTheme  next = ml_theme_default ();
	MlSlice  name = ml_choose (
		ml_slice_equal (arg, ml_slice_cstr ("reload")),
		ml_slice_cstr (ed->theme.name),
		arg
	);

	if (arg.len == 0)
	{
		r = say2 (
			ed,
			0,
			"messages.theme_now",
			"name",
			ml_slice_cstr (ed->theme.name),
			"state",
			ml_slice_cstr (
				ml_text (
					ml_choose (ed->highlight, "messages.on", "messages.off")
				)
			)
		);

		for (uint64_t i = 0; i < ml_theme_builtin_count () && r == ML_OK; i++)
		{
			r = ml_str_appendf (&ed->message, " %s", ml_theme_builtin_name (i));
		}
	}

	else if (ml_theme_builtin (&next, name) == ML_OK)
	{
		ed->theme     = next;
		ed->highlight = 1;
		r = say1 (
			ed,
			ml_choose (
				ed->colors == ML_COLORS_OFF,
				"messages.theme_set_nocolor",
				"messages.theme_set"
			),
			"name",
			ml_slice_cstr (next.name)
		);
	}

	else if (load_user_theme (ed, name) != ML_OK)
	{
		r = set_message (
			ed,
			ml_text ("messages.no_such_theme")
		);
	}

	return r;
}

MlResult ml_editor_apply_theme (MlEditor *ed, MlSlice name)
{
	return set_theme (ed, name);
}

/* :colors 24bit|256|16|off sets how many colours the terminal shows. */
static MlResult set_colors (MlEditor *ed, MlSlice arg)
{
	MlResult     r     = ML_OK;
	MlColorLevel level = ml_color_level_from_name (arg);

	if (arg.len == 0)
	{
		r = say1 (
			ed,
			"messages.colors_now",
			"level",
			ml_slice_cstr (ml_color_level_name (ed->colors))
		);
	}

	else if (level == ML_COLORS_COUNT)
	{
		r = set_message (
			ed,
			ml_text ("messages.colors_usage")
		);
	}

	else
	{
		ed->colors    = level;
		ed->highlight = 1;
		r = say1 (
			ed,
			"messages.colors_now",
			"level",
			ml_slice_cstr (ml_color_level_name (level))
		);
	}

	return r;
}

static MlResult open_help (MlEditor *ed, MlSlice topic);

/*
 * :keys and :map alone show the key bindings in use as a page of the help,
 * made at the time of the command.
 */
static MlResult show_keys (MlEditor *ed)
{
	MlResult r = ML_OK;
	MlStr    page;
	MlStr    table;

	ml_str_init (&page);
	ml_str_init (&table);
	r = ml_keymap_format (&table, &ed->keymap);

	if (r == ML_OK)
	{
		MlTextArg table_arg = {
			.name  = "table",
			.value = ml_str_view (table)
		};

		r = ml_text_fill (&page, ml_text ("pages.yourkeys"), &table_arg, 1);
	}

	if (r == ML_OK)
	{
		r = ml_help_set_page (&ed->help, ml_str_view (page));
	}

	if (r == ML_OK)
	{
		r = open_help (ed, ml_slice_cstr ("yourkeys"));
	}

	ml_str_free (&table);
	ml_str_free (&page);
	return r;
}

/* Keeps a binding in the settings file: the key and an action or none. */
static void keep_binding (MlKey key, MlSlice action)
{
	MlStr spec;

	ml_str_init (&spec);

	if (ml_key_spec (&spec, key) == ML_OK)
	{
		ml_settings_set (
			ml_slice_cstr ("keys"),
			ml_str_view (spec),
			action
		);
	}

	ml_str_free (&spec);
}

/*
 * :map KEY ACTION binds a key and :unmap KEY removes the binding; the change
 * is written to the [keys] section of the settings file. KEY is written as
 * in the file (Ctrl+Shift+Z), ACTION is one of the names of :keys.
 */
static MlResult map_key (MlEditor *ed, MlSlice arg, int32_t unmap)
{
	MlResult   r     = ML_OK;
	uint64_t   split = 0;
	MlKeyParse p;
	MlAction   a;

	while (split < arg.len && arg.data [split] != ' ')
	{
		split++;
	}

	p = ml_key_parse (ml_slice_sub (arg, 0, split));
	a = ml_choose (
		unmap,
		ML_ACT_NONE,
		ml_action_from_name (
			trim (ml_slice_sub (arg, split, arg.len - split))
		)
	);

	if (arg.len == 0 && !unmap)
	{
		r = show_keys (ed);
	}

	else if (p.result != ML_OK)
	{
		ml_str_clear (&ed->message);
		r = ml_str_appendf (&ed->message, "%s", p.problem);
	}

	else if (a == ML_ACT_COUNT)
	{
		r = set_message (
			ed,
			ml_text ("messages.map_no_such_action")
		);
	}

	else
	{
		MlStr spec;

		ml_str_init (&spec);
		r = ml_key_spec (&spec, p.key);

		if (r == ML_OK && a == ML_ACT_NONE)
		{
			ml_keymap_unbind (&ed->keymap, p.key);
			ml_help_reload (&ed->help);
		}

		else if (r == ML_OK)
		{
			r = ml_keymap_bind (&ed->keymap, p.key, a);
			ml_help_reload (&ed->help);
		}

		if (r == ML_OK)
		{
			keep_binding (p.key, ml_slice_cstr (ml_action_name (a)));
			ml_str_clear (&ed->message);
			r = ml_str_appendf (
				&ed->message,
				"%s: %s",
				ml_str_cstr (spec),
				ml_action_name (a)
			);
		}

		ml_str_free (&spec);
	}

	return r;
}

MlResult ml_editor_load_keys (MlEditor *ed)
{
	MlResult r = ML_OK;
	MlStr    text;
	MlStr    report;
	uint64_t bad = 0;

	ml_str_init (&text);
	ml_str_init (&report);
	ml_keymap_free (&ed->keymap);
	r = ml_keymap_init (&ed->keymap);
	ml_help_reload (&ed->help);

	if (r == ML_OK)
	{
		r = ml_settings_read (&text);
	}

	if (r == ML_OK)
	{
		r = ml_keymap_apply_text (
			&ed->keymap,
			ml_str_view (text),
			&report,
			&bad
		);
		ml_help_reload (&ed->help);
	}

	if (r == ML_OK && bad > 0)
	{
		uint64_t first = 0;

		while (first < report.len && report.data [first] != '\n')
		{
			first++;
		}

		r = say1 (
			ed,
			ml_choose (bad > 1, "messages.keys_bad_more", "messages.keys_bad"),
			"first",
			ml_slice (report.data, first)
		);
	}

	ml_str_free (&report);
	ml_str_free (&text);
	return r;
}

/* The value of a setting as the editor has it now. */
static MlResult option_value (const MlEditor *ed, uint64_t index, MlStr *out)
{
	MlSlice name = ml_slice_cstr (ml_option_at (index).name);
	MlResult r   = ML_OK;

	if (ml_slice_equal (name, ml_slice_cstr ("theme")))
	{
		r = ml_str_append (out, ml_slice_cstr (ed->theme.name));
	}

	else if (ml_slice_equal (name, ml_slice_cstr ("colors")))
	{
		r = ml_str_append (
			out,
			ml_slice_cstr (ml_color_level_name (ed->colors))
		);
	}

	else if (ml_slice_equal (name, ml_slice_cstr ("language")))
	{
		r = ml_str_append (out, ml_slice_cstr (ml_texts_language ()));
	}

	else
	{
		int32_t on = ml_choose (
			ml_slice_equal (name, ml_slice_cstr ("ghost")),
			ed->ghost,
			ml_choose (
				ml_slice_equal (name, ml_slice_cstr ("watch")),
				ed->watch,
				ed->highlight
			)
		);

		r = ml_str_append (out, ml_slice_cstr (ml_choose (on, "on", "off")));
	}

	return r;
}

/* Puts a checked value of a setting in force. */
static MlResult apply_option (MlEditor *ed, uint64_t index, MlSlice value)
{
	MlResult r    = ML_OK;
	MlSlice  name = ml_slice_cstr (ml_option_at (index).name);

	if (ml_slice_equal (name, ml_slice_cstr ("theme")))
	{
		r = set_theme (ed, value);
	}

	else if (ml_slice_equal (name, ml_slice_cstr ("colors")))
	{
		r = set_colors (ed, value);
	}

	else if (ml_slice_equal (name, ml_slice_cstr ("language")))
	{
		MlStr language;

		ml_str_init (&language);
		r = ml_str_append (&language, value);

		if (r == ML_OK)
		{
			r = ml_texts_reload (ml_str_cstr (language), NULL);
		}

		ml_str_free (&language);
	}

	else if (ml_slice_equal (name, ml_slice_cstr ("ghost")))
	{
		ed->ghost = ml_option_bool (value) > 0;
	}

	else if (ml_slice_equal (name, ml_slice_cstr ("watch")))
	{
		ed->watch = ml_option_bool (value) > 0;
	}

	else
	{
		ed->highlight = ml_option_bool (value) > 0;
	}

	return r;
}

static MlResult set_option (MlEditor *ed, MlSlice arg);

/*
 * F4 and :watch show or hide the watch window; :watch on and :watch off say
 * which. The choice is kept in the settings like any other (watch).
 */
static MlResult toggle_watch (MlEditor *ed, MlSlice arg)
{
	int32_t on = ml_choose (
		ml_option_bool (arg) >= 0,
		ml_option_bool (arg),
		!ed->watch
	);

	return set_option (
		ed,
		ml_choose (on, ml_slice_cstr ("watch on"), ml_slice_cstr ("watch off"))
	);
}

/*
 * F7 and :plot show or hide the plot of the cursor line in the plot panel;
 * :plot on and :plot off say which. A line without a plot says so.
 */
static MlResult toggle_plot (MlEditor *ed, MlSlice arg)
{
	uint64_t     line = ed->session.cursor;
	MlResultView v    = ml_results_get (&ed->results, line);
	int32_t      on   = ml_choose (
		ml_option_bool (arg) >= 0,
		ml_option_bool (arg),
		!ml_session_get (&ed->session, line).plotted
	);
	MlResult     r    = ML_OK;

	if (line >= ed->session.lines.len || v.plot == NULL)
	{
		r = set_message (
			ed,
			ml_text ("messages.plot_no_plot")
		);
	}

	else
	{
		ml_session_set_plotted (&ed->session, line, on);
		r = set_message (
			ed,
			ml_choose (
				on,
				ml_text ("messages.plot_shown"),
				ml_text ("messages.plot_hidden")
			)
		);
	}

	return r;
}

/*
 * :set alone lists the settings, :set name shows one, :set name value
 * changes it and keeps it in the settings file.
 */
static MlResult set_option (MlEditor *ed, MlSlice arg)
{
	MlResult r     = ML_OK;
	uint64_t split = 0;
	MlSlice  name;
	MlSlice  value;
	uint64_t index;

	while (split < arg.len && arg.data [split] != ' ')
	{
		split++;
	}

	name  = ml_slice_sub (arg, 0, split);
	value = trim (ml_slice_sub (arg, split, arg.len - split));
	index = ml_option_find (name);
	ml_str_clear (&ed->message);

	if (arg.len == 0)
	{
		for (uint64_t i = 0; i < ml_option_count () && r == ML_OK; i++)
		{
			MlStr now;

			ml_str_init (&now);
			r = option_value (ed, i, &now);

			if (r == ML_OK)
			{
				r = ml_str_appendf (
					&ed->message,
					"%s%s=%s",
					ml_choose (i > 0, " ", ""),
					ml_option_at (i).name,
					ml_str_cstr (now)
				);
			}

			ml_str_free (&now);
		}
	}

	else if (index >= ml_option_count ())
	{
		r = say1 (ed, "messages.no_such_setting_named", "name", name);
	}

	else if (value.len == 0)
	{
		MlStr now;

		ml_str_init (&now);
		r = option_value (ed, index, &now);

		if (r == ML_OK)
		{
			r = ml_str_appendf (
				&ed->message,
				"%s=%s",
				ml_option_at (index).name,
				ml_str_cstr (now)
			);
		}

		ml_str_free (&now);
	}

	else
	{
		MlStr good;

		ml_str_init (&good);

		if (ml_option_check (index, value, &good) != ML_OK)
		{
			MlOption o = ml_option_at (index);

			r = say2 (
				ed,
				0,
				ml_choose (
					o.kind == ML_OPTION_CHOICE,
					"messages.not_a_value_choice",
					ml_choose (
						o.kind == ML_OPTION_BOOL,
						"messages.not_a_value_bool",
						"messages.not_a_value"
					)
				),
				"name",
				ml_slice_cstr (o.name),
				"values",
				ml_slice_cstr (ml_choose (o.values != NULL, o.values, ""))
			);
		}

		else
		{
			MlStr now;

			ml_str_init (&now);
			r = apply_option (ed, index, ml_str_view (good));

			/* what is kept is what is in force, as a theme may be missing */
			if (r == ML_OK)
			{
				r = option_value (ed, index, &now);
			}

			if (r == ML_OK)
			{
				ml_settings_set (
					ml_slice_cstr (ml_option_at (index).section),
					ml_slice_cstr (ml_option_at (index).name),
					ml_str_view (now)
				);
			}

			ml_str_free (&now);
		}

		ml_str_free (&good);
	}

	return r;
}

MlResult ml_editor_load_settings (MlEditor *ed)
{
	MlResult r = ML_OK;

	for (uint64_t i = 0; i < ml_option_count () && r == ML_OK; i++)
	{
		MlOption o     = ml_option_at (i);
		MlStr    value;
		MlStr    good;
		int32_t  found = 0;

		ml_str_init (&value);
		ml_str_init (&good);
		r = ml_settings_get (
			&value,
			ml_slice_cstr (o.section),
			ml_slice_cstr (o.name),
			&found
		);

		if (
			   r == ML_OK
			&& found
			&& ml_option_check (i, ml_str_view (value), &good) == ML_OK
		)
		{
			r = apply_option (ed, i, ml_str_view (good));
		}

		ml_str_free (&good);
		ml_str_free (&value);
	}

	/* the texts follow the setting, also when it was taken out of the file */
	if (r == ML_OK)
	{
		r = ml_texts_use_settings (NULL);
	}

	ml_str_clear (&ed->message);
	return r;
}

/*
 * :config shows the page of the documentation about the settings, :config
 * path says where the file is, :config reload reads it again (the settings
 * and the key bindings).
 */
static MlResult config_command (MlEditor *ed, MlSlice arg)
{
	MlResult r = ML_OK;

	if (ml_slice_equal (arg, ml_slice_cstr ("path")))
	{
		MlStr path;

		ml_str_init (&path);
		ml_str_clear (&ed->message);
		r = ml_settings_path (&path);

		if (r == ML_OK)
		{
			r = say1 (
				ed,
				"messages.settings_path",
				"path",
				ml_str_view (path)
			);
		}

		else
		{
			r = set_message (
				ed,
				ml_text ("messages.no_settings_folder")
			);
		}

		ml_str_free (&path);
	}

	else if (ml_slice_equal (arg, ml_slice_cstr ("reload")))
	{
		r = ml_editor_load_settings (ed);

		if (r == ML_OK)
		{
			r = ml_editor_load_keys (ed);
		}

		if (r == ML_OK && ed->message.len == 0)
		{
			r = set_message (
				ed,
				ml_text ("messages.settings_read")
			);
		}
	}

	else if (arg.len == 0)
	{
		r = open_help (ed, ml_slice_cstr ("settings"));
	}

	else
	{
		r = set_message (
			ed,
			ml_text ("messages.config_usage")
		);
	}

	return r;
}

/*
 * Puts the lines of src into the session after the cursor line, one change
 * for undo, and the cursor on the first of them.
 */
static MlResult put_session_lines (
	MlEditor        *ed,
	const MlSession *src,
	MlSlice         name
)
{
	MlResult r  = ML_OK;
	uint64_t at = ml_choose (
		ed->session.lines.len > 0,
		ed->session.cursor + 1,
		0
	);

	for (uint64_t i = 0; i < src->lines.len && r == ML_OK; i++)
	{
		MlLineView v = ml_session_get (src, i);

		r = ml_session_insert (&ed->session, at + i, v.kind, v.domain, v.text);
	}

	if (r == ML_OK)
	{
		ml_session_set_cursor (&ed->session, at);
		char count [12];

		r = say2 (
			ed,
			0,
			"messages.example_inserted",
			"count",
			number_text (count, (uint32_t)src->lines.len),
			"name",
			name
		);
	}

	return r;
}

/*
 * :example NAME puts the lines of an example (see docs/examples.h) into the
 * session after the cursor line, as one change that undo takes back at once;
 * :examples alone opens the table of contents in the help.
 */
static MlResult insert_example (MlEditor *ed, MlSlice name)
{
	MlResult r   = ML_OK;
	uint64_t idx = ml_example_find (name);

	if (name.len == 0)
	{
		r = open_help (ed, ml_slice_cstr ("examples"));
	}

	else if (idx >= ml_example_count ())
	{
		r = say1 (ed, "messages.no_example", "name", name);
	}

	else
	{
		MlSession ex;

		ml_session_init (&ex);
		r = ml_mlog_parse (&ex, ml_example_text (idx));

		if (r == ML_OK)
		{
			r = put_session_lines (ed, &ex, name);
		}

		ml_session_free (&ex);
	}

	return r;
}

/*
 * :snippet NAME puts the lines of a snippet (see docs/snippets.h) into the
 * session after the cursor line; :snippet alone and :snippets open the page
 * about them, with the snippets of the user in it.
 */
static MlResult insert_snippet (MlEditor *ed, MlSlice name)
{
	MlResult r = ML_OK;

	if (name.len == 0)
	{
		MlStr page;

		ml_str_init (&page);
		r = ml_snippets_page (&page);

		if (r == ML_OK)
		{
			r = ml_help_set_page (&ed->help, ml_str_view (page));
		}

		if (r == ML_OK)
		{
			r = open_help (ed, ml_slice_cstr ("snippets"));
		}

		ml_str_free (&page);
	}

	else
	{
		MlStr          text;
		MlSnippetFound f;

		ml_str_init (&text);
		f = ml_snippet_find (&text, name);
		r = f.result;

		if (r == ML_OK && !f.found)
		{
			r = say1 (ed, "messages.no_snippet", "name", name);
		}

		else if (r == ML_OK)
		{
			MlSession lines;

			ml_session_init (&lines);
			r = ml_snippet_lines (&lines, ml_str_view (text));

			if (r == ML_OK)
			{
				r = put_session_lines (ed, &lines, name);
			}

			ml_session_free (&lines);
		}

		ml_str_free (&text);
	}

	return r;
}

/* The tutorial's view of the editor after the key. */
static MlTutorialView tutorial_view (const MlEditor *ed, MlKey key)
{
	int32_t editing = ed->state == ML_STATE_MATH;
	MlTutorialView v = {
		.action       = ml_keymap_lookup (&ed->keymap, key),
		.key_code     = key.code,
		.key_mods     = key.mods,
		.state        = (int32_t)ed->state,
		.domain       = ml_choose (
			editing,
			ed->edit_domain,
			ed->session.domain
		),
		.session      = &ed->session,
		.results      = &ed->results,
		.last_command = ml_str_view (ed->last_command),
		.commands_run = ed->commands_run,
		.steps_open   = ed->steps_open,
		.watch        = ed->watch
	};

	return v;
}

/* The states the lessons name are the states of the editor. */
_Static_assert (ML_TUT_MATH == ML_STATE_MATH, "tutorial state");
_Static_assert (ML_TUT_NOTE == ML_STATE_NOTE, "tutorial state");
_Static_assert (ML_TUT_COMMAND == ML_STATE_COMMAND, "tutorial state");
_Static_assert (ML_TUT_SELECT == ML_STATE_SELECT, "tutorial state");
_Static_assert (ML_TUT_HELP == ML_STATE_HELP, "tutorial state");

/* The lesson to start with: the one after the last that was finished. */
static uint64_t tutorial_resume (MlSlice what)
{
	uint64_t lesson = 0;
	MlStr    saved;
	int32_t  found  = 0;

	ml_str_init (&saved);

	if (
		   ml_settings_get (
			   &saved,
			   ml_slice_cstr ("tutorial"),
			   ml_slice_cstr ("lesson"),
			   &found
		   ) == ML_OK
		&& found
		&& ml_lesson_find (ml_str_view (saved)) < ml_lesson_count ()
	)
	{
		lesson = ml_lesson_find (ml_str_view (saved)) + 1;
	}

	if (lesson >= ml_lesson_count ())
	{
		lesson = 0;
	}

	if (ml_slice_equal (what, ml_slice_cstr ("syntax")))
	{
		lesson = ml_choose (
			lesson >= ml_lesson_first (2),
			lesson,
			ml_lesson_first (2)
		);
	}

	ml_str_free (&saved);
	return lesson;
}

MlResult ml_editor_start_tutorial (MlEditor *ed, MlSlice what)
{
	MlResult       r      = ML_OK;
	uint64_t       lesson = tutorial_resume (what);
	MlKey          none   = { .code = ML_KEY_NONE };
	MlTutorialView v;

	ml_session_free (&ed->session);
	ml_session_init (&ed->session);
	ed->steps_open = 0;
	ed->selecting  = 0;
	ed->picking    = 0;
	ml_lineedit_clear (&ed->edit);
	r = ml_results_update (&ed->results, &ed->session, 0);

	if (r == ML_OK)
	{
		ed->results_rev = ed->session.revision;
		r               = ml_editor_begin_note (ed);
	}

	if (r == ML_OK)
	{
		v = tutorial_view (ed, none);
		ml_tutorial_start (&ed->tutorial, lesson, &v);
		r = set_message (
			ed,
			ml_text ("messages.tutorial_keys")
		);
	}

	return r;
}

int32_t ml_editor_take_pending (MlEditor *ed, MlKey *key)
{
	int32_t any = ed->pending.len > 0;

	if (any)
	{
		*key = ed->pending.data [0];
		MlKeyVec_remove (&ed->pending, 0);
	}

	return any;
}

/* Keeps the lesson that was just finished in the settings file. */
static void tutorial_save_progress (const MlEditor *ed, uint64_t previous)
{
	if (ed->tutorial.lesson_done)
	{
		ml_settings_set (
			ml_slice_cstr ("tutorial"),
			ml_slice_cstr ("lesson"),
			ml_slice_cstr (ml_lesson_at (previous).id)
		);
	}
}

/*
 * :tutorial starts it at the next lesson not done, :tutorial syntax at the
 * syntax lessons; off, next, back, show (types the model answer of the step)
 * and reset (forget what was done). :tutorial! also leaves a session with
 * changes behind.
 */
static MlResult tutorial_command (MlEditor *ed, MlSlice arg, int32_t force)
{
	MlResult       r    = ML_OK;
	MlKey          none = { .code = ML_KEY_NONE };
	MlTutorialView v    = tutorial_view (ed, none);

	if (ml_slice_equal (arg, ml_slice_cstr ("off")))
	{
		ml_tutorial_stop (&ed->tutorial);
		r = set_message (
			ed,
			ml_text ("messages.tutorial_left")
		);
	}

	else if (ml_slice_equal (arg, ml_slice_cstr ("next")))
	{
		ml_tutorial_skip (&ed->tutorial, &v);
		ed->tutorial.skip_next = 1;
	}

	else if (ml_slice_equal (arg, ml_slice_cstr ("back")))
	{
		ml_tutorial_back (&ed->tutorial, &v);
		ed->tutorial.skip_next = 1;
	}

	else if (ml_slice_equal (arg, ml_slice_cstr ("show")))
	{
		r = ml_tutorial_answer (
			&ed->pending,
			&ed->keymap,
			ml_tutorial_step (&ed->tutorial)
		);
	}

	else if (ml_slice_equal (arg, ml_slice_cstr ("reset")))
	{
		ml_settings_remove (
			ml_slice_cstr ("tutorial"),
			ml_slice_cstr ("lesson")
		);
		r = set_message (
			ed,
			ml_text ("messages.tutorial_nothing_done")
		);
	}

	else if (arg.len == 0 || ml_slice_equal (arg, ml_slice_cstr ("syntax")))
	{
		if (!force && ed->session.dirty && ed->session.lines.len > 0)
		{
			r = set_message (
				ed,
				ml_text ("messages.tutorial_session_changed")
			);
		}

		else
		{
			r = ml_editor_start_tutorial (ed, arg);

			/* the Enter that ran the command is not an answer */
			ed->tutorial.skip_next = 1;
		}
	}

	else
	{
		r = set_message (
			ed,
			ml_text ("messages.tutorial_usage")
		);
	}

	return r;
}

/* The name of the session file without its folder and its extension. */
static MlSlice session_stem (const MlSession *s)
{
	MlSlice name = ml_path_name (ml_str_view (s->path));
	uint64_t dot = name.len;

	while (dot > 0 && name.data [dot - 1] != '.')
	{
		dot--;
	}

	return ml_choose (dot > 1, ml_slice_sub (name, 0, dot - 1), name);
}

/* The default file of an export: the session file with the new extension. */
static MlResult export_path (MlStr *out, const MlSession *s, const char *ext)
{
	MlSlice  path = ml_str_view (s->path);
	uint64_t dot  = path.len;
	uint64_t sep  = path.len;

	while (dot > 0 && path.data [dot - 1] != '.')
	{
		dot--;
	}

	while (sep > 0 && path.data [sep - 1] != '/' && path.data [sep - 1] != '\\')
	{
		sep--;
	}

	return ml_str_appendf (
		out,
		"%.*s.%s",
		(int)ml_choose (dot > sep + 1, dot - 1, path.len),
		path.data,
		ext
	);
}

/*
 * :export FORMAT [body] [errors] [3-12] [path] writes the session (or the
 * lines of the range) as a document, next to the session file unless a path
 * is given. :export! replaces a file that is there.
 */
static MlResult export_command (
	MlEditor *ed,
	MlSlice  arg,
	int32_t  force,
	int32_t  ranged,
	uint64_t first,
	uint64_t last
)
{
	MlResult        r     = ML_OK;
	uint64_t        space = 0;
	MlExportFormat  fmt;
	MlExportOptions opt;
	MlSlice         path  = ml_slice ("", 0);
	MlStr           file;
	MlStr           doc;

	ml_str_init (&file);
	ml_str_init (&doc);

	while (space < arg.len && arg.data [space] != ' ')
	{
		space++;
	}

	fmt = ml_export_format_from_name (ml_slice_sub (arg, 0, space));
	opt = ml_export_options (fmt);

	if (fmt == ML_EXPORT_COUNT)
	{
		r = set_message (
			ed,
			ml_text ("messages.export_format")
		);
	}

	else if (
		ml_export_read_words (
			&opt,
			ml_slice_sub (arg, space, arg.len - space),
			&path
		) != ML_OK
	)
	{
		r = set_message (
			ed,
			ml_text ("messages.export_options")
		);
	}

	else if (path.len == 0 && ed->session.path.len == 0)
	{
		r = set_message (
			ed,
			ml_text ("messages.export_needs_path")
		);
	}

	else
	{
		if (path.len > 0)
		{
			r = ml_str_append (&file, path);
		}

		else
		{
			r = export_path (&file, &ed->session, ml_export_extension (fmt));
		}

		/* a range before the command, or the words, picks the lines */
		if (ranged && !opt.ranged)
		{
			opt.ranged = 1;
			opt.first  = first;
			opt.last   = last;
		}

		opt.title = session_stem (&ed->session);

		if (r == ML_OK)
		{
			r = ml_editor_refresh (ed);
		}

		if (r == ML_OK && ml_fs_exists (ml_str_view (file)) && !force)
		{
			r = set_message (
				ed,
				ml_text ("messages.export_file_exists")
			);
		}

		else if (r == ML_OK)
		{
			r = ml_export (&doc, &ed->session, &ed->results, opt);
		}

		if (r == ML_OK && doc.len > 0)
		{
			if (ml_fs_write_file_atomic (ml_str_view (file), ml_str_view (doc))
				== ML_OK)
			{
				r = say1 (ed, "messages.exported", "path", ml_str_view (file));
			}

			else
			{
				r = set_message (
					ed,
					ml_text ("messages.export_cannot_write")
				);
			}
		}
	}

	ml_str_free (&doc);
	ml_str_free (&file);
	return r;
}

/*
 * Opens the help viewer at what was typed after :help (nothing: the index
 * page). A search that finds nothing is said in the status line.
 */
static MlResult open_help (MlEditor *ed, MlSlice topic)
{
	MlResult r = ml_help_open (&ed->help, topic);

	if (r == ML_OK)
	{
		ed->state = ML_STATE_HELP;
	}

	else if (r == ML_ERR_INVALID)
	{
		r = say1 (ed, "messages.no_help_for", "topic", topic);
	}

	return r;
}

/* The word under the cursor of the line being typed (or the command). */
static MlSlice word_at_cursor (const MlEditor *ed)
{
	MlSlice  text  = ml_str_view (ed->edit.text);
	uint64_t start = ed->edit.cursor;
	uint64_t end   = ed->edit.cursor;

	while (
		   start > 0
		&& (
			   (text.data [start - 1] >= 'a' && text.data [start - 1] <= 'z')
			|| (text.data [start - 1] >= 'A' && text.data [start - 1] <= 'Z')
			|| (text.data [start - 1] >= '0' && text.data [start - 1] <= '9')
			|| text.data [start - 1] == '_'
		)
	)
	{
		start--;
	}

	while (
		   end < text.len
		&& (
			   (text.data [end] >= 'a' && text.data [end] <= 'z')
			|| (text.data [end] >= 'A' && text.data [end] <= 'Z')
			|| (text.data [end] >= '0' && text.data [end] <= '9')
			|| text.data [end] == '_'
		)
	)
	{
		end++;
	}

	return ml_slice_sub (text, start, end - start);
}

/*
 * F1: the help for the word under the cursor, or for the command being typed.
 * The line being typed is written into the session first, the viewer closes
 * into it again.
 */
static MlResult help_key_f1 (MlEditor *ed)
{
	MlResult r       = ML_OK;
	int32_t  editing = ed->state == ML_STATE_MATH || ed->state == ML_STATE_NOTE;
	MlStr    topic;

	ml_str_init (&topic);

	if (editing || ed->state == ML_STATE_COMMAND)
	{
		r = ml_str_append (&topic, word_at_cursor (ed));
	}

	if (r == ML_OK && editing)
	{
		r = settle (ed);
	}

	if (r == ML_OK)
	{
		r = open_help (ed, ml_str_view (topic));
	}

	/* nothing known about the word: the index, with a word about it */
	if (r == ML_OK && ed->state != ML_STATE_HELP)
	{
		MlStr said;

		ml_str_init (&said);
		r = ml_str_append (&said, ml_str_view (ed->message));

		if (r == ML_OK)
		{
			r = open_help (ed, ml_slice ("", 0));
		}

		if (r == ML_OK)
		{
			ml_str_clear (&ed->message);
			r = ml_str_append (&ed->message, ml_str_view (said));
		}

		ml_str_free (&said);
	}

	ml_str_free (&topic);
	return r;
}

/* The keys while the help viewer is shown. */
static MlResult help_key (MlEditor *ed, MlKey key)
{
	int32_t search = 0;
	MlResult r     = ml_help_key (&ed->help, key, &search);

	if (r == ML_OK && search)
	{
		r = begin_command (ed);

		if (r == ML_OK)
		{
			r = ml_lineedit_set (&ed->edit, ml_slice_cstr ("help "));
		}
	}

	else if (r == ML_OK && !ed->help.open)
	{
		r = resume (ed);
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
		r = set_message (
			ed,
			ml_text ("messages.unknown_domain")
		);
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
	Address a = {
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
	Range   rg = {
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
		r = set_message (
			ed,
			ml_text ("messages.move_needs_line")
		);
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
		r = set_message (
			ed,
			ml_text ("messages.copy_needs_line")
		);
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
		r = set_message (
			ed,
			ml_text ("messages.no_lines")
		);
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

	/* the tutorial looks at the commands that were run */
	ml_str_clear (&ed->last_command);
	r = ml_str_append (&ed->last_command, full);
	ed->commands_run++;

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

	else if (is_cmd (cmd, "symbols"))
	{
		r = set_symbols (ed, arg);
	}

	else if (is_cmd (cmd, "fix"))
	{
		r = fix_units (ed, arg);
	}

	else if (is_cmd (cmd, "help") || is_cmd (cmd, "h"))
	{
		r = open_help (ed, arg);
	}

	else if (is_cmd (cmd, "export") || is_cmd (cmd, "export!"))
	{
		r = export_command (
			ed,
			arg,
			is_cmd (cmd, "export!"),
			rg.given,
			rg.first,
			rg.last
		);
	}

	else if (is_cmd (cmd, "theme"))
	{
		r = set_theme (ed, arg);
	}

	else if (is_cmd (cmd, "colors"))
	{
		r = set_colors (ed, arg);
	}

	else if (is_cmd (cmd, "undo") || is_cmd (cmd, "u"))
	{
		r = undo_redo (ed, 0);
	}

	else if (is_cmd (cmd, "redo") || is_cmd (cmd, "red"))
	{
		r = undo_redo (ed, 1);
	}

	else if (is_cmd (cmd, "examples") || is_cmd (cmd, "example"))
	{
		r = insert_example (ed, arg);
	}

	else if (is_cmd (cmd, "snippet") || is_cmd (cmd, "snippets"))
	{
		r = insert_snippet (ed, arg);
	}

	else if (is_cmd (cmd, "watch"))
	{
		r = toggle_watch (ed, arg);
	}

	else if (is_cmd (cmd, "plot"))
	{
		r = toggle_plot (ed, arg);
	}

	else if (is_cmd (cmd, "tutorial") || is_cmd (cmd, "tutorial!"))
	{
		r = tutorial_command (ed, arg, is_cmd (cmd, "tutorial!"));
	}

	else if (is_cmd (cmd, "set"))
	{
		r = set_option (ed, arg);
	}

	else if (is_cmd (cmd, "config"))
	{
		r = config_command (ed, arg);
	}

	else if (is_cmd (cmd, "keys") || is_cmd (cmd, "map"))
	{
		r = map_key (ed, arg, 0);
	}

	else if (is_cmd (cmd, "unmap"))
	{
		r = map_key (ed, arg, 1);
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
		r = set_message (
			ed,
			ml_text ("messages.unknown_command")
		);
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
		c = ml_complete_command_with (
			ml_str_view (ed->edit.text),
			ml_str_view (ed->theme_files)
		);
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
MlTextRange ml_editor_selection (const MlEditor *ed)
{
	uint64_t cl    = ed->edit_index;
	uint64_t cb    = ed->edit.cursor;
	int32_t  first = (
		   ed->sel_line < cl
		|| (ed->sel_line == cl && ed->sel_anchor <= cb)
	);
	MlTextRange r = {
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
		MlLine     piece = {
			.kind   = ml_choose (whole, v.kind, ML_LINE_NOTE),
			.domain = ml_choose (whole, v.domain, ML_DOMAIN_ALGEBRA),
			.text   = {
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
		r = set_message (
			ed,
			ml_text ("messages.copied")
		);
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
		r = set_message (
			ed,
			ml_text ("messages.nothing_to_paste")
		);
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
	MlEditResult res = {
		.result  = ML_OK,
		.handled = 1
	};

	MlAction     act = key_action (ed, key);

	if (act == ML_ACT_LINES_DOWN)
	{
		res.result = row_move (ed, 1);
	}

	else if (act == ML_ACT_LINES_UP)
	{
		res.result = row_move (ed, -1);
	}

	else if (act == ML_ACT_FOLD)
	{
		res.result = row_fold (ed);
	}

	else if (act == ML_ACT_BLOCK_NEXT)
	{
		res.result = row_block (ed, 1);
	}

	else if (act == ML_ACT_BLOCK_PREV)
	{
		res.result = row_block (ed, 0);
	}

	else if (act == ML_ACT_GOTO_FIRST)
	{
		res.result = row_goto (ed, 0);
	}

	else if (act == ML_ACT_GOTO_LAST)
	{
		res.result = row_goto (ed, 1);
	}

	else if (act == ML_ACT_SELECT_LINE)
	{
		res.result = select_line (ed);
	}

	else if (act == ML_ACT_SELECT_ALL)
	{
		res.result = select_all (ed);
	}

	else if (act == ML_ACT_COPY)
	{
		res.result = row_copy (ed);
	}

	else if (act == ML_ACT_CUT)
	{
		res.result = row_cut (ed);
	}

	else if (act == ML_ACT_PASTE)
	{
		res.result = row_paste (ed);
	}

	else if (act == ML_ACT_COMMAND)
	{
		res.result = row_command (ed);
	}

	else if (act == ML_ACT_SAVE)
	{
		res.result = row_file (ed, 0);
	}

	else if (act == ML_ACT_QUIT)
	{
		res.result = row_file (ed, 1);
	}

	else if (act == ML_ACT_STEPS)
	{
		res.result = row_steps (ed);
	}

	else if (act == ML_ACT_WATCH)
	{
		res.result = toggle_watch (ed, ml_slice ("", 0));
	}

	else if (act == ML_ACT_PLOT)
	{
		res.result = toggle_plot (ed, ml_slice ("", 0));
	}

	else
	{
		res.handled = 0;
	}

	return res;
}

/* Does the key keep a selection going, or act on it? */
static int32_t is_selection_key (const MlEditor *ed, MlKey key)
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
	MlAction act = key_action (ed, key);
	int32_t  ctrl = (
		   act == ML_ACT_SELECT_ALL
		|| act == ML_ACT_COPY
		|| act == ML_ACT_CUT
		|| act == ML_ACT_PASTE
		|| act == ML_ACT_SELECT_LINE
		|| act == ML_ACT_COMMAND
	);
	int32_t alt = act == ML_ACT_LINES_UP || act == ML_ACT_LINES_DOWN;
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
	MlEditResult res = {
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

	if (
		   move
		&& (key.code == ML_KEY_PAGE_UP || key.code == ML_KEY_PAGE_DOWN)
	)
	{
		/* a page is a number of Shift+Up or Shift+Down */
		MlKey step = {
			.code = ml_choose (
				key.code == ML_KEY_PAGE_UP,
				ML_KEY_UP,
				ML_KEY_DOWN
			),
			.cp   = 0,
			.mods = ML_MOD_SHIFT
		};

		for (uint64_t i = 0; i < ed->page_rows && res.result == ML_OK; i++)
		{
			res.result = select_move (ed, step);
		}

		res.handled = 1;
	}

	else if (move)
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
	MlEditResult res = {
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

	else if (
		   key_action (ed, key) == ML_ACT_DOMAIN_NEXT
		|| key_action (ed, key) == ML_ACT_DOMAIN_PREV
	)
	{
		r = cycle_line_domain (ed, key_action (ed, key) == ML_ACT_DOMAIN_NEXT);
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
	MlAction     act = key_action (ed, key);
	MlEditResult res = {
		.result  = ML_OK,
		.handled = 0
	};

	if (!is_selection_key (ed, key))
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

	else if (act == ML_ACT_UNDO || act == ML_ACT_REDO)
	{
		/* the line being typed is kept, then the step before it undone */
		r = settle (ed);

		if (r == ML_OK)
		{
			r = undo_redo (ed, act == ML_ACT_REDO);
		}

		if (r == ML_OK)
		{
			r = resume (ed);
		}
	}

	else if (act == ML_ACT_BLOCK_NEW)
	{
		/* Ctrl+Enter, Shift+Enter: a block of the domain used last */
		r = start_block (ed, ed->session.domain);
	}

	else if (act == ML_ACT_BLOCK_PICK)
	{
		/* Ctrl+B: the domain is chosen first */
		ed->picking = 1;
		ed->pick    = ed->session.domain;
	}

	else if (act == ML_ACT_NOTE_TOGGLE)
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
	MlAction act   = key_action (ed, key);
	int32_t  ctrl  = (key.mods & ML_MOD_CTRL) != 0;
	int32_t  shift = (key.mods & ML_MOD_SHIFT) != 0;

	if (act == ML_ACT_UNDO || act == ML_ACT_REDO)
	{
		r = undo_redo (ed, act == ML_ACT_REDO);

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

	else if (act == ML_ACT_LINES_DOWN)
	{
		r = shift_lines (ed, first, last, 1);
	}

	else if (act == ML_ACT_LINES_UP)
	{
		r = shift_lines (ed, first, last, -1);
	}

	else if (act == ML_ACT_FOLD)
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

	else if (act == ML_ACT_STEPS)
	{
		r = toggle_steps (ed);
	}

	else if (act == ML_ACT_COPY)
	{
		r = yank_range (ed, first, last);

		if (r == ML_OK)
		{
			r = set_message (
				ed,
				ml_text ("messages.lines_copied")
			);
		}
	}

	else if (act == ML_ACT_CUT)
	{
		r = delete_range (ed, first, last);

		if (r == ML_OK)
		{
			r = resume (ed);
		}
	}

	else if (act == ML_ACT_PASTE && ed->yank.len > 0)
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

	else if (act == ML_ACT_COMMAND)
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

		/* the help viewer opened by the command stays */
		if (r == ML_OK && ed->state == ML_STATE_HELP)
		{
			r = ML_OK;
		}

		else if (r == ML_OK && ed->steps_open)
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

/* The wheel: the cursor goes three lines, the text scrolls with it. */
static MlResult mouse_wheel (MlEditor *ed, int32_t dir)
{
	MlResult r = ML_OK;

	for (uint64_t i = 0; i < 3 && r == ML_OK; i++)
	{
		int32_t can = ml_choose (
			dir < 0,
			ed->edit_index > 0,
			ed->edit_index + 1 < ed->session.lines.len
		);

		if (can)
		{
			r = line_move (ed, dir, LINE_SPOT_COLUMN);
		}
	}

	return r;
}

MlResult ml_editor_mouse (MlEditor *ed, MlKey key, MlHit hit)
{
	MlResult r       = ML_OK;
	int32_t  editing = (
		   ed->state == ML_STATE_MATH
		|| ed->state == ML_STATE_NOTE
	);
	int32_t  action  = (int32_t)key.cp;

	/* in the help viewer the wheel scrolls */
	if (ed->state == ML_STATE_HELP && key.code == ML_KEY_MOUSE)
	{
		MlKey step = {
			.code = ml_choose (
				action == ML_MOUSE_WHEEL_UP,
				ML_KEY_UP,
				ML_KEY_DOWN
			),
			.cp   = 0,
			.mods = 0
		};

		for (
			uint64_t i = 0;
			i < 3 && r == ML_OK
				&& (
					action == ML_MOUSE_WHEEL_UP
					|| action == ML_MOUSE_WHEEL_DOWN
				);
			i++
		)
		{
			r = help_key (ed, step);
		}

		action = ML_MOUSE_NONE;
	}

	if (
		   key.code != ML_KEY_MOUSE
		|| ed->state == ML_STATE_COMMAND
		|| ed->state == ML_STATE_HELP
		|| ed->picking
	)
	{
		action = ML_MOUSE_NONE;
	}

	if (action == ML_MOUSE_WHEEL_UP && editing)
	{
		r = mouse_wheel (ed, -1);
	}

	else if (action == ML_MOUSE_WHEEL_DOWN && editing)
	{
		r = mouse_wheel (ed, 1);
	}

	else if (action == ML_MOUSE_PRESS && hit.valid && hit.folded)
	{
		ed->mouse_down = 0;
		ed->selecting  = 0;
		r              = settle (ed);

		if (r == ML_OK)
		{
			ml_session_set_cursor (&ed->session, hit.line);
			enter_select (ed, hit.line);
		}
	}

	else if (action == ML_MOUSE_PRESS && hit.valid)
	{
		ed->selecting  = 0;
		ed->mouse_down = 1;
		ed->mouse_line = hit.line;
		ed->mouse_byte = hit.byte;

		if (ed->state == ML_STATE_SELECT)
		{
			ml_session_set_cursor (&ed->session, hit.line);
			r = resume (ed);
		}

		if (r == ML_OK)
		{
			r = goto_pos (ed, hit.line, hit.byte);
		}
	}

	else if (
		   action == ML_MOUSE_DRAG
		&& ed->mouse_down
		&& hit.valid
		&& !hit.folded
		&& editing
	)
	{
		if (!ed->selecting)
		{
			ed->selecting  = 1;
			ed->sel_line   = ed->mouse_line;
			ed->sel_anchor = ed->mouse_byte;
		}

		r = goto_pos (ed, hit.line, hit.byte);
	}

	else if (action == ML_MOUSE_RELEASE)
	{
		ed->mouse_down = 0;
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
	MlResult r        = ML_OK;
	uint64_t previous = ed->tutorial.lesson;
	int32_t  control  = ed->tutorial.active && key.code == ML_KEY_F6;

	ml_str_clear (&ed->message);
	ml_session_checkpoint (&ed->session);

	if (control)
	{
		/* F6 skips a step of the tutorial, Shift+F6 goes back one */
		MlTutorialView v = tutorial_view (ed, key);

		if (key.mods & ML_MOD_SHIFT)
		{
			ml_tutorial_back (&ed->tutorial, &v);
		}

		else
		{
			ml_tutorial_skip (&ed->tutorial, &v);
		}
	}

	else if (key_action (ed, key) == ML_ACT_HELP && ed->state != ML_STATE_HELP)
	{
		r = help_key_f1 (ed);
	}

	else if (ed->state == ML_STATE_HELP)
	{
		r = help_key (ed, key);
	}

	else if (ed->state == ML_STATE_SELECT)
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

	if (
		   r == ML_OK
		&& !control
		&& ed->tutorial.active
		&& key.code != ML_KEY_MOUSE
		&& key.code != ML_KEY_RESIZE
	)
	{
		MlTutorialView v = tutorial_view (ed, key);

		ml_tutorial_observe (&ed->tutorial, &v);
		tutorial_save_progress (ed, previous);
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

	else if (ed->state == ML_STATE_HELP)
	{
		MlTextArg title = {
			.name  = "title",
			.value = ml_choose (
				ed->help.results,
				ml_str_view (ed->help.query),
				ml_str_view (ed->help.docs.pages.data [ed->help.page].title)
			)
		};

		r = ml_text_fill (out, ml_text ("ui.help_bar"), &title, 1);
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
