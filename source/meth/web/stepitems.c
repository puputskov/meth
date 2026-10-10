#include "internal.h"

#include <meth/core/vec.h>
#include <meth/export/math.h>

/*
 * The steps of a line for a page: the text rows of ml_line_steps_raw as a JSON
 * array of items, so the page can set a matrix as a matrix.
 *
 *	{ "t": "text",   "text": "Eliminate below the diagonal:" }
 *	{ "t": "rule",   "text": "d/dx (x³) = 3x²", "note": "(power rule)" }
 *	{ "t": "matrix", "math": "<math>...</math>", "rows": [the rows as text] }
 *
 * A matrix is found by the pieces of its brackets (see ml_mat_typeset): the
 * rows from a line that starts with a top left piece to the one with the
 * bottom left piece, or one line in [ ]. Its cells are written out as a
 * matrix and set by ml_math_typeset; if that fails the rows stay text.
 */

ML_VEC_DEFINE (RowVec, MlSlice)

typedef enum RowKind
{
	ROW_TEXT = 0,
	ROW_TOP,
	ROW_MIDDLE,
	ROW_BOTTOM,
	ROW_SINGLE
} RowKind;

#define PIECE_LEN 3 /* the bracket pieces are one code point of 3 bytes */

static int32_t has_prefix (MlSlice s, const char *p)
{
	MlSlice w = ml_slice_cstr (p);

	return s.len >= w.len && ml_slice_equal (ml_slice_sub (s, 0, w.len), w);
}

static int32_t has_suffix (MlSlice s, const char *p)
{
	MlSlice w = ml_slice_cstr (p);

	return s.len >= w.len
		&& ml_slice_equal (ml_slice_sub (s, s.len - w.len, w.len), w);
}

/* The characters the cells of a matrix are written with. */
static int32_t is_cell_text (MlSlice s)
{
	int32_t ok = s.len > 0;

	for (uint64_t i = 0; i < s.len && ok; i++)
	{
		uint8_t c = (uint8_t)s.data [i];

		ok = (
			   (c >= '0' && c <= '9')
			|| c == '/'
			|| c == '-'
			|| c == '+'
			|| c == '.'
			|| c == ' '
			|| c >= 0x80
		);
	}

	return ok;
}

static RowKind row_kind (MlSlice row)
{
	RowKind kind = ROW_TEXT;

	if (has_prefix (row, "\xE2\x8E\xA1") && has_suffix (row, "\xE2\x8E\xA4"))
	{
		kind = ROW_TOP;
	}

	else if (
		   has_prefix (row, "\xE2\x8E\xA2")
		&& has_suffix (row, "\xE2\x8E\xA5")
	)
	{
		kind = ROW_MIDDLE;
	}

	else if (
		   has_prefix (row, "\xE2\x8E\xA3")
		&& has_suffix (row, "\xE2\x8E\xA6")
	)
	{
		kind = ROW_BOTTOM;
	}

	else if (
		   has_prefix (row, "[")
		&& has_suffix (row, "]")
		&& row.len > 2
		&& is_cell_text (ml_slice_sub (row, 1, row.len - 2))
	)
	{
		kind = ROW_SINGLE;
	}

	return kind;
}

/* The part of a matrix row between its bracket pieces. */
static MlSlice inside (MlSlice row, RowKind kind)
{
	uint64_t piece = ml_choose (kind == ROW_SINGLE, 1, PIECE_LEN);

	return ml_slice_sub (row, piece, row.len - 2 * piece);
}

/* Appends "[c, d]" for the cells of one row. */
static MlResult append_row (MlStr *src, MlSlice cells)
{
	MlResult r     = ml_str_append (src, ml_slice_cstr ("["));
	uint64_t pos   = 0;
	int32_t  first = 1;

	while (r == ML_OK && pos < cells.len)
	{
		uint64_t end = pos;

		while (end < cells.len && cells.data [end] == ' ')
		{
			end++;
		}

		pos = end;

		while (end < cells.len && cells.data [end] != ' ')
		{
			end++;
		}

		if (end > pos)
		{
			r = ml_choose (
				first,
				r,
				ml_str_append (src, ml_slice_cstr (", "))
			);
			r = ml_choose (
				r == ML_OK,
				ml_str_append (src, ml_slice_sub (cells, pos, end - pos)),
				r
			);
			first = 0;
		}

		pos = end;
	}

	return ml_choose (
		r == ML_OK,
		ml_str_append (src, ml_slice_cstr ("]")),
		r
	);
}

