#include <matlog/ui/screen.h>

#include <matlog/core/utf8.h>

#define STYLE_REVERSE "\x1b[7m"
#define STYLE_DIM     "\x1b[2m"
#define STYLE_RESET   "\x1b[0m"

/*
 * Gutter of a math line in each domain. All gutters are ML_GUTTER_WIDTH wide.
 */
static const char *const math_gutters [ML_DOMAIN_COUNT] =
{
	[ML_DOMAIN_ALGEBRA]   = "alg> ",
	[ML_DOMAIN_EQUATIONS] = "eqn> ",
	[ML_DOMAIN_MATRIX]    = "mat> ",
	[ML_DOMAIN_VECTOR]    = "vec> ",
	[ML_DOMAIN_LAMBDA]    = "lam> ",
	[ML_DOMAIN_PGA2]      = "pg2> ",
	[ML_DOMAIN_PGA3]      = "pg3> ",
	[ML_DOMAIN_BOOLEAN]   = "bol> "
};

void ml_screen_init (MlScreen *s, uint32_t cols, uint32_t rows)
{
	s->cols  = cols;
	s->rows  = rows;
	s->top   = 0;
	s->force = 1;
	MlRowVec_init (&s->drawn);
}

static void free_rows (MlRowVec *rows)
{
	for (uint64_t i = 0; i < rows->len; i++)
	{
		ml_str_free (&rows->data [i]);
	}

	MlRowVec_free (rows);
}

void ml_screen_free (MlScreen *s)
{
	free_rows (&s->drawn);
}

void ml_screen_resize (MlScreen *s, uint32_t cols, uint32_t rows)
{
	s->cols  = cols;
	s->rows  = rows;
	s->force = 1;
	free_rows (&s->drawn);
}

void ml_layout_free (MlLayout *layout)
{
	free_rows (&layout->rows);
}

static MlSlice gutter_for (MlLineKind kind, MlDomain domain)
{
	const char *gutter = "???> ";

	if (kind == ML_LINE_MATH)
	{
		gutter = math_gutters [domain];
	}

	else if (kind == ML_LINE_NOTE)
	{
		gutter = "  #  ";
	}

	else if (kind == ML_LINE_INCLUDE)
	{
		gutter = "inc> ";
	}

	else if (kind == ML_LINE_BLANK)
	{
		gutter = "     ";
	}

	return ml_slice_cstr (gutter);
}

static MlResult append_cstr (MlStr *out, const char *text)
{
	return ml_str_append (out, ml_slice_cstr (text));
}

/* One row of the line area: gutter, then the text clipped to the width. */
static MlResult append_line_row (
	MlStr      *out,
	MlLineView line,
	int32_t    highlight,
	uint32_t   cols
)
{
	MlResult r     = ML_OK;
	int32_t  dim   = line.kind == ML_LINE_NOTE;
	int32_t  empty = line.kind == ML_LINE_BLANK && !highlight;
	uint64_t avail = ml_choose (
		cols > ML_GUTTER_WIDTH,
		cols - ML_GUTTER_WIDTH,
		0
	);
	MlSlice gutter = ml_slice_sub (
		gutter_for (line.kind, line.domain),
		0,
		cols
	);

	if (!empty && highlight)
	{
		r = append_cstr (out, STYLE_REVERSE);
	}

	else if (!empty && dim)
	{
		r = append_cstr (out, STYLE_DIM);
	}

	if (r == ML_OK && !empty)
	{
		r = ml_str_append (out, gutter);
	}

	if (r == ML_OK && !empty && highlight)
	{
		r = append_cstr (out, STYLE_RESET);
	}

	if (r == ML_OK && !empty && highlight && dim)
	{
		r = append_cstr (out, STYLE_DIM);
	}

	if (r == ML_OK && !empty)
	{
		r = ml_text_window (out, line.text, 0, avail).result;
	}

	if (r == ML_OK && !empty && (dim || highlight))
	{
		r = append_cstr (out, STYLE_RESET);
	}

	return r;
}

typedef struct EditRow
{
	MlResult result;
	uint32_t cursor_col; /* 1 based */
} EditRow;

