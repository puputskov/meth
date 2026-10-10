#include <matlog/ui/plotpanel.h>

#include <matlog/ui/plot.h>

uint64_t ml_plot_panel_count (const MlEditor *ed)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < ed->session.lines.len; i++)
	{
		if (
			   ed->session.lines.data [i].plotted
			&& ml_results_get (&ed->results, i).plot != NULL
		)
		{
			count++;
		}
	}

	return count;
}

/* The index of the n-th line (from 0) that shows a plot. */
static uint64_t plot_line (const MlEditor *ed, uint64_t n)
{
	uint64_t found = ed->session.lines.len;

	for (uint64_t i = 0; i < ed->session.lines.len; i++)
	{
		if (
			   found == ed->session.lines.len
			&& ed->session.lines.data [i].plotted
			&& ml_results_get (&ed->results, i).plot != NULL
		)
		{
			if (n == 0)
			{
				found = i;
			}

			else
			{
				n--;
			}
		}
	}

	return found;
}

/* One row of the panel with the text, cut or padded to the width. */
static MlResult text_row (
	MlRowVec       *out,
	const MlEditor *ed,
	MlStyle        style,
	MlSlice        text,
	uint32_t       width
)
{
	MlStr    row;
	MlResult r;

	ml_str_init (&row);
	r = ml_panel_cell (&row, ed, style, text, width);

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

/* The header and the picture of the plot of line idx, in `rows` rows. */
static MlResult plot_block (
	MlRowVec       *out,
	const MlEditor *ed,
	uint64_t       idx,
	uint32_t       width,
	uint32_t       rows
)
{
	MlResult     r       = ML_OK;
	MlResultView v       = ml_results_get (&ed->results, idx);
	MlStyle      head    = ml_panel_style (ed, ML_CLASS_GUTTER, 1);
	MlStr        title;
	MlStr        picture;
	uint64_t     cursor  = ml_choose (
		ed->state == ML_STATE_MATH || ed->state == ML_STATE_NOTE,
		ed->edit_index,
		ed->session.cursor
	);
	uint64_t     pos     = 0;

	ml_str_init (&title);
	ml_str_init (&picture);
	head.bold = 1;

	r = ml_str_appendf (
		&title,
		"%splot  line %u  %.*s",
		ml_choose (idx == cursor, "*", " "),
		(uint32_t)(idx + 1),
		(int32_t)v.text.len,
		v.text.data
	);

	if (r == ML_OK && rows > 0)
	{
		r = text_row (out, ed, head, ml_str_view (title), width);
	}

	if (r == ML_OK && rows > 1)
	{
		r = ml_plot_draw (&picture, v.plot, width, rows - 1);
	}

	for (uint32_t k = 1; k < rows && r == ML_OK; k++)
	{
		MlSlice all  = ml_str_view (picture);
		uint64_t end = pos;

		while (end < all.len && all.data [end] != '\n')
		{
			end++;
		}

		r = text_row (
			out,
			ed,
			ml_panel_style (ed, ML_CLASS_RESULT, 0),
			ml_slice_sub (all, pos, end - pos),
			width
		);
		pos = ml_choose (end < all.len, end + 1, end);
	}

	ml_str_free (&title);
	ml_str_free (&picture);

	return r;
}

MlResult ml_plot_panel (
	MlRowVec       *out,
	const MlEditor *ed,
	uint32_t       width,
	uint32_t       rows
)
{
	MlResult r       = ML_OK;
	uint64_t count   = ml_plot_panel_count (ed);
	uint64_t fit     = ml_choose (
		rows / ML_PLOT_PANEL_MIN > 0,
		rows / ML_PLOT_PANEL_MIN,
		1
	);
	uint64_t shown   = ml_choose (count < fit, count, fit);
	uint64_t hidden  = count - shown;
	uint32_t used    = 0;
	uint32_t room    = rows - (uint32_t)ml_choose (hidden > 0, 1, 0);

	for (uint64_t k = 0; k < shown && r == ML_OK; k++)
	{
		uint32_t share = (uint32_t)(room / shown);

		r = plot_block (out, ed, plot_line (ed, k), width, share);
		used += share;
	}

	if (r == ML_OK && hidden > 0)
	{
		MlStr more;

		ml_str_init (&more);
		r = ml_str_appendf (
			&more,
			" \xE2\x80\xA6 +%u more",
			(uint32_t)hidden
		);

		if (r == ML_OK)
		{
			r = text_row (
				out,
				ed,
				ml_panel_style (ed, ML_CLASS_GUTTER, 1),
				ml_str_view (more),
				width
			);
			used++;
		}

		ml_str_free (&more);
	}

	for (uint32_t k = used; k < rows && r == ML_OK; k++)
	{
		r = text_row (
			out,
			ed,
			ml_panel_style (ed, ML_CLASS_TEXT, 0),
			ml_slice ("", 0),
			width
		);
	}

	return r;
}
