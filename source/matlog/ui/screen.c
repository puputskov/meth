#include <matlog/ui/screen.h>

#include <matlog/core/utf8.h>
#include <matlog/ui/brackets.h>
#include <matlog/ui/ghost.h>
#include <matlog/ui/highlight.h>
#include <matlog/ui/hint.h>
#include <matlog/ui/markup.h>

#define STYLE_REVERSE "\x1b[7m"
#define STYLE_DIM     "\x1b[2m"
#define STYLE_RED     "\x1b[31m"
#define STYLE_RESET   "\x1b[0m"
#define STYLE_MATCH     "\x1b[1;4m"
#define STYLE_UNMATCHED "\x1b[1;31m"

/*
 * Gutter of a math line in each domain. All gutters are ML_GUTTER_WIDTH wide.
 */
static const char *const math_gutters [ML_DOMAIN_COUNT] =
{
	[ML_DOMAIN_ALGEBRA]   = "alg> ",
	[ML_DOMAIN_EQUATIONS] = "=  > ",
	[ML_DOMAIN_MATRIX]    = "mat> ",
	[ML_DOMAIN_VECTOR]    = "\xE2\x86\x92  > ", /* right arrow */
	[ML_DOMAIN_LAMBDA]    = "\xCE\xBB  > ",     /* lambda sign */
	[ML_DOMAIN_PGA2]      = "pg2> ",
	[ML_DOMAIN_PGA3]      = "pg3> ",
	[ML_DOMAIN_BOOLEAN]   = "\xE2\x88\xA7  > "  /* logical and */
};

void ml_screen_init (MlScreen *s, uint32_t cols, uint32_t rows)
{
	s->cols  = cols;
	s->rows  = rows;
	s->top   = 0;
	s->force = 1;
	s->hint_tick = 0;
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
	MlRowInfoVec_free (&layout->info);
}

/*
 * The gutter of a row. The first line of a block carries the label of the
 * block (alg>), the other lines of a code block a rule that ends in a corner
 * at the last line, text has none: only the numbers show where its lines are.
 */
static MlSlice gutter_for (
	MlLineKind kind,
	MlDomain   domain,
	int32_t    first,
	int32_t    last
)
{
	const char *gutter = "     ";

	if (kind == ML_LINE_NOTE)
	{
		gutter = "";
	}

	else if (kind == ML_LINE_BLANK)
	{
		gutter = "     ";
	}

	else if (!first)
	{
		gutter = ml_choose (last, "   \xE2\x94\x94 ", "   \xE2\x94\x82 ");
	}

	else if (kind == ML_LINE_MATH)
	{
		gutter = math_gutters [domain];
	}

	else if (kind == ML_LINE_INCLUDE)
	{
		gutter = "inc> ";
	}

	else
	{
		gutter = "\?\?\?> ";
	}

	return ml_slice_cstr (gutter);
}

static MlResult append_cstr (MlStr *out, const char *text)
{
	return ml_str_append (out, ml_slice_cstr (text));
}

/*
 * Switches to the style of a class of the theme, or to the plain way when the
 * editor draws without a theme.
 */
static MlResult append_class (
	MlStr          *out,
	const MlEditor *ed,
	MlClass        c,
	const char     *plain
)
{
	MlResult r = ML_OK;

	if (ed->highlight)
	{
		r = ml_style_sgr (out, ed->theme.styles [c], ed->colors);
	}

	else
	{
		r = append_cstr (out, plain);
	}

	return r;
}

/* base with what over adds: its colours when it has them, its attributes. */
static MlStyle merge_style (MlStyle base, MlStyle over)
{
	MlStyle s = base;

	s.fg        = ml_choose (over.fg.set, over.fg, s.fg);
	s.bg        = ml_choose (over.bg.set, over.bg, s.bg);
	s.bold      = s.bold || over.bold;
	s.dim       = s.dim || over.dim;
	s.underline = s.underline || over.underline;
	s.reverse   = s.reverse || over.reverse;
	return s;
}

/* What a math line is drawn from by append_themed. */
typedef struct Themed
{
	MlSlice  text;
	uint64_t snapshot;  /* the environment the line saw */
	uint64_t err_start; /* the byte range of an error, err_len 0: none */
	uint64_t err_len;
	uint64_t a;         /* the brackets to emphasise (past the text: none) */
	uint64_t b;
	int32_t  matched;   /* a and b pair up */
} Themed;

/*
 * A math line drawn with the theme: every character in the style of its class
 * (see highlight.h) and the brackets emphasised, from column start_col on,
 * avail columns wide; a wide character is not cut in half.
 */
static MlTextWindow append_themed (
	MlStr          *out,
	const MlEditor *ed,
	Themed         t,
	uint64_t       start_col,
	uint64_t       avail
)
{
	MlTextWindow w =
	{
		.result    = ML_OK,
		.first_col = start_col
	};

	MlRunVec runs;
	MlStyle  cur = { .bold = 0 };
	uint64_t pos = 0;
	uint64_t col = 0;

	MlRunVec_init (&runs);
	w.result = ml_highlight (
		&runs,
		t.text,
		&ed->results.env,
		t.snapshot,
		t.err_start,
		t.err_len
	);

	while (pos < t.text.len && w.result == ML_OK && col < start_col + avail)
	{
		uint64_t next  = ml_utf8_next (t.text, pos);
		MlSlice  cp    = ml_slice_sub (t.text, pos, next - pos);
		uint64_t width = ml_utf8_width (cp);
		int32_t  shown = col >= start_col && col + width <= start_col + avail;
		MlStyle  want  = ed->theme.styles [ml_runs_class_at (&runs, pos)];

		if (pos == t.a || pos == t.b)
		{
			want = merge_style (
				want,
				ed->theme.styles [
					ml_choose (t.matched, ML_CLASS_MATCH, ML_CLASS_UNMATCHED)
				]
			);
		}

		if (shown && !ml_style_equal (cur, want))
		{
			if (!ml_style_is_plain (cur))
			{
				w.result = append_cstr (out, STYLE_RESET);
			}

			if (w.result == ML_OK)
			{
				w.result = ml_style_sgr (out, want, ed->colors);
			}

			cur = want;
		}

		if (shown && w.result == ML_OK)
		{
			w.result = ml_str_append (out, cp);
		}

		col += width;
		pos  = next;
	}

	if (w.result == ML_OK && !ml_style_is_plain (cur))
	{
		w.result = append_cstr (out, STYLE_RESET);
	}

	MlRunVec_free (&runs);
	return w;
}