/* The row of the line being typed. */
static EditRow append_edit_row (
	MlStr          *out,
	const MlEditor *ed,
	uint32_t       cols
)
{
	EditRow row =
	{
		.result     = ML_OK,
		.cursor_col = 1
	};
	MlSlice gutter = gutter_for (
		ml_choose (
			ed->state == ML_STATE_NOTE,
			ML_LINE_NOTE,
			ML_LINE_MATH
		),
		ed->edit_domain
	);
	uint64_t avail = ml_choose (
		cols > ML_GUTTER_WIDTH + 1,
		cols - ML_GUTTER_WIDTH - 1,
		1
	);
	MlTextWindow w =
	{
		.result    = ML_OK,
		.first_col = 0
	};

	row.result = ml_str_append (out, ml_slice_sub (gutter, 0, cols));

	if (row.result == ML_OK)
	{
		w = ml_text_window (
			out,
			ml_str_view (ed->edit.text),
			ml_lineedit_start_col (&ed->edit, avail),
			avail
		);
		row.result = w.result;
	}

	if (row.result == ML_OK)
	{
		uint64_t cursor_col = ml_lineedit_cursor_col (&ed->edit);

		row.cursor_col = (uint32_t)(
			ML_GUTTER_WIDTH + 1
			+ ml_choose (
				cursor_col > w.first_col,
				cursor_col - w.first_col,
				0
			)
		);
	}

	return row;
}

static MlResult append_spaces (MlStr *out, uint64_t count)
{
	MlResult r = ML_OK;

	for (uint64_t i = 0; i < count && r == ML_OK; i++)
	{
		r = append_cstr (out, " ");
	}

	return r;
}

/* The status line: the state on the left, file and dirty mark on the right. */
static MlResult append_status_row (
	MlStr          *out,
	const MlEditor *ed,
	uint32_t       cols
)
{
	MlResult r = ML_OK;
	MlStr    left;
	MlStr    right;
	MlStr    shown;
	uint64_t used;
	uint64_t right_w;

	ml_str_init (&left);
	ml_str_init (&right);
	ml_str_init (&shown);

	r = ml_editor_status (&left, ed);

	if (r == ML_OK)
	{
		r = ml_str_append (
			&right,
			ml_choose (
				ed->session.path.len > 0,
				ml_str_view (ed->session.path),
				ml_slice_cstr ("[no name]")
			)
		);
	}

	if (r == ML_OK && ed->session.dirty)
	{
		r = append_cstr (&right, " [+]");
	}

	right_w = ml_utf8_width (ml_str_view (right));

	if (r == ML_OK)
	{
		r = ml_text_window (&shown, ml_str_view (left), 0, cols).result;
	}

	used = ml_utf8_width (ml_str_view (shown));

	if (r == ML_OK && used + 1 + right_w <= cols)
	{
		r = append_spaces (&shown, cols - used - right_w);

		if (r == ML_OK)
		{
			r = ml_str_append (&shown, ml_str_view (right));
		}
	}

	else if (r == ML_OK)
	{
		r = append_spaces (&shown, cols - used);
	}

	if (r == ML_OK)
	{
		r = append_cstr (out, STYLE_REVERSE);
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_str_view (shown));
	}

	if (r == ML_OK)
	{
		r = append_cstr (out, STYLE_RESET);
	}

	ml_str_free (&shown);
	ml_str_free (&right);
	ml_str_free (&left);
	return r;
}

/*
 * The command line: ':' and the command, scrolled to keep its cursor visible.
 */
static EditRow append_command_row (
	MlStr          *out,
	const MlEditor *ed,
	uint32_t       cols
)
{
	EditRow row =
	{
		.result     = ML_OK,
		.cursor_col = 2
	};
	uint64_t avail = ml_choose (
		cols > 2,
		cols - 2,
		1
	);
	MlTextWindow w =
	{
		.result    = ML_OK,
		.first_col = 0
	};

	row.result = append_cstr (out, ":");

	if (row.result == ML_OK)
	{
		w = ml_text_window (
			out,
			ml_str_view (ed->edit.text),
			ml_lineedit_start_col (&ed->edit, avail),
			avail
		);
		row.result = w.result;
	}

	if (row.result == ML_OK)
	{
		uint64_t cursor_col = ml_lineedit_cursor_col (&ed->edit);

		row.cursor_col = (uint32_t)(
			2
			+ ml_choose (
				cursor_col > w.first_col,
				cursor_col - w.first_col,
				0
			)
		);
	}

	return row;
}

