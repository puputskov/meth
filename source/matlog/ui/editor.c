#include <matlog/ui/editor.h>

#include <matlog/core/utf8.h>

static const char *const state_names [ML_STATE_COUNT] =
{
	[ML_STATE_NORMAL]  = "NORMAL",
	[ML_STATE_INSERT]  = "INSERT",
	[ML_STATE_NOTE]    = "NOTE",
	[ML_STATE_COMMAND] = "COMMAND"
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
	ml_str_init (&ed->yank.text);
	ml_str_init (&ed->message);
	ed->yank.kind     = ML_LINE_NONE;
	ed->yank.domain   = ML_DOMAIN_ALGEBRA;
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
	ml_str_free (&ed->yank.text);
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
		ml_rewriter_set_domain (&ed->rewriter, domain);
	}

	return r;
}

/* Index of a new line written below the cursor line. */
static uint64_t line_below_cursor (const MlEditor *ed)
{
	return ml_choose (
		ed->session.lines.len > 0,
		ed->session.cursor + 1,
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

	if (ed->state == ML_STATE_INSERT)
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

	else if (ed->state == ML_STATE_INSERT)
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

/* The line register. */
static MlResult yank_line (MlEditor *ed, uint64_t idx)
{
	MlLineView v = ml_session_get (&ed->session, idx);
	MlResult   r = ML_OK;

	if (v.kind != ML_LINE_NONE)
	{
		ml_str_clear (&ed->yank.text);
		r = ml_str_append (&ed->yank.text, v.text);

		if (r == ML_OK)
		{
			ed->yank.kind   = v.kind;
			ed->yank.domain = v.domain;
		}
	}

	return r;
}

static MlResult paste_line (MlEditor *ed, int32_t below)
{
	MlResult r   = ML_OK;
	uint64_t idx = ml_choose (
		below,
		line_below_cursor (ed),
		ed->session.cursor
	);

	if (ed->yank.kind != ML_LINE_NONE)
	{
		r = ml_session_insert (
			&ed->session,
			idx,
			ed->yank.kind,
			ed->yank.domain,
			ml_str_view (ed->yank.text)
		);
	}

	if (r == ML_OK && ed->yank.kind != ML_LINE_NONE)
	{
		ml_session_set_cursor (&ed->session, idx);
	}

	return r;
}

static MlResult delete_line (MlEditor *ed)
{
	MlResult r = yank_line (ed, ed->session.cursor);

	if (r == ML_OK && ed->session.lines.len > 0)
	{
		r = ml_session_delete (&ed->session, ed->session.cursor);
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
		ml_session_set_cursor (&ed->session, 0);
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

/* The second key of d, y and g. */
static MlResult pending_key (MlEditor *ed, uint32_t first, uint32_t ch)
{
	MlResult r = ML_OK;

	if (first == 'd' && ch == 'd')
	{
		r = delete_line (ed);
	}

	else if (first == 'y' && ch == 'y')
	{
		r = yank_line (ed, ed->session.cursor);
	}

	else if (first == 'g')
	{
		r = goto_key (ed, ch);
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

static MlResult normal_key (MlEditor *ed, MlKey key)
{
	MlResult r  = ML_OK;
	uint32_t ch = ml_choose (
		key.code == ML_KEY_CHAR && !(key.mods & (ML_MOD_CTRL | ML_MOD_ALT)),
		key.cp,
		0
	);

	if (ed->pending != 0)
	{
		uint32_t first = ed->pending;

		ed->pending = 0;
		r           = pending_key (ed, first, ch);
	}

	else if (is_domain_cycle (key))
	{
		r = cycle_domain (ed, key);
	}

	else if (ch == 'j' || key.code == ML_KEY_DOWN)
	{
		ml_session_move_cursor (&ed->session, 1);
	}

	else if (ch == 'k' || key.code == ML_KEY_UP)
	{
		ml_session_move_cursor (&ed->session, -1);
	}

	else if (ch == 'G')
	{
		ml_session_set_cursor (&ed->session, ed->session.lines.len);
	}

	else if (ch == 'd' || ch == 'y' || ch == 'g')
	{
		ed->pending = ch;
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

typedef struct LineNumber
{
	int32_t  valid;
	uint64_t value;
} LineNumber;

/* A command made only of digits is a line number. */
static LineNumber parse_line_number (MlSlice s)
{
	LineNumber number =
	{
		.valid = s.len > 0 && s.len <= 18,
		.value = 0
	};

	for (uint64_t i = 0; i < s.len && number.valid; i++)
	{
		number.valid = s.data [i] >= '0' && s.data [i] <= '9';
		number.value = number.value * 10 + (uint64_t)(s.data [i] - '0');
	}

	return number;
}

/* Runs the command typed after ':'. */
static MlResult run_command (MlEditor *ed)
{
	MlResult r     = ML_OK;
	MlSlice  line  = trim (ml_str_view (ed->edit.text));
	uint64_t split = 0;
	MlSlice  cmd;
	MlSlice  arg;

	while (split < line.len && line.data [split] != ' ')
	{
		split++;
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

	else if (parse_line_number (cmd).valid)
	{
		/* :12 goes to line 12 */
		uint64_t number = parse_line_number (cmd).value;

		ml_session_set_cursor (
			&ed->session,
			ml_choose (number > 0, number - 1, 0)
		);
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

	if (ed->state == ML_STATE_NORMAL)
	{
		r = normal_key (ed, key);
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