/* Digits needed for the line numbers of total lines (at least two). */
static uint64_t number_width (uint64_t total)
{
	uint64_t digits = 2;

	for (uint64_t limit = 100; total >= limit && limit != 0; limit *= 10)
	{
		digits++;
	}

	return digits;
}

typedef struct Head
{
	MlResult result;
	uint64_t used; /* columns taken by the line number and the gutter */
} Head;

/*
 * The start of a row, the bar on the left: the line number and the gutter
 * are one piece, dim, reversed on the Normal state cursor line. Notes dim
 * their text too.
 */
static Head append_head (
	MlStr          *out,
	const MlEditor *ed,
	uint64_t       number,
	uint64_t       num_width,
	MlSlice        gutter,
	int32_t        highlight,
	int32_t        note,
	uint32_t       cols
)
{
	Head head =
	{
		.result = ML_OK,
		.used   = 0
	};

	MlStr num;
	MlStr shown;

	ml_str_init (&num);
	ml_str_init (&shown);
	head.result = ml_str_appendf (
		&num,
		"%*u ",
		(int32_t)num_width,
		(uint32_t)number
	);

	if (head.result == ML_OK)
	{
		head.result = ml_text_window (
			&shown,
			ml_str_view (num),
			0,
			cols
		).result;
	}

	head.used = ml_utf8_width (ml_str_view (shown));

	if (head.result == ML_OK)
	{
		head.result = ml_choose (
			highlight,
			append_cstr (out, STYLE_REVERSE),
			append_class (out, ed, ML_CLASS_GUTTER, STYLE_DIM)
		);
	}

	if (head.result == ML_OK)
	{
		head.result = ml_str_append (out, ml_str_view (shown));
	}

	if (head.result == ML_OK)
	{
		head.result = ml_text_window (out, gutter, 0, cols - head.used).result;
		head.used  += ml_utf8_width (gutter);
		head.used = ml_choose (head.used > cols, cols, head.used);
	}

	if (head.result == ML_OK && (highlight || !note))
	{
		head.result = append_cstr (out, STYLE_RESET);
	}

	if (head.result == ML_OK && highlight && note)
	{
		head.result = append_cstr (out, STYLE_DIM);
	}

	ml_str_free (&shown);
	ml_str_free (&num);
	return head;
}

/* Columns that the result needs at least to be worth drawing: "= 1". */
#define MIN_RESULT_WIDTH 3

/*
 * The result after the text of a line, in the avail columns that are left:
 * two spaces, then "= value" dim or "! message" in red.
 */
static MlResult append_result (
	MlStr          *out,
	const MlEditor *ed,
	MlResultView   result,
	uint64_t       avail
)
{
	MlResult r = ML_OK;
	MlStr    shown;

	ml_str_init (&shown);

	if (
		   result.kind != ML_RESULT_NONE
		&& avail >= 2 + MIN_RESULT_WIDTH
	)
	{
		r = append_cstr (
			&shown,
			ml_choose (
				result.kind == ML_RESULT_ERROR,
				"! ",
				"= "
			)
		);

		if (r == ML_OK)
		{
			r = ml_str_append (&shown, result.text);
		}

		if (r == ML_OK)
		{
			r = append_cstr (out, "  ");
		}

		if (r == ML_OK)
		{
			r = append_class (
				out,
				ed,
				ml_choose (
					result.kind == ML_RESULT_ERROR,
					ML_CLASS_RESULT_ERROR,
					ML_CLASS_RESULT
				),
				ml_choose (
					result.kind == ML_RESULT_ERROR,
					STYLE_RED,
					STYLE_DIM
				)
			);
		}

		if (r == ML_OK)
		{
			r = ml_text_window (
				out,
				ml_str_view (shown),
				0,
				avail - 2
			).result;
		}

		if (r == ML_OK)
		{
			r = append_cstr (out, STYLE_RESET);
		}
	}

	ml_str_free (&shown);
	return r;
}

/*
 * The text of a note in the room columns: the Markdown marks are taken away
 * and bold, italic, underline, strike and code are drawn as such. The text
 * is clipped by its width without the marks.
 */
static MlResult append_note_text (MlStr *out, MlSlice text, uint64_t room)
{
	MlMarkup m;
	MlStr    shown;
	MlResult r;

	ml_markup_init (&m);
	ml_str_init (&shown);
	r = ml_markup_parse (&m, text);

	if (r == ML_OK)
	{
		r = ml_text_window (&shown, ml_str_view (m.plain), 0, room).result;
	}

	if (r == ML_OK)
	{
		r = append_cstr (out, STYLE_RESET);
	}

	if (r == ML_OK)
	{
		r = ml_markup_append (out, &m, 0, shown.len);
	}

	ml_str_free (&shown);
	ml_markup_free (&m);
	return r;
}

/* A note of three or more '-', '*' or '_' only is a horizontal rule. */
static int32_t is_rule (MlSlice text)
{
	uint64_t first = 0;
	uint64_t end   = text.len;
	uint8_t  mark;
	int32_t  rule;

	while (first < end && text.data [first] == ' ')
	{
		first++;
	}

	while (end > first && text.data [end - 1] == ' ')
	{
		end--;
	}

	mark = ml_choose (end > first, text.data [first], 0);
	rule = (
		   end - first >= 3
		&& (mark == '-' || mark == '*' || mark == '_')
	);

	for (uint64_t i = first; i < end && rule; i++)
	{
		rule = text.data [i] == mark;
	}

	return rule;
}