/* Scrolls so that focus (an index into total rows) is inside the area. */
static uint64_t scroll_top (
	uint64_t top,
	uint64_t focus,
	uint64_t total,
	uint64_t area
)
{
	uint64_t t = top;

	t = ml_choose (focus < t, focus, t);
	t = ml_choose (area > 0 && focus >= t + area, focus - area + 1, t);
	t = ml_choose (total > area && t > total - area, total - area, t);
	t = ml_choose (total <= area, 0, t);
	return t;
}

MlLayout ml_screen_layout (MlScreen *s, const MlEditor *ed)
{
	MlLayout layout =
	{
		.result = ML_OK,
		.cursor =
		{
			.row = 1,
			.col = 1
		}
	};
	int32_t editing = (
		   ed->state == ML_STATE_INSERT
		|| ed->state == ML_STATE_NOTE
	);
	int32_t  editing_new = editing && !ed->edit_existing;
	uint64_t total       = ed->session.lines.len + ml_choose (
		editing_new,
		1,
		0
	);
	uint64_t focus = ml_choose (
		editing,
		ed->edit_index,
		ed->session.cursor
	);
	uint64_t area = ml_choose (s->rows >= 2, s->rows - 1, 0);

	MlRowVec_init (&layout.rows);
	s->top = scroll_top (s->top, focus, total, area);

	for (uint64_t r = 0; r < s->rows && layout.result == ML_OK; r++)
	{
		MlStr row;

		ml_str_init (&row);

		if (r + 1 == s->rows && ed->state == ML_STATE_COMMAND)
		{
			EditRow e = append_command_row (&row, ed, s->cols);

			layout.result     = e.result;
			layout.cursor.row = (uint32_t)(r + 1);
			layout.cursor.col = e.cursor_col;
		}

		else if (r + 1 == s->rows)
		{
			layout.result = append_status_row (&row, ed, s->cols);
		}

		else if (s->top + r < total && editing && s->top + r == ed->edit_index)
		{
			EditRow e = append_edit_row (&row, ed, s->cols);

			layout.result     = e.result;
			layout.cursor.row = (uint32_t)(r + 1);
			layout.cursor.col = e.cursor_col;
		}

		else if (s->top + r < total)
		{
			uint64_t idx = s->top + r;
			uint64_t src = ml_choose (
				editing_new && idx > ed->edit_index,
				idx - 1,
				idx
			);
			int32_t highlight = !editing && src == ed->session.cursor;

			layout.result = append_line_row (
				&row,
				ml_session_get (&ed->session, src),
				highlight,
				s->cols
			);

			if (highlight && !editing && ed->state == ML_STATE_NORMAL)
			{
				layout.cursor.row = (uint32_t)(r + 1);
				layout.cursor.col = 1;
			}
		}

		if (layout.result == ML_OK)
		{
			layout.result = MlRowVec_push (&layout.rows, row);
		}

		if (layout.result != ML_OK)
		{
			ml_str_free (&row);
		}
	}

	if (layout.result != ML_OK)
	{
		ml_layout_free (&layout);
	}

	return layout;
}

MlResult ml_screen_render (MlScreen *s, MlStr *out, const MlEditor *ed)
{
	MlLayout layout = ml_screen_layout (s, ed);
	MlResult r      = layout.result;

	if (r == ML_OK)
	{
		r = append_cstr (out, "\x1b[?25l");
	}

	if (r == ML_OK && s->force)
	{
		r = append_cstr (out, "\x1b[2J");
	}

	for (uint64_t i = 0; i < layout.rows.len && r == ML_OK; i++)
	{
		MlSlice row     = ml_str_view (layout.rows.data [i]);
		int32_t changed = (
			   s->force
			|| i >= s->drawn.len
			|| !ml_slice_equal (row, ml_str_view (s->drawn.data [i]))
		);

		if (changed)
		{
			r = ml_str_appendf (out, "\x1b[%u;1H", (uint32_t)(i + 1));

			if (r == ML_OK)
			{
				r = ml_str_append (out, row);
			}

			if (r == ML_OK)
			{
				r = append_cstr (out, "\x1b[K");
			}
		}
	}

	if (r == ML_OK)
	{
		r = ml_str_appendf (
			out,
			"\x1b[%u;%uH\x1b[?25h",
			layout.cursor.row,
			layout.cursor.col
		);
	}

	if (r == ML_OK)
	{
		free_rows (&s->drawn);
		s->drawn = layout.rows;
		s->force = 0;
		MlRowVec_init (&layout.rows);
	}

	ml_layout_free (&layout);
	return r;
}