/*
 * The matrix of rows first..last as an item. Sets *done to 1 when it was set
 * as a matrix, 0 when the rows have to be shown as text.
 */
static MlResult matrix_item (
	MlStr          *out,
	const RowVec   *rows,
	uint64_t       first,
	uint64_t       last,
	int32_t        *done
)
{
	MlResult r = ML_OK;
	MlStr    src;
	MlStr    math;

	ml_str_init (&src);
	ml_str_init (&math);
	r = ml_str_append (&src, ml_slice_cstr ("["));

	for (uint64_t i = first; i <= last && r == ML_OK; i++)
	{
		MlSlice row = rows->data [i];

		r = ml_choose (
			i > first,
			ml_str_append (&src, ml_slice_cstr (", ")),
			r
		);
		r = ml_choose (
			r == ML_OK,
			append_row (&src, inside (row, row_kind (row))),
			r
		);
	}

	r = ml_choose (
		r == ML_OK,
		ml_str_append (&src, ml_slice_cstr ("]")),
		r
	);
	r = ml_choose (
		r == ML_OK,
		ml_str_append (&math, ml_slice_cstr ("<math>")),
		r
	);

	if (r == ML_OK)
	{
		MlResult typeset = ml_math_typeset (
			&math,
			ML_MATH_MATHML,
			ml_str_view (src)
		);

		*done = typeset == ML_OK;
		r     = ml_choose (typeset == ML_ERR_PARSE, ML_OK, typeset);
	}

	r = ml_choose (
		r == ML_OK,
		ml_str_append (&math, ml_slice_cstr ("</math>")),
		r
	);

	if (r == ML_OK && *done)
	{
		r = lit (r, out, "{\"t\":\"matrix\",\"math\":");
		r = str (r, out, ml_str_view (math));
		r = lit (r, out, "}");
	}

	ml_str_free (&src);
	ml_str_free (&math);
	return r;
}

/* A line of text, or of a rule with its note behind a tab. */
static MlResult text_item (MlStr *out, MlSlice row)
{
	MlResult r   = ML_OK;
	uint64_t tab = row.len;

	for (uint64_t i = 0; i < row.len && tab == row.len; i++)
	{
		tab = ml_choose (row.data [i] == '\t', i, tab);
	}

	if (tab < row.len)
	{
		r = lit (r, out, "{\"t\":\"rule\",\"text\":");
		r = str (r, out, ml_slice_sub (row, 0, tab));
		r = lit (r, out, ",\"note\":");
		r = str (r, out, ml_slice_sub (row, tab + 1, row.len - tab - 1));
		r = lit (r, out, "}");
	}

	else
	{
		r = lit (r, out, "{\"t\":\"text\",\"text\":");
		r = str (r, out, row);
		r = lit (r, out, "}");
	}

	return r;
}

MlResult web_step_items (MlStr *out, MlSlice raw)
{
	MlResult r   = ML_OK;
	RowVec   rows;
	uint64_t pos = 0;
	uint64_t i   = 0;
	int32_t  first = 1;

	RowVec_init (&rows);

	while (r == ML_OK && pos <= raw.len && raw.len > 0)
	{
		uint64_t end = pos;
		MlSlice  row;

		while (end < raw.len && raw.data [end] != '\n')
		{
			end++;
		}

		row = ml_slice_sub (raw, pos, end - pos);

		while (row.len > 0 && row.data [row.len - 1] == '\r')
		{
			row.len--;
		}

		r   = RowVec_push (&rows, row);
		pos = end + 1;
	}

	r = lit (r, out, "[");

	while (r == ML_OK && i < rows.len)
	{
		RowKind  kind     = row_kind (rows.data [i]);
		uint64_t last     = i;
		int32_t  is_block = 0;
		int32_t  done     = 0;
		uint64_t used     = 1;

		if (kind == ROW_TOP)
		{
			last = i + 1;

			while (last < rows.len && row_kind (rows.data [last]) == ROW_MIDDLE)
			{
				last++;
			}

			is_block = last < rows.len
				&& row_kind (rows.data [last]) == ROW_BOTTOM;
		}

		else if (kind == ROW_SINGLE)
		{
			is_block = 1;
		}

		r = ml_choose (first, r, lit (r, out, ","));
		first = 0;

		if (is_block)
		{
			r    = matrix_item (out, &rows, i, last, &done);
			used = last - i + 1;
		}

		if (r == ML_OK && !done)
		{
			/* not a matrix, or one that could not be set: the row as text */
			r    = text_item (out, rows.data [i]);
			used = 1;
		}

		i += used;
	}

	RowVec_free (&rows);
	return lit (r, out, "]");
}