/*
 * One row of the line area: number and gutter, then the text clipped, then
 * the result if there is room. A folded block is this one row, with the
 * number of the lines hidden behind it.
 */
static MlResult append_line_row (
	MlStr          *out,
	const MlEditor *ed,
	MlLineView     line,
	MlResultView result,
	uint64_t     number,
	uint64_t     num_width,
	int32_t      highlight,
	uint32_t     cols,
	MlSlice      gutter,
	uint64_t     hidden
)
{
	MlResult r    = ML_OK;
	int32_t  note = line.kind == ML_LINE_NOTE;
	int32_t  bare = line.kind == ML_LINE_BLANK && !highlight;
	Head     head = append_head (
		out,
		ed,
		number,
		num_width,
		ml_choose (bare, ml_slice ("", 0), gutter),
		highlight,
		0,
		cols
	);

	r = head.result;

	if (r == ML_OK && !bare)
	{
		uint64_t room = cols - head.used;
		uint64_t mark = ml_choose (hidden > 0 && room >= 2, 2, 0);
		uint64_t used;

		/* the chevron of a folded block */
		if (mark > 0)
		{
			r = append_cstr (out, "\xE2\x96\xB8 ");
		}

		room -= mark;
		used  = ml_choose (
			ml_utf8_width (line.text) < room,
			ml_utf8_width (line.text),
			room
		);

		if (r == ML_OK && note && hidden == 0 && is_rule (line.text))
		{
			r = append_cstr (out, STYLE_DIM);

			for (uint64_t i = 0; i < room && r == ML_OK; i++)
			{
				r = append_cstr (out, "\xE2\x94\x80");
			}

			used = room;
		}

		else if (r == ML_OK && note)
		{
			r = append_note_text (out, line.text, room);
		}

		else if (r == ML_OK && ed->highlight && line.kind == ML_LINE_MATH)
		{
			Themed t =
			{
				.text      = line.text,
				.snapshot  = result.snapshot,
				.err_start = result.err_start,
				.err_len   = ml_choose (
					result.kind == ML_RESULT_ERROR,
					result.err_len,
					0
				),
				.a         = line.text.len,
				.b         = line.text.len,
				.matched   = 0
			};

			r = append_themed (out, ed, t, 0, room).result;
		}

		else if (r == ML_OK)
		{
			r = ml_text_window (out, line.text, 0, room).result;
		}

		if (r == ML_OK && hidden > 0 && room > used + 4)
		{
			MlStr more;

			ml_str_init (&more);
			r = ml_str_appendf (
				&more,
				"  +%u line%s",
				(uint32_t)hidden,
				ml_choose (hidden == 1, "", "s")
			);

			if (r == ML_OK)
			{
				r = append_cstr (out, STYLE_RESET STYLE_DIM);
			}

			if (r == ML_OK)
			{
				r = ml_text_window (
					out,
					ml_str_view (more),
					0,
					room - used
				).result;
			}

			if (r == ML_OK)
			{
				r = append_cstr (out, STYLE_RESET);
			}

			used = room;
			ml_str_free (&more);
		}

		/* a matrix is drawn under the line instead */
		else if (
			   r == ML_OK
			&& line.kind == ML_LINE_MATH
			&& result.block.len == 0
		)
		{
			r = append_result (out, ed, result, room - used);
		}
	}

	if (r == ML_OK && (note || bare))
	{
		r = append_cstr (out, STYLE_RESET);
	}

	return r;
}

/*
 * Draws the whole row reversed, up to cols columns: the style is switched on
 * again after each reset inside the row and the rest is filled with spaces.
 */
static MlResult reverse_row (MlStr *row, uint32_t cols)
{
	MlResult r    = ML_OK;
	MlStr    out;
	MlSlice  in   = ml_str_view (*row);
	uint64_t pos  = 0;
	uint64_t used = 0;

	ml_str_init (&out);
	r = append_cstr (&out, STYLE_REVERSE);

	while (pos < in.len && r == ML_OK)
	{
		uint64_t next = pos + 1;

		if (in.data [pos] == 0x1b)
		{
			while (
				   next < in.len
				&& !(
					   next > pos + 1
					&& in.data [next] >= '@'
					&& in.data [next] <= '~'
				)
			)
			{
				next++;
			}

			next = ml_choose (next < in.len, next + 1, in.len);
			r    = ml_str_append (&out, ml_slice_sub (in, pos, next - pos));

			if (r == ML_OK && ml_slice_equal (
				ml_slice_sub (in, pos, next - pos),
				ml_slice_cstr (STYLE_RESET)
			))
			{
				r = append_cstr (&out, STYLE_REVERSE);
			}
		}

		else
		{
			next  = ml_utf8_next (in, pos);
			used += ml_utf8_width (ml_slice_sub (in, pos, next - pos));
			r     = ml_str_append (&out, ml_slice_sub (in, pos, next - pos));
		}

		pos = next;
	}

	for (uint64_t i = used; i < cols && r == ML_OK; i++)
	{
		r = append_cstr (&out, " ");
	}

	if (r == ML_OK)
	{
		r = append_cstr (&out, STYLE_RESET);
	}

	if (r == ML_OK)
	{
		ml_str_free (row);
		*row = out;
	}

	else
	{
		ml_str_free (&out);
	}

	return r;
}

/* The byte of text that starts at display column col. */
static uint64_t byte_at_column (MlSlice text, uint64_t col)
{
	uint64_t pos   = 0;
	uint64_t width = 0;

	while (pos < text.len && width < col)
	{
		uint64_t next = ml_utf8_next (text, pos);

		width += ml_utf8_width (ml_slice_sub (text, pos, next - pos));
		pos    = next;
	}

	return pos;
}

