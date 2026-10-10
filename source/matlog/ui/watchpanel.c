#include <matlog/ui/watchpanel.h>

#include <matlog/core/utf8.h>
#include <matlog/session/watch.h>
#include <matlog/texts/texts.h>

#define STYLE_RESET "\x1b[0m"

uint32_t ml_watch_width (uint32_t cols)
{
	return ml_choose (cols >= 100, 40, ml_choose (cols >= 70, 30, 0));
}

/* The end of the escape sequence that starts at pos (ESC [ ... final byte). */
static uint64_t escape_end (MlSlice row, uint64_t pos)
{
	uint64_t end = pos + 2;

	while (end < row.len && !(row.data [end] >= 0x40 && row.data [end] <= 0x7E))
	{
		end++;
	}

	return ml_choose (end < row.len, end + 1, row.len);
}

uint64_t ml_row_width (MlSlice row)
{
	uint64_t width = 0;
	uint64_t pos   = 0;
	uint64_t start = 0;

	while (pos < row.len)
	{
		if (
			   row.data [pos] == 0x1b
			&& pos + 1 < row.len
			&& row.data [pos + 1] == '['
		)
		{
			width += ml_utf8_width (ml_slice_sub (row, start, pos - start));
			pos    = escape_end (row, pos);
			start  = pos;
		}

		else
		{
			pos++;
		}
	}

	return width + ml_utf8_width (ml_slice_sub (row, start, row.len - start));
}

/* The style the panel uses for a class: the theme's, or dim without one. */
MlStyle ml_panel_style (const MlEditor *ed, MlClass cls, int32_t dim)
{
	MlStyle plain = { .dim = dim };

	return ml_choose (ed->highlight, ed->theme.styles [cls], plain);
}

/*
 * Appends text cut or padded to exactly width columns, in a style; a text
 * that does not fit ends in an ellipsis.
 */
MlResult ml_panel_cell (
	MlStr          *out,
	const MlEditor *ed,
	MlStyle        style,
	MlSlice        text,
	uint64_t       width
)
{
	MlResult r    = ML_OK;
	uint64_t used = 0;
	uint64_t pos  = 0;
	int32_t  fits = ml_utf8_width (text) <= width;
	uint64_t room = ml_choose (
		fits,
		width,
		ml_choose (width > 0, width - 1, 0)
	);

	if (!ml_style_is_plain (style))
	{
		r = ml_style_sgr (out, style, ed->colors);
	}

	while (r == ML_OK && pos < text.len)
	{
		uint64_t next = ml_utf8_next (text, pos);
		MlSlice  cp   = ml_slice_sub (text, pos, next - pos);
		uint64_t w    = ml_utf8_width (cp);

		if (used + w <= room)
		{
			r     = ml_str_append (out, cp);
			used += w;
			pos   = next;
		}

		else
		{
			pos = text.len;
		}
	}

	if (r == ML_OK && !fits && width > 0)
	{
		r     = ml_str_append (out, ml_slice_cstr ("\xE2\x80\xA6"));
		used += 1;
	}

	for (uint64_t k = used; k < width && r == ML_OK; k++)
	{
		r = ml_str_append (out, ml_slice_cstr (" "));
	}

	if (r == ML_OK && !ml_style_is_plain (style))
	{
		r = ml_str_append (out, ml_slice_cstr (STYLE_RESET));
	}

	return r;
}

static MlResult header_row (
	MlStr          *row,
	const MlEditor *ed,
	uint32_t       width,
	uint64_t       line,
	uint64_t       count
)
{
	MlStr    text;
	MlResult r;
	MlStyle  style = ml_panel_style (ed, ML_CLASS_GUTTER, 1);

	ml_str_init (&text);
	style.bold = 1;
	r = ml_str_append (&text, ml_slice_cstr (" "));

	if (r == ML_OK)
	{
		char a [ML_TEXT_NUMBER];
		char b [ML_TEXT_NUMBER];

		r = ml_text_fill2 (
			&text,
			"ui.watch_title",
			"line",
			ml_text_number (a, line + 1),
			"count",
			ml_text_number (b, count)
		);
	}

	if (r == ML_OK)
	{
		r = ml_panel_cell (row, ed, style, ml_str_view (text), width);
	}

	ml_str_free (&text);
	return r;
}

