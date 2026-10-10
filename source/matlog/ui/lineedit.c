#include <matlog/ui/lineedit.h>

#include <matlog/core/utf8.h>

void ml_lineedit_init (MlLineEdit *e)
{
	ml_str_init (&e->text);
	e->cursor = 0;
}

void ml_lineedit_free (MlLineEdit *e)
{
	ml_str_free (&e->text);
	e->cursor = 0;
}

void ml_lineedit_clear (MlLineEdit *e)
{
	ml_str_clear (&e->text);
	e->cursor = 0;
}

MlResult ml_lineedit_set (MlLineEdit *e, MlSlice text)
{
	MlStr    copy;
	MlResult r;

	ml_str_init (&copy);
	r = ml_str_append (&copy, text);

	if (r == ML_OK)
	{
		ml_str_free (&e->text);
		e->text   = copy;
		e->cursor = copy.len;
	}

	else
	{
		ml_str_free (&copy);
	}

	return r;
}

MlResult ml_lineedit_insert (MlLineEdit *e, MlSlice text)
{
	MlResult r = ml_str_insert (&e->text, e->cursor, text);

	if (r == ML_OK)
	{
		e->cursor += text.len;
	}

	return r;
}

MlResult ml_lineedit_insert_cp (MlLineEdit *e, uint32_t cp)
{
	MlEncoded enc = ml_utf8_encode (cp);

	return ml_choose (
		enc.size > 0,
		ml_lineedit_insert (e, ml_slice (enc.bytes, enc.size)),
		ML_ERR_UTF8
	);
}

/* Is the code point that ends at pos a word character? */
static int32_t word_before (MlSlice s, uint64_t pos)
{
	uint64_t start = ml_utf8_prev (s, pos);

	return ml_cp_is_word (
		ml_utf8_decode (ml_slice_sub (s, start, pos - start)).cp
	);
}

/* Is the code point that starts at pos a word character? */
static int32_t word_at (MlSlice s, uint64_t pos)
{
	return ml_cp_is_word (
		ml_utf8_decode (ml_slice_sub (s, pos, s.len - pos)).cp
	);
}

static uint64_t word_left_pos (MlSlice s, uint64_t pos)
{
	uint64_t p = pos;

	while (p > 0 && !word_before (s, p))
	{
		p = ml_utf8_prev (s, p);
	}

	while (p > 0 && word_before (s, p))
	{
		p = ml_utf8_prev (s, p);
	}

	return p;
}

static uint64_t word_right_pos (MlSlice s, uint64_t pos)
{
	uint64_t p = pos;

	while (p < s.len && !word_at (s, p))
	{
		p = ml_utf8_next (s, p);
	}

	while (p < s.len && word_at (s, p))
	{
		p = ml_utf8_next (s, p);
	}

	return p;
}

/*
 * Removes the bytes between from and to (from <= to) and puts the cursor at
 * from.
 */
static void remove_range (MlLineEdit *e, uint64_t from, uint64_t to)
{
	ml_str_erase (&e->text, from, to - from);
	e->cursor = from;
}

void ml_lineedit_backspace (MlLineEdit *e)
{
	remove_range (
		e,
		ml_utf8_prev (ml_str_view (e->text), e->cursor),
		e->cursor
	);
}

void ml_lineedit_delete (MlLineEdit *e)
{
	uint64_t to = ml_utf8_next (ml_str_view (e->text), e->cursor);

	remove_range (e, e->cursor, to);
}

void ml_lineedit_delete_word_left (MlLineEdit *e)
{
	remove_range (
		e,
		word_left_pos (ml_str_view (e->text), e->cursor),
		e->cursor
	);
}

void ml_lineedit_delete_word_right (MlLineEdit *e)
{
	uint64_t to = word_right_pos (ml_str_view (e->text), e->cursor);

	remove_range (e, e->cursor, to);
}

void ml_lineedit_kill_to_start (MlLineEdit *e)
{
	remove_range (e, 0, e->cursor);
}

void ml_lineedit_kill_to_end (MlLineEdit *e)
{
	remove_range (e, e->cursor, e->text.len);
}

void ml_lineedit_left (MlLineEdit *e)
{
	e->cursor = ml_utf8_prev (ml_str_view (e->text), e->cursor);
}

void ml_lineedit_right (MlLineEdit *e)
{
	e->cursor = ml_utf8_next (ml_str_view (e->text), e->cursor);
}

void ml_lineedit_home (MlLineEdit *e)
{
	e->cursor = 0;
}

void ml_lineedit_end (MlLineEdit *e)
{
	e->cursor = e->text.len;
}

void ml_lineedit_word_left (MlLineEdit *e)
{
	e->cursor = word_left_pos (ml_str_view (e->text), e->cursor);
}

void ml_lineedit_word_right (MlLineEdit *e)
{
	e->cursor = word_right_pos (ml_str_view (e->text), e->cursor);
}

uint64_t ml_lineedit_width (const MlLineEdit *e)
{
	return ml_utf8_width (ml_str_view (e->text));
}

uint64_t ml_lineedit_cursor_col (const MlLineEdit *e)
{
	return ml_utf8_width (ml_slice_sub (ml_str_view (e->text), 0, e->cursor));
}