/*
 * The note being typed, as it will look: the text stays as typed, marks
 * included, the marks are dim and the text between them bold, italic and so
 * on right away.
 */
static MlTextWindow append_live_note (
	MlStr    *out,
	MlSlice  text,
	uint64_t start_col,
	uint64_t avail
)
{
	MlMarkup     m;
	MlStr        shown;
	MlTextWindow w;

	ml_markup_init (&m);
	ml_str_init (&shown);
	w = ml_text_window (&shown, text, start_col, avail);

	if (w.result == ML_OK)
	{
		w.result = ml_markup_parse_live (&m, text);
	}

	if (w.result == ML_OK)
	{
		w.result = ml_markup_append (
			out,
			&m,
			byte_at_column (text, w.first_col),
			shown.len
		);
	}

	ml_str_free (&shown);
	ml_markup_free (&m);
	return w;
}

typedef struct EditRow
{
	MlResult result;
	uint32_t cursor_col; /* 1 based */
	uint64_t head_used;  /* columns taken by the line number and the gutter */
	uint64_t first_col;  /* display column of the first text column shown */
} EditRow;

/*
 * The text with the bytes lo..hi (the selection) reversed, from column
 * start_col on, avail columns wide; a wide character is not cut in half.
 */
static MlTextWindow append_selected (
	MlStr    *out,
	MlSlice  text,
	uint64_t start_col,
	uint64_t avail,
	uint64_t lo,
	uint64_t hi
)
{
	MlTextWindow w =
	{
		.result    = ML_OK,
		.first_col = start_col
	};

	uint64_t pos     = 0;
	uint64_t col     = 0;
	int32_t  reverse = 0;

	while (pos < text.len && w.result == ML_OK && col < start_col + avail)
	{
		uint64_t next  = ml_utf8_next (text, pos);
		MlSlice  cp    = ml_slice_sub (text, pos, next - pos);
		uint64_t width = ml_utf8_width (cp);
		int32_t  shown = col >= start_col && col + width <= start_col + avail;
		int32_t  want  = pos >= lo && pos < hi;

		if (shown && want != reverse)
		{
			w.result = append_cstr (
				out,
				ml_choose (want, STYLE_REVERSE, STYLE_RESET)
			);
			reverse  = want;
		}

		if (shown && w.result == ML_OK)
		{
			w.result = ml_str_append (out, cp);
		}

		col += width;
		pos  = next;
	}

	if (w.result == ML_OK && reverse)
	{
		w.result = append_cstr (out, STYLE_RESET);
	}

	return w;
}

/*
 * The part of a line the selection covers: bytes lo..hi, and whether it
 * goes on past the end of the line.
 */
static int32_t selection_in_line (
	const MlEditor *ed,
	uint64_t       idx,
	uint64_t       len,
	uint64_t       *lo,
	uint64_t       *hi,
	int32_t        *eol
)
{
	MlTextRange s = ml_editor_selection (ed);
	int32_t     in = s.valid && idx >= s.first_line && idx <= s.last_line;

	*lo  = ml_choose (in && idx == s.first_line, s.first_byte, 0);
	*hi  = ml_choose (in && idx == s.last_line, s.last_byte, len);
	*eol = in && idx < s.last_line;

	return in;
}

/* A line of the session with a selection in it: the raw text, reversed. */
static MlResult append_selected_line (
	MlStr          *out,
	const MlEditor *ed,
	MlSlice        text,
	uint64_t       idx,
	uint64_t       num_width,
	uint32_t       cols,
	MlSlice        gutter
)
{
	uint64_t lo;
	uint64_t hi;
	int32_t  eol;
	Head     head = append_head (
		out,
		ed,
		idx + 1,
		num_width,
		gutter,
		0,
		0,
		cols
	);
	uint64_t avail = ml_choose (cols > head.used, cols - head.used, 0);
	MlTextWindow w =
	{
		.result    = head.result,
		.first_col = 0
	};

	selection_in_line (ed, idx, text.len, &lo, &hi, &eol);

	if (w.result == ML_OK)
	{
		w = append_selected (out, text, 0, avail, lo, hi);
	}

	/* the line break is shown as one more reversed column */
	if (
		   w.result == ML_OK
		&& eol
		&& ml_utf8_width (text) < avail
	)
	{
		w.result = append_cstr (out, STYLE_REVERSE " " STYLE_RESET);
	}

	return w.result;
}

/*
 * The text with the bytes a and b drawn in a style (the brackets that pair
 * up, or a lone one), from column start_col on, avail columns wide.
 */
static MlTextWindow append_marked (
	MlStr       *out,
	MlSlice     text,
	uint64_t    start_col,
	uint64_t    avail,
	uint64_t    a,
	uint64_t    b,
	const char *style
)
{
	MlTextWindow w =
	{
		.result    = ML_OK,
		.first_col = start_col
	};

	uint64_t pos = 0;
	uint64_t col = 0;

	while (pos < text.len && w.result == ML_OK && col < start_col + avail)
	{
		uint64_t next  = ml_utf8_next (text, pos);
		MlSlice  cp    = ml_slice_sub (text, pos, next - pos);
		uint64_t width = ml_utf8_width (cp);
		int32_t  shown = col >= start_col && col + width <= start_col + avail;
		int32_t  mark  = shown && (pos == a || pos == b);

		if (mark)
		{
			w.result = append_cstr (out, style);
		}

		if (shown && w.result == ML_OK)
		{
			w.result = ml_str_append (out, cp);
		}

		if (mark && w.result == ML_OK)
		{
			w.result = append_cstr (out, STYLE_RESET);
		}

		col += width;
		pos  = next;
	}

	return w;
}