static MlResult entry_row (
	MlStr            *row,
	const MlEditor   *ed,
	const MlWatchRow *w,
	uint32_t         width
)
{
	uint64_t name_w = 7;
	uint64_t type_w = ml_choose (width >= 36, 12, 10);
	uint64_t value_w = width - 1 - name_w - 1 - type_w - 1;
	int32_t  func   = w->type.len > 0 && w->type.data [w->type.len - 1] == ')';
	MlResult r;

	r = ml_panel_cell (
		row,
		ed,
		ml_panel_style (ed, ML_CLASS_KEYWORD, 0),
		ml_slice_cstr (ml_choose (w->fresh, "*", " ")),
		1
	);
	r = ml_choose (
		r == ML_OK,
		ml_panel_cell (
			row,
			ed,
			ml_panel_style (
				ed,
				ml_choose (func, ML_CLASS_FUNCTION, ML_CLASS_NAME),
				0
			),
			ml_str_view (w->name),
			name_w
		),
		r
	);
	r = ml_choose (r == ML_OK, ml_str_append (row, ml_slice_cstr (" ")), r);
	r = ml_choose (
		r == ML_OK,
		ml_panel_cell (
			row,
			ed,
			ml_panel_style (ed, ML_CLASS_GUTTER, 1),
			ml_str_view (w->type),
			type_w
		),
		r
	);
	r = ml_choose (r == ML_OK, ml_str_append (row, ml_slice_cstr (" ")), r);
	r = ml_choose (
		r == ML_OK,
		ml_panel_cell (
			row,
			ed,
			ml_panel_style (ed, ML_CLASS_RESULT, 1),
			ml_str_view (w->value),
			value_w
		),
		r
	);
	return r;
}

MlResult ml_watch_panel (
	MlRowVec       *out,
	const MlEditor *ed,
	uint32_t       width,
	uint32_t       rows
)
{
	MlResult      r     = ML_OK;
	MlWatchRowVec list;
	int32_t       editing = (
		   ed->state == ML_STATE_MATH
		|| ed->state == ML_STATE_NOTE
	);
	uint64_t      line    = ml_choose (
		editing,
		ed->edit_index,
		ed->session.cursor
	);
	uint64_t      room    = ml_choose (rows > 1, rows - 1, 0);

	MlWatchRowVec_init (&list);
	r = ml_watch_rows (&list, &ed->results, line);

	for (uint32_t k = 0; k < rows && r == ML_OK; k++)
	{
		MlStr row;

		ml_str_init (&row);

		if (k == 0)
		{
			r = header_row (&row, ed, width, line, list.len);
		}

		else if (list.len > room && k == rows - 1)
		{
			MlStr more;

			char n [ML_TEXT_NUMBER];

			ml_str_init (&more);
			ml_str_append (&more, ml_slice_cstr (" "));
			r = ml_text_fill2 (
				&more,
				"ui.more",
				"count",
				ml_text_number (n, list.len - (room - 1)),
				"",
				ml_slice ("", 0)
			);
			r = ml_choose (
				r == ML_OK,
				ml_panel_cell (
					&row,
					ed,
					ml_panel_style (ed, ML_CLASS_GUTTER, 1),
					ml_str_view (more),
					width
				),
				r
			);
			ml_str_free (&more);
		}

		else if (k - 1 < list.len)
		{
			r = entry_row (&row, ed, &list.data [k - 1], width);
		}

		else
		{
			r = ml_panel_cell (
				&row,
				ed,
				ml_panel_style (ed, ML_CLASS_TEXT, 0),
				ml_slice ("", 0),
				width
			);
		}

		if (r == ML_OK)
		{
			r = MlRowVec_push (out, row);
		}

		else
		{
			ml_str_free (&row);
		}
	}

	ml_watch_rows_free (&list);
	return r;
}