MlEditResult ml_lineedit_handle_key (MlLineEdit *e, MlKey key)
{
	MlEditResult res = {
		.result  = ML_OK,
		.handled = 1
	};

	int32_t  ctrl      = (key.mods & ML_MOD_CTRL) != 0;
	int32_t  alt       = (key.mods & ML_MOD_ALT) != 0;
	int32_t  ctrl_char = key.code == ML_KEY_CHAR && ctrl && !alt;
	uint32_t ch        = ml_choose (
		key.code == ML_KEY_CHAR,
		key.cp,
		0
	);

	if (key.code == ML_KEY_CHAR && !ctrl && !alt)
	{
		res.result = ml_lineedit_insert_cp (e, key.cp);
	}

	else if (ctrl_char && ch == 'a')
	{
		ml_lineedit_home (e);
	}

	else if (ctrl_char && ch == 'e')
	{
		ml_lineedit_end (e);
	}

	else if (ctrl_char && ch == 'k')
	{
		ml_lineedit_kill_to_end (e);
	}

	else if (ctrl_char && ch == 'u')
	{
		ml_lineedit_kill_to_start (e);
	}

	else if (ctrl_char && ch == 'w')
	{
		ml_lineedit_delete_word_left (e);
	}

	else if (key.code == ML_KEY_LEFT && ctrl)
	{
		ml_lineedit_word_left (e);
	}

	else if (key.code == ML_KEY_LEFT && !alt)
	{
		ml_lineedit_left (e);
	}

	else if (key.code == ML_KEY_RIGHT && ctrl)
	{
		ml_lineedit_word_right (e);
	}

	else if (key.code == ML_KEY_RIGHT && !alt)
	{
		ml_lineedit_right (e);
	}

	else if (key.code == ML_KEY_HOME)
	{
		ml_lineedit_home (e);
	}

	else if (key.code == ML_KEY_END)
	{
		ml_lineedit_end (e);
	}

	else if (key.code == ML_KEY_BACKSPACE && ctrl)
	{
		ml_lineedit_delete_word_left (e);
	}

	else if (key.code == ML_KEY_BACKSPACE)
	{
		ml_lineedit_backspace (e);
	}

	else if (key.code == ML_KEY_DELETE && ctrl)
	{
		ml_lineedit_delete_word_right (e);
	}

	else if (key.code == ML_KEY_DELETE)
	{
		ml_lineedit_delete (e);
	}

	else
	{
		res.handled = 0;
	}

	return res;
}

uint64_t ml_lineedit_start_col (const MlLineEdit *e, uint64_t avail)
{
	uint64_t cursor_col = ml_lineedit_cursor_col (e);

	return ml_choose (
		cursor_col >= avail,
		cursor_col - avail + 1,
		0
	);
}

MlTextWindow ml_text_window (
	MlStr    *out,
	MlSlice  text,
	uint64_t start_col,
	uint64_t avail
)
{
	MlTextWindow w = {
		.result    = ML_OK,
		.first_col = start_col
	};

	uint64_t pos       = 0;
	uint64_t col       = 0;
	int32_t  done      = 0;
	int32_t  any       = 0;
	int32_t  prev_show = 0;

	while (w.result == ML_OK && pos < text.len && !done)
	{
		MlDecoded d = ml_utf8_decode (
			ml_slice_sub (text, pos, text.len - pos)
		);
		uint64_t cw   = (uint64_t)ml_cp_width (d.cp);
		int32_t  show = 0;

		if (cw == 0)
		{
			/* a combining mark belongs to the character before it */
			show = prev_show;
		}

		else
		{
			show        = col >= start_col && col + cw - start_col <= avail;
			done        = col + cw - start_col > avail && col >= start_col;
			w.first_col = ml_choose (
				(show && !any),
				col,
				w.first_col
			);
			any = any || show;
		}

		if (show)
		{
			w.result = ml_str_append (out, ml_slice_sub (text, pos, d.size));
		}

		prev_show = show;
		col      += cw;
		pos      += d.size;
	}

	return w;
}

MlResult ml_lineedit_render (
	MlStr            *out,
	const MlLineEdit *e,
	MlSlice          prompt,
	uint32_t         prompt_width,
	uint32_t         cols
)
{
	MlResult r     = ML_OK;
	uint64_t avail = ml_choose (
		cols > (uint64_t)prompt_width + 1,
		cols - prompt_width - 1,
		1
	);
	uint64_t     cursor_col = ml_lineedit_cursor_col (e);
	uint64_t     start_col  = ml_lineedit_start_col (e, avail);
	MlTextWindow win        = {
		.result    = ML_OK,
		.first_col = 0
	};

	r = ml_str_append (out, ml_slice_cstr ("\r"));

	if (r == ML_OK)
	{
		r = ml_str_append (out, prompt);
	}

	if (r == ML_OK)
	{
		win = ml_text_window (out, ml_str_view (e->text), start_col, avail);
		r   = win.result;
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr ("\x1b[K\r"));
	}

	if (r == ML_OK)
	{
		uint64_t offset = ml_choose (
			cursor_col > win.first_col,
			cursor_col - win.first_col,
			0
		);
		uint64_t screen_col = (uint64_t)prompt_width + offset;

		if (screen_col > 0)
		{
			r = ml_str_appendf (out, "\x1b[%uC", (uint32_t)screen_col);
		}
	}

	return r;
}