/* The row of the line being typed. */
static EditRow append_edit_row (
	MlStr          *out,
	const MlEditor *ed,
	uint64_t       number,
	uint64_t       num_width,
	uint32_t       cols,
	MlSlice        gutter
)
{
	EditRow row =
	{
		.result     = ML_OK,
		.cursor_col = 1,
		.head_used  = 0,
		.first_col  = 0
	};

	Head    head = append_head (
		out,
		ed,
		number,
		num_width,
		gutter,
		0,
		0,
		cols
	);
	uint64_t avail = ml_choose (
		cols > head.used + 1,
		cols - head.used - 1,
		1
	);
	MlTextWindow w =
	{
		.result    = ML_OK,
		.first_col = 0
	};

	uint64_t lo;
	uint64_t hi;
	int32_t  eol;
	int32_t  selected = selection_in_line (
		ed,
		ed->edit_index,
		ed->edit.text.len,
		&lo,
		&hi,
		&eol
	);
	MlBracketMatch bracket = ml_bracket_match (
		ml_str_view (ed->edit.text),
		ed->edit.cursor
	);

	row.result = head.result;

	if (row.result == ML_OK && selected)
	{
		w = append_selected (
			out,
			ml_str_view (ed->edit.text),
			ml_lineedit_start_col (&ed->edit, avail),
			avail,
			lo,
			hi
		);
		row.result = w.result;

		/* the line break is shown as one more reversed column */
		if (
			   row.result == ML_OK
			&& eol
			&& ml_utf8_width (ml_str_view (ed->edit.text)) < w.first_col + avail
		)
		{
			row.result = append_cstr (out, STYLE_REVERSE " " STYLE_RESET);
		}
	}

	else if (
		   row.result == ML_OK
		&& ed->highlight
		&& ed->state == ML_STATE_MATH
	)
	{
		uint64_t end = ed->edit.text.len;
		Themed   t   =
		{
			.text      = ml_str_view (ed->edit.text),
			.snapshot  = ml_choose (
				ed->edit_index < ed->results.lines.len,
				ed->results.lines.data [ed->edit_index].snapshot,
				ml_env_snapshot (&ed->results.env)
			),
			.err_start = 0,
			.err_len   = 0,
			.a         = ml_choose (bracket.found, bracket.at, end),
			.b         = ml_choose (
				bracket.matched,
				bracket.partner,
				ml_choose (bracket.found, bracket.at, end)
			),
			.matched   = bracket.matched
		};

		w = append_themed (
			out,
			ed,
			t,
			ml_lineedit_start_col (&ed->edit, avail),
			avail
		);
		row.result = w.result;
	}

	else if (
		   row.result == ML_OK
		&& ed->state == ML_STATE_MATH
		&& bracket.found
	)
	{
		w = append_marked (
			out,
			ml_str_view (ed->edit.text),
			ml_lineedit_start_col (&ed->edit, avail),
			avail,
			bracket.at,
			ml_choose (bracket.matched, bracket.partner, bracket.at),
			ml_choose (bracket.matched, STYLE_MATCH, STYLE_UNMATCHED)
		);
		row.result = w.result;
	}

	else if (row.result == ML_OK && ed->state == ML_STATE_NOTE)
	{
		w = append_live_note (
			out,
			ml_str_view (ed->edit.text),
			ml_lineedit_start_col (&ed->edit, avail),
			avail
		);
		row.result = w.result;
	}

	else if (row.result == ML_OK)
	{
		w = ml_text_window (
			out,
			ml_str_view (ed->edit.text),
			ml_lineedit_start_col (&ed->edit, avail),
			avail
		);
		row.result = w.result;
	}

	/* the preview of the result, dim, when the end of the text is visible */
	if (
		   row.result == ML_OK
		&& ed->state == ML_STATE_MATH
		&& ed->edit_domain == ML_DOMAIN_ALGEBRA
	)
	{
		MlStr    value;
		MlGhost  g;
		uint64_t used = ml_utf8_width (ml_str_view (ed->edit.text));

		ml_str_init (&value);
		g = ml_ghost (
			&value,
			&ed->results,
			ed->edit_index,
			ml_str_view (ed->edit.text)
		);
		row.result = g.result;

		if (row.result == ML_OK && g.shown && used <= w.first_col + avail)
		{
			MlResultView view =
			{
				.kind = ML_RESULT_VALUE,
				.text = ml_str_view (value)
			};

			row.result = append_result (
				out,
				ed,
				view,
				cols - head.used - (used - w.first_col)
			);
		}

		ml_str_free (&value);
	}

	row.head_used = head.used;
	row.first_col = w.first_col;

	if (row.result == ML_OK)
	{
		uint64_t cursor_col = ml_lineedit_cursor_col (&ed->edit);

		row.cursor_col = (uint32_t)(
			head.used + 1
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

	MlCompletion comp = ml_editor_completion (ed);

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

	/* the rest of the suggested word, dim, after the typed text */
	if (row.result == ML_OK && comp.ghost < ML_MAX_CANDIDATES)
	{
		uint64_t shown = ml_lineedit_cursor_col (&ed->edit) - w.first_col;

		row.result = append_cstr (out, STYLE_DIM);

		if (row.result == ML_OK)
		{
			row.result = ml_text_window (
				out,
				comp.ghost_text,
				0,
				ml_choose (avail > shown, avail - shown, 0)
			).result;
		}

		if (row.result == ML_OK)
		{
			row.result = append_cstr (out, STYLE_RESET);
		}
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

/* The row above the command line that lists the candidates. */
static MlResult append_suggestions (
	MlStr              *out,
	const MlCompletion *comp,
	uint32_t           cols
)
{
	MlResult r    = append_cstr (out, STYLE_DIM);
	uint64_t used = 0;

	for (uint64_t i = 0; i < comp->count && r == ML_OK && used < cols; i++)
	{
		MlStr item;

		ml_str_init (&item);
		r = append_cstr (&item, "  ");

		if (r == ML_OK)
		{
			r = ml_str_append (&item, comp->items [i]);
		}

		if (r == ML_OK && i == comp->ghost)
		{
			r = append_cstr (out, STYLE_REVERSE);
		}

		if (r == ML_OK)
		{
			r = ml_text_window (out, ml_str_view (item), 0, cols - used).result;
		}

		if (r == ML_OK && i == comp->ghost)
		{
			r = append_cstr (out, STYLE_RESET);

			if (r == ML_OK)
			{
				r = append_cstr (out, STYLE_DIM);
			}
		}

		used += ml_utf8_width (ml_str_view (item));
		ml_str_free (&item);
	}

	if (r == ML_OK)
	{
		r = append_cstr (out, STYLE_RESET);
	}

	return r;
}

/* A row of the typeset result of a matrix, under the text of its line. */
static MlResult append_block_row (
	MlStr      *out,
	MlSlice    text,
	uint64_t   indent,
	uint32_t   cols,
	const char *style
)
{
	uint64_t pad = ml_choose (indent + 2 < cols, indent + 2, cols);
	MlResult r   = append_spaces (out, pad);

	if (r == ML_OK)
	{
		r = append_cstr (out, style);
	}

	if (r == ML_OK)
	{
		r = ml_text_window (out, text, 0, cols - pad).result;
	}

	if (r == ML_OK)
	{
		r = append_cstr (out, STYLE_RESET);
	}

	return r;
}

/* The number of rows of the typeset block: its newlines plus one. */
static uint64_t block_rows (MlSlice block)
{
	uint64_t rows = ml_choose (block.len > 0, 1, 0);

	for (uint64_t i = 0; i < block.len; i++)
	{
		rows += ml_choose (block.data [i] == '\n', 1, 0);
	}

	return rows;
}

/* Row n (from 0) of a block. */
static MlSlice block_row (MlSlice block, uint64_t n)
{
	uint64_t start = 0;
	uint64_t end   = 0;
	uint64_t row   = 0;

	while (end < block.len && row <= n)
	{
		start = end;

		while (end < block.len && block.data [end] != '\n')
		{
			end++;
		}

		row++;
		end = ml_choose (end < block.len && row <= n, end + 1, end);
	}

	return ml_choose (
		row > n,
		ml_slice_sub (block, start, end - start),
		ml_slice ("", 0)
	);
}

/* The selected lines. */
static uint64_t selection_first (const MlEditor *ed)
{
	return ml_choose (
		ed->anchor < ed->session.cursor,
		ed->anchor,
		ed->session.cursor
	);
}

static uint64_t selection_last (const MlEditor *ed)
{
	return ml_choose (
		ed->anchor > ed->session.cursor,
		ed->anchor,
		ed->session.cursor
	);
}

/* The steps under a line that has them open. */
static MlSlice steps_of (const MlEditor *ed, uint64_t src)
{
	return ml_choose (
		ed->steps_open && ed->steps_line == src,
		ml_str_view (ed->steps),
		ml_slice ("", 0)
	);
}

/*
 * What the layout needs to know of each line it draws, the lines of the
 * session and the one being typed (a new line is not in the session yet).
 */
int32_t ml_screen_hint_active (const MlEditor *ed)
{
	return (
		   ed->session.lines.len == 0
		&& ed->edit.text.len == 0
		&& (ed->state == ML_STATE_NOTE || ed->state == ML_STATE_MATH)
		&& !ed->selecting
	);
}

/*
 * A hint in the middle of a row of cols columns as ghost text, the parts
 * between braces in the colour of the hint-key class. Nothing is written
 * when it does not fit.
 */
static MlResult append_hint (
	MlStr          *out,
	const MlEditor *ed,
	MlSlice        hint,
	uint32_t       cols
)
{
	MlResult r     = ML_OK;
	uint64_t width = ml_hint_width (hint);

	if (width + 2 <= cols)
	{
		r = append_spaces (out, (cols - width) / 2);

		if (r == ML_OK)
		{
			r = append_class (out, ed, ML_CLASS_GHOST, STYLE_DIM);
		}

		for (uint64_t i = 0; i < hint.len && r == ML_OK; i++)
		{
			char c = hint.data [i];

			if (c == '{' || c == '}')
			{
				r = append_cstr (out, STYLE_RESET);

				if (r == ML_OK)
				{
					r = ml_choose (
						c == '{',
						append_class (out, ed, ML_CLASS_HINT_KEY, "\x1b[1m"),
						append_class (out, ed, ML_CLASS_GHOST, STYLE_DIM)
					);
				}
			}

			else
			{
				r = ml_str_append (out, ml_slice_sub (hint, i, 1));
			}
		}

		if (r == ML_OK)
		{
			r = append_cstr (out, STYLE_RESET);
		}
	}

	return r;
}

/* Puts the hint in the middle row of the text area. */
static MlResult layout_hint (
	MlLayout       *layout,
	const MlScreen *s,
	const MlEditor *ed
)
{
	MlResult r   = ML_OK;
	uint64_t mid = (s->rows - 1) / 2;
	MlStr    row;

	ml_str_init (&row);
	r = append_hint (&row, ed, ml_hint_at (s->hint_tick), s->cols);

	if (r == ML_OK)
	{
		ml_str_free (&layout->rows.data [mid]);
		layout->rows.data [mid] = row;
	}

	else
	{
		ml_str_free (&row);
	}

	return r;
}

typedef struct VLine
{
	MlLineKind kind;
	MlDomain   domain;
	uint64_t   src;     /* index in the session, valid unless fresh */
	int32_t    fresh;   /* the new line being typed */
	int32_t    typing;  /* the line (fresh or not) being typed */
	uint64_t   first;   /* the block of the line */
	uint64_t   last;
	int32_t    folded;  /* the block is one row */
} VLine;

ML_VEC_DEFINE (VLineVec, VLine)

/* What a row past the last line is drawn from. */
static const VLine no_line =
{
	.kind   = ML_LINE_NONE,
	.domain = ML_DOMAIN_ALGEBRA,
	.src    = 0,
	.fresh  = 0,
	.typing = 0,
	.first  = 0,
	.last   = 0,
	.folded = 0
};

/* The lines to draw, with their blocks and folds. */
static MlResult vlines_build (
	VLineVec       *vl,
	const MlEditor *ed,
	int32_t        editing,
	int32_t        editing_new,
	uint64_t       total
)
{
	MlResult r = VLineVec_reserve (vl, total);

	for (uint64_t v = 0; v < total && r == ML_OK; v++)
	{
		int32_t    fresh = editing_new && v == ed->edit_index;
		uint64_t   src   = ml_choose (
			editing_new && v > ed->edit_index,
			v - 1,
			v
		);
		MlLineView lv    = ml_session_get (&ed->session, src);
		VLine      l     =
		{
			.kind   = ml_choose (
				fresh,
				ml_choose (
					ed->state == ML_STATE_NOTE,
					ML_LINE_NOTE,
					ml_choose (
						ed->edit_kind == ML_LINE_BLANK
						|| ed->edit.text.len == 0,
						ML_LINE_BLANK,
						ed->edit_kind
					)
				),
				lv.kind
			),
			.domain = ml_choose (fresh, ed->edit_domain, lv.domain),
			.src    = src,
			.fresh  = fresh,
			.typing = editing && v == ed->edit_index,
			.first  = v,
			.last   = v,
			.folded = ml_choose (fresh, 0, lv.folded)
		};

		r = VLineVec_push (vl, l);
	}

	/* the block of each line, front to back, then back to front */
	for (uint64_t v = 1; v < vl->len; v++)
	{
		if (
			ml_block_continues (
				vl->data [v - 1].kind,
				vl->data [v - 1].domain,
				vl->data [v].kind,
				vl->data [v].domain
			)
		)
		{
			vl->data [v].first = vl->data [v - 1].first;
		}
	}

	for (uint64_t v = vl->len; v > 1; v--)
	{
		if (vl->data [v - 1].first == vl->data [v - 2].first)
		{
			vl->data [v - 2].last = vl->data [v - 1].last;
		}
	}

	for (uint64_t v = 0; v < vl->len; v++)
	{
		VLine f = vl->data [vl->data [v].first];

		/* the last line of a block is known from the first one */
		vl->data [v].last = vl->data [vl->data [v].first].last;

		vl->data [v].folded = f.folded && f.last > f.first;
	}

	for (uint64_t v = 0; v < vl->len; v++)
	{
		if (vl->data [v].typing)
		{
			uint64_t first = vl->data [v].first;

			/* a block with the line being typed in it is open */
			for (
				uint64_t k = first;
				k < vl->len && vl->data [k].first == first;
				k++
			)
			{
				vl->data [k].folded = 0;
			}
		}
	}

	return r;
}

/* The rows a line takes: 0 when hidden in a folded block. */
static uint64_t line_height (
	const MlEditor *ed,
	const VLineVec *vl,
	uint64_t       v
)
{
	VLine    l      = vl->data [v];
	int32_t  hidden = l.folded && v != l.first;
	uint64_t extra  = 0;

	if (!hidden && !l.folded && !l.typing)
	{
		extra = (
			  block_rows (ml_results_get (&ed->results, l.src).block)
			+ block_rows (steps_of (ed, l.src))
		);
	}

	return ml_choose (hidden, 0, 1 + extra);
}

/* The rows of lines from to to (not including to). */
static uint64_t rows_between (
	const MlEditor *ed,
	const VLineVec *vl,
	uint64_t       from,
	uint64_t       to
)
{
	uint64_t rows = 0;

	for (uint64_t i = from; i < to; i++)
	{
		rows += line_height (ed, vl, i);
	}

	return rows;
}

/*
 * Scrolls so that the focused line (an index into total lines) is inside the
 * area of rows, and no row of the area is empty while lines above could fill
 * it.
 */
static uint64_t scroll_top (
	const MlEditor *ed,
	const VLineVec *vl,
	uint64_t       top,
	uint64_t       focus,
	uint64_t       area
)
{
	uint64_t t = ml_choose (focus < top, focus, top);

	while (
		   t < focus
		&& rows_between (ed, vl, t, focus + 1) > area
	)
	{
		t++;
	}

	while (
		   t > 0
		&& rows_between (ed, vl, t - 1, vl->len) <= area
	)
	{
		t--;
	}

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
		   ed->state == ML_STATE_MATH
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

	uint64_t     num_width = number_width (total);
	MlCompletion comp      = ml_editor_completion (ed);
	VLineVec     vl;
	uint64_t     line = 0;
	uint64_t     sub  = 0; /* the row of the line being drawn, 0 for its own */

	VLineVec_init (&vl);
	MlRowVec_init (&layout.rows);
	MlRowInfoVec_init (&layout.info);
	layout.result = vlines_build (&vl, ed, editing, editing_new, total);
	s->top        = scroll_top (ed, &vl, s->top, focus, area);
	line          = s->top;

	for (uint64_t r = 0; r < s->rows && layout.result == ML_OK; r++)
	{
		MlStr row;
		VLine l = ml_choose (line < total, vl.data [line], no_line);
		MlRowInfo ri =
		{
			.kind      = 0,
			.src       = 0,
			.text_col  = 0,
			.first_col = 0
		};

		uint64_t gutter_width = ml_utf8_width (
			gutter_for (l.kind, l.domain, line == l.first, line == l.last)
		);

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

		else if (r + 2 == s->rows && comp.count >= 2)
		{
			layout.result = append_suggestions (&row, &comp, s->cols);
		}

		else if (line < total && l.typing)
		{
			EditRow e = append_edit_row (
				&row,
				ed,
				line + 1,
				num_width,
				s->cols,
				gutter_for (
					l.kind,
					l.domain,
					line == l.first,
					line == l.last
				)
			);

			layout.result     = e.result;
			layout.cursor.row = (uint32_t)(r + 1);
			layout.cursor.col = e.cursor_col;
			ri.kind           = 1;
			ri.src            = ed->edit_index;
			ri.text_col       = e.head_used;
			ri.first_col      = e.first_col;
		}

		else if (
			   line < total
			&& sub == 0
			&& !l.fresh
			&& !l.folded
			&& ml_editor_selection (ed).valid
			&& line >= ml_editor_selection (ed).first_line
			&& line <= ml_editor_selection (ed).last_line
		)
		{
			layout.result = append_selected_line (
				&row,
				ed,
				ml_session_get (&ed->session, l.src).text,
				l.src,
				num_width,
				s->cols,
				gutter_for (
					l.kind,
					l.domain,
					line == l.first,
					line == l.last
				)
			);
			ri.kind     = 1;
			ri.src      = l.src;
			ri.text_col = num_width + 1 + gutter_width;
		}

		else if (line < total && sub > 0)
		{
			MlSlice  result = ml_results_get (&ed->results, l.src).block;
			uint64_t shown  = block_rows (result);

			layout.result = ml_choose (
				sub - 1 < shown,
				append_block_row (
					&row,
					block_row (result, sub - 1),
					num_width + 1 + ML_GUTTER_WIDTH,
					s->cols,
					STYLE_DIM
				),
				append_block_row (
					&row,
					block_row (steps_of (ed, l.src), sub - 1 - shown),
					num_width + 1 + ML_GUTTER_WIDTH,
					s->cols,
					""
				)
			);
		}

		else if (line < total)
		{
			uint64_t src    = l.src;
			int32_t  inside = (
				   src >= ed->session.cursor
				&& ed->session.cursor + 0 >= src
			);
			int32_t  here   = (
				   ed->session.cursor >= vl.data [l.first].src
				&& ed->session.cursor <= vl.data [l.last].src
			);
			int32_t  highlight;

			(void)inside;
			highlight = (
				   !editing
				&& ed->state != ML_STATE_SELECT
				&& ml_choose (l.folded, here, src == ed->session.cursor)
			) || (
				   ed->state == ML_STATE_SELECT
				&& ml_choose (
					l.folded,
					   selection_first (ed) <= vl.data [l.last].src
					&& selection_last (ed) >= vl.data [l.first].src,
					   src >= selection_first (ed)
					&& src <= selection_last (ed)
				)
			);

			layout.result = append_line_row (
				&row,
				ed,
				ml_session_get (&ed->session, src),
				ml_results_get (&ed->results, src),
				line + 1,
				num_width,
				highlight,
				s->cols,
				gutter_for (
					l.kind,
					l.domain,
					line == l.first,
					line == l.last && !l.folded
				),
				ml_choose (l.folded, vl.data [l.last].src - src, 0)
			);

			ri.kind     = ml_choose (l.folded, 2, 1);
			ri.src      = src;
			ri.text_col = num_width + 1 + gutter_width;

			if (layout.result == ML_OK && highlight)
			{
				layout.result = reverse_row (&row, s->cols);
			}

			if (
				   here
				&& ed->state == ML_STATE_SELECT
				&& ml_choose (l.folded, 1, src == ed->session.cursor)
			)
			{
				layout.cursor.row = (uint32_t)(r + 1);
				layout.cursor.col = 1;
			}
		}

		if (layout.result == ML_OK)
		{
			layout.result = MlRowVec_push (&layout.rows, row);
		}

		if (layout.result == ML_OK)
		{
			layout.result = MlRowInfoVec_push (&layout.info, ri);
		}

		if (layout.result != ML_OK)
		{
			ml_str_free (&row);
		}

		/* the next row belongs to the same line, or to the one after it */
		sub++;

		if (line < total && sub >= line_height (ed, &vl, line))
		{
			line++;
			sub = 0;

			/* the lines hidden in a folded block take no row */
			while (line < total && line_height (ed, &vl, line) == 0)
			{
				line++;
			}
		}
	}

	VLineVec_free (&vl);

	/* the hint needs room under the line being typed */
	if (
		   layout.result == ML_OK
		&& s->rows >= 6
		&& ml_screen_hint_active (ed)
	)
	{
		layout.result = layout_hint (&layout, s, ed);
	}

	if (layout.result != ML_OK)
	{
		ml_layout_free (&layout);
	}

	return layout;
}

MlHit ml_screen_hit (
	MlScreen       *s,
	const MlEditor *ed,
	uint32_t       col,
	uint32_t       row
)
{
	MlHit    hit    =
	{
		.valid  = 0,
		.folded = 0,
		.line   = 0,
		.byte   = 0
	};

	MlLayout layout = ml_screen_layout (s, ed);

	if (
		   layout.result == ML_OK
		&& row >= 1
		&& col >= 1
		&& row <= layout.info.len
		&& layout.info.data [row - 1].kind != 0
	)
	{
		MlRowInfo ri   = layout.info.data [row - 1];
		int32_t   typed = (
			   (ed->state == ML_STATE_MATH || ed->state == ML_STATE_NOTE)
			&& ri.src == ed->edit_index
		);
		MlSlice   text  = ml_choose (
			typed,
			ml_str_view (ed->edit.text),
			ml_session_get (&ed->session, ri.src).text
		);
		uint64_t cell  = col - 1;

		hit.valid  = 1;
		hit.folded = ri.kind == 2;
		hit.line   = ri.src;
		hit.byte   = ml_choose (
			cell > ri.text_col,
			byte_at_column (text, cell - ri.text_col + ri.first_col),
			byte_at_column (text, ri.first_col)
		);
	}

	ml_layout_free (&layout);
	return hit;
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
