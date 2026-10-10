#include <matlog/ui/tutorialpanel.h>

#include <matlog/core/utf8.h>

uint32_t ml_tutorial_panel_height (const MlEditor *ed, uint32_t rows)
{
	return ml_choose (
		ed->tutorial.active && rows >= ML_TUTORIAL_MIN_ROWS,
		ML_TUTORIAL_PANEL_ROWS,
		0
	);
}

/* The end of the line of text that starts at pos and fits width columns. */
static uint64_t line_end (MlSlice text, uint64_t pos, uint64_t width)
{
	uint64_t end      = pos;
	uint64_t last_gap = pos;
	uint64_t used     = 0;

	while (end < text.len && used < width)
	{
		uint64_t next = ml_utf8_next (text, end);

		used += ml_utf8_width (ml_slice_sub (text, end, next - end));

		if (text.data [end] == ' ')
		{
			last_gap = end;
		}

		end = next;
	}

	return ml_choose (
		end >= text.len,
		text.len,
		ml_choose (last_gap > pos, last_gap, end)
	);
}

/* Appends one row of the panel: the text in a style, padded to cols. */
static MlResult push_row (
	MlRowVec       *out,
	const MlEditor *ed,
	MlStyle        style,
	MlSlice        text,
	uint32_t       cols
)
{
	MlStr    row;
	MlResult r;

	ml_str_init (&row);
	r = ml_panel_cell (&row, ed, style, text, cols);

	if (r == ML_OK)
	{
		r = MlRowVec_push (out, row);
	}

	if (r != ML_OK)
	{
		ml_str_free (&row);
	}

	return r;
}

/* Appends up to `rows` rows of the text, wrapped at the spaces. */
static MlResult push_wrapped (
	MlRowVec       *out,
	const MlEditor *ed,
	MlStyle        style,
	MlSlice        text,
	uint32_t       cols,
	uint32_t       rows
)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	for (uint32_t k = 0; k < rows && r == ML_OK; k++)
	{
		uint64_t end   = line_end (text, pos, cols - 2);
		MlSlice  piece = ml_slice_sub (text, pos, end - pos);
		MlStr    line;

		ml_str_init (&line);
		r = ml_str_append (&line, ml_slice_cstr (" "));
		r = ml_choose (r == ML_OK, ml_str_append (&line, piece), r);

		/* the last row says that there is more */
		if (r == ML_OK && k + 1 == rows && end < text.len)
		{
			r = ml_str_append (&line, ml_slice_cstr (" \xE2\x80\xA6"));
		}

		r   = ml_choose (
			r == ML_OK,
			push_row (out, ed, style, ml_str_view (line), cols),
			r
		);
		pos = end;

		while (pos < text.len && text.data [pos] == ' ')
		{
			pos++;
		}

		ml_str_free (&line);
	}

	return r;
}

MlResult ml_tutorial_panel (
	MlRowVec       *out,
	const MlEditor *ed,
	uint32_t       cols,
	uint32_t       height
)
{
	MlResult r     = ML_OK;
	MlStep   step  = ml_tutorial_step (&ed->tutorial);
	MlStyle  bold  = ml_panel_style (ed, ML_CLASS_HELP_TITLE, 0);
	MlStyle  text  = ml_panel_style (ed, ML_CLASS_TEXT, 0);
	MlStyle  quiet = ml_panel_style (ed, ML_CLASS_GUTTER, 1);
	MlStr    title;

	ml_str_init (&title);
	bold.bold = 1;
	r         = ml_tutorial_title (&title, &ed->tutorial);
	r         = ml_choose (
		r == ML_OK,
		push_row (out, ed, bold, ml_str_view (title), cols),
		r
	);
	r         = ml_choose (
		r == ML_OK,
		push_wrapped (out, ed, text, ml_slice_cstr (step.text), cols, 3),
		r
	);

	/* the hint, or how to leave */
	if (r == ML_OK && height > 4)
	{
		MlStr line;

		ml_str_init (&line);
		r = ml_str_append (
			&line,
			ml_slice_cstr (
				ml_choose (
					ml_tutorial_hint_due (&ed->tutorial),
					" Hint: ",
					" F6 skip, Shift+F6 back, :tutorial show types the answer,"
					" :tutorial off leaves"
				)
			)
		);

		if (r == ML_OK && ml_tutorial_hint_due (&ed->tutorial))
		{
			r = ml_str_append (&line, ml_slice_cstr (step.hint));
		}

		r = ml_choose (
			r == ML_OK,
			push_row (
				out,
				ed,
				ml_choose (ml_tutorial_hint_due (&ed->tutorial), bold, quiet),
				ml_str_view (line),
				cols
			),
			r
		);
		ml_str_free (&line);
	}

	/* the rule between the panel and the lines */
	for (uint32_t k = (uint32_t)out->len; k < height && r == ML_OK; k++)
	{
		MlStr rule;

		ml_str_init (&rule);

		for (uint32_t c = 0; c < cols && r == ML_OK; c++)
		{
			r = ml_str_append (
				&rule,
				ml_slice_cstr (
					ml_choose (k + 1 == height, "\xE2\x94\x80", " ")
				)
			);
		}

		if (r == ML_OK)
		{
			r = MlRowVec_push (out, rule);
		}

		else
		{
			ml_str_free (&rule);
		}
	}

	ml_str_free (&title);
	return r;
}
