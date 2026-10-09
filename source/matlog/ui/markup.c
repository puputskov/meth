#include <matlog/ui/markup.h>

#include <matlog/core/utf8.h>

typedef enum MarkKind
{
	MARK_BOLD = 0,
	MARK_ITALIC_STAR,
	MARK_ITALIC_UNDER,
	MARK_UNDERLINE,
	MARK_STRIKE,
	MARK_CODE
} MarkKind;

/* A mark found in the text: where, how long, what it may do. */
typedef struct Mark
{
	MarkKind kind;
	uint64_t start;
	uint64_t len;
	int32_t  can_open;
	int32_t  can_close;
	int32_t  partner; /* index of the mark it is paired with, -1 if none */
} Mark;

ML_VEC_DEFINE (MarkVec, Mark)
ML_VEC_DEFINE (IndexVec, uint64_t)

void ml_markup_init (MlMarkup *m)
{
	ml_str_init (&m->plain);
	MlByteVec_init (&m->styles);
}

void ml_markup_free (MlMarkup *m)
{
	ml_str_free (&m->plain);
	MlByteVec_free (&m->styles);
}

int32_t ml_markup_has_styles (const MlMarkup *m)
{
	int32_t any = 0;

	for (uint64_t i = 0; i < m->styles.len; i++)
	{
		any = any || m->styles.data [i] != 0;
	}

	return any;
}

static int32_t is_space (uint32_t c)
{
	return c == ' ' || c == '\t' || c == 0;
}

static int32_t is_word (uint32_t c)
{
	return (
		   (c >= '0' && c <= '9')
		|| (c >= 'a' && c <= 'z')
		|| (c >= 'A' && c <= 'Z')
		|| c == '_'
		|| c >= 0x80
	);
}

/* The byte before pos (0 at the start) and the byte at pos (0 at the end). */
static uint32_t byte_before (MlSlice t, uint64_t pos)
{
	return ml_choose (pos > 0, (uint8_t)t.data [pos - 1], 0);
}

static uint32_t byte_at (MlSlice t, uint64_t pos)
{
	return ml_choose (pos < t.len, (uint8_t)t.data [pos], 0);
}

/* Is the text at pos the tag, in any case? ("<u>", "</u>") */
static int32_t tag_at (MlSlice t, uint64_t pos, const char *tag)
{
	int32_t same = 1;

	for (uint64_t i = 0; tag [i] != '\0' && same; i++)
	{
		uint32_t c = byte_at (t, pos + i);

		c    = ml_choose (c >= 'A' && c <= 'Z', c + 32, c);
		same = c == (uint8_t)tag [i];
	}

	return same;
}

static MlResult add_mark (
	MarkVec  *marks,
	MarkKind kind,
	uint64_t start,
	uint64_t len,
	int32_t  can_open,
	int32_t  can_close
)
{
	Mark m =
	{
		.kind      = kind,
		.start     = start,
		.len       = len,
		.can_open  = can_open,
		.can_close = can_close,
		.partner   = -1
	};

	return MarkVec_push (marks, m);
}

/*
 * The mark made of a run of the character c at pos (len of them, 1 or more).
 * Markdown's rule: a mark opens when a non space follows it and closes when
 * a non space precedes it; for the underscore a word character on the
 * outside forbids it.
 */
static void flanking (
	MlSlice  t,
	uint64_t pos,
	uint64_t len,
	int32_t  underscore,
	int32_t  *can_open,
	int32_t  *can_close
)
{
	uint32_t before = byte_before (t, pos);
	uint32_t after  = byte_at (t, pos + len);

	*can_open  = !is_space (after);
	*can_close = !is_space (before);

	if (underscore)
	{
		*can_open  = *can_open && !is_word (before);
		*can_close = *can_close && !is_word (after);
	}
}

/*
 * Finds the marks of the text, left to right. The end of a code span is found
 * here too, so what is inside it is skipped.
 */
static MlResult find_marks (MarkVec *marks, MlSlice t)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	while (r == ML_OK && pos < t.len)
	{
		uint32_t c = byte_at (t, pos);

		if (c == '\\' && pos + 1 < t.len)
		{
			/* an escaped character is not a mark */
			pos += 2;
		}

		else if (c == '`')
		{
			uint64_t end = pos + 1;

			while (end < t.len && t.data [end] != '`')
			{
				end++;
			}

			if (end < t.len)
			{
				r   = add_mark (marks, MARK_CODE, pos, 1, 1, 0);
				r   = ml_choose (
					r == ML_OK,
					add_mark (marks, MARK_CODE, end, 1, 0, 1),
					r
				);
				pos = end + 1;
			}

			else
			{
				pos++;
			}
		}

		else if (c == '*' || c == '_')
		{
			uint64_t len = 0;
			int32_t  open;
			int32_t  close;

			while (byte_at (t, pos + len) == c)
			{
				len++;
			}

			flanking (t, pos, len, c == '_', &open, &close);

			/* a run of three: italic then bold if it opens, else bold first */
			for (uint64_t k = 0; k < len && r == ML_OK; )
			{
				uint64_t left   = len - k;
				int32_t  as_bold = c == '*' && left >= 2 && !(
					   left == 3
					&& open
					&& !close
				);
				uint64_t take  = ml_choose (as_bold, 2, 1);

				r = add_mark (
					marks,
					ml_choose (
						as_bold,
						MARK_BOLD,
						ml_choose (
						c == '*',
						MARK_ITALIC_STAR,
						MARK_ITALIC_UNDER
					)
					),
					pos + k,
					take,
					open,
					close
				);
				k += take;
			}

			pos += len;
		}

		else if (c == '~' && byte_at (t, pos + 1) == '~')
		{
			int32_t open;
			int32_t close;

			flanking (t, pos, 2, 0, &open, &close);
			r    = add_mark (marks, MARK_STRIKE, pos, 2, open, close);
			pos += 2;
		}

		else if (tag_at (t, pos, "<u>"))
		{
			r    = add_mark (marks, MARK_UNDERLINE, pos, 3, 1, 0);
			pos += 3;
		}

		else if (tag_at (t, pos, "</u>"))
		{
			r    = add_mark (marks, MARK_UNDERLINE, pos, 4, 0, 1);
			pos += 4;
		}

		else
		{
			pos++;
		}
	}

	return r;
}

/*
 * Pairs the marks: a mark that can close takes the nearest open mark of its
 * kind; the marks opened after that one are left unpaired. A mark that cannot
 * close, or has nothing to close, may open.
 */
static MlResult pair_marks (MarkVec *marks)
{
	MlResult r = ML_OK;
	IndexVec open;

	IndexVec_init (&open);

	for (uint64_t i = 0; i < marks->len && r == ML_OK; i++)
	{
		Mark    *m     = &marks->data [i];
		int32_t closed = 0;

		/*
		 * Marks in one run of stars have no order of their own: when this one
		 * would close past an open mark that the next one in the run closes,
		 * they trade places (**a *b*** closes the italic first).
		 */
		if (
			   m->can_close
			&& open.len > 0
			&& marks->data [open.data [open.len - 1]].kind != m->kind
			&& i + 1 < marks->len
			&& marks->data [i + 1].start == m->start + m->len
			&& marks->data [i + 1].can_close
			&& marks->data [i + 1].kind
				== marks->data [open.data [open.len - 1]].kind
		)
		{
			Mark *n    = &marks->data [i + 1];
			Mark saved = *m;

			m->kind   = n->kind;
			m->len    = n->len;
			n->kind   = saved.kind;
			n->len    = saved.len;
			n->start  = saved.start + m->len;
		}

		if (m->can_close)
		{
			for (uint64_t k = open.len; k > 0 && !closed; k--)
			{
				uint64_t o = open.data [k - 1];

				if (marks->data [o].kind == m->kind)
				{
					marks->data [o].partner = (int32_t)i;
					m->partner              = (int32_t)o;
					open.len                = k - 1;
					closed                  = 1;
				}
			}
		}

		if (!closed && m->can_open)
		{
			r = IndexVec_push (&open, i);
		}
	}

	IndexVec_free (&open);
	return r;
}

static uint32_t style_of (MarkKind kind)
{
	uint32_t s = ML_STYLE_CODE;

	s = ml_choose (kind == MARK_BOLD, ML_STYLE_BOLD, s);
	s = ml_choose (
		kind == MARK_ITALIC_STAR || kind == MARK_ITALIC_UNDER,
		ML_STYLE_ITALIC,
		s
	);
	s = ml_choose (kind == MARK_UNDERLINE, ML_STYLE_UNDERLINE, s);
	s = ml_choose (kind == MARK_STRIKE, ML_STYLE_STRIKE, s);
	return s;
}

static MlResult parse (MlMarkup *m, MlSlice text, int32_t live)
{
	MlResult r = ML_OK;
	MarkVec  marks;
	uint64_t next = 0; /* the next mark that may start at the byte */
	uint32_t active = 0;
	uint64_t pos  = 0;

	MarkVec_init (&marks);
	ml_str_clear (&m->plain);
	MlByteVec_clear (&m->styles);
	r = find_marks (&marks, text);

	if (r == ML_OK)
	{
		r = pair_marks (&marks);
	}

	while (r == ML_OK && pos < text.len)
	{
		/* a paired mark here: it takes effect and its bytes are left out */
		while (next < marks.len && marks.data [next].start < pos)
		{
			next++;
		}

		if (next < marks.len && marks.data [next].start == pos)
		{
			Mark mk = marks.data [next];

			if (mk.partner >= 0)
			{
				/* the style is on between the opener and the closer */
				active ^= style_of (mk.kind);

				/* live: the mark stays in the text, drawn dim */
				if (live)
				{
					r = ml_str_append (
						&m->plain,
						ml_slice_sub (text, pos, mk.len)
					);

					for (uint64_t k = 0; k < mk.len && r == ML_OK; k++)
					{
						r = MlByteVec_push (&m->styles, (uint8_t)ML_STYLE_MARK);
					}
				}

				pos += mk.len;
			}

			else
			{
				r = ml_str_append (&m->plain, ml_slice_sub (text, pos, mk.len));

				for (uint64_t k = 0; k < mk.len && r == ML_OK; k++)
				{
					r = MlByteVec_push (&m->styles, (uint8_t)active);
				}

				pos += mk.len;
			}

			next++;
		}

		else if (
			   byte_at (text, pos) == '\\'
			&& pos + 1 < text.len
			&& (
				   text.data [pos + 1] == '*'
				|| text.data [pos + 1] == '_'
				|| text.data [pos + 1] == '~'
				|| text.data [pos + 1] == '`'
				|| text.data [pos + 1] == '<'
				|| text.data [pos + 1] == '\\'
			)
		)
		{
			/* the backslash is dropped, the character stays (live: both) */
			uint64_t from = ml_choose (live, pos, pos + 1);
			uint64_t take = ml_choose (live, 2, 1);

			r = ml_str_append (&m->plain, ml_slice_sub (text, from, take));

			for (uint64_t k = 0; k < take && r == ML_OK; k++)
			{
				r = MlByteVec_push (&m->styles, (uint8_t)active);
			}

			pos += 2;
		}

		else
		{
			r = ml_str_append (&m->plain, ml_slice_sub (text, pos, 1));

			if (r == ML_OK)
			{
				r = MlByteVec_push (&m->styles, (uint8_t)active);
			}

			pos++;
		}
	}

	MarkVec_free (&marks);
	return r;
}

MlResult ml_markup_parse (MlMarkup *m, MlSlice text)
{
	return parse (m, text, 0);
}

MlResult ml_markup_parse_live (MlMarkup *m, MlSlice text)
{
	return parse (m, text, 1);
}

/* The escape sequence that sets exactly the styles of mask. */
static MlResult set_style (MlStr *out, uint32_t mask)
{
	MlResult r = ml_str_append (out, ml_slice_cstr ("\x1b[0m"));

	if (r == ML_OK && (mask & ML_STYLE_BOLD))
	{
		r = ml_str_append (out, ml_slice_cstr ("\x1b[1m"));
	}

	if (r == ML_OK && (mask & ML_STYLE_ITALIC))
	{
		r = ml_str_append (out, ml_slice_cstr ("\x1b[3m"));
	}

	if (r == ML_OK && (mask & ML_STYLE_UNDERLINE))
	{
		r = ml_str_append (out, ml_slice_cstr ("\x1b[4m"));
	}

	if (r == ML_OK && (mask & ML_STYLE_STRIKE))
	{
		r = ml_str_append (out, ml_slice_cstr ("\x1b[9m"));
	}

	if (r == ML_OK && (mask & ML_STYLE_CODE))
	{
		r = ml_str_append (out, ml_slice_cstr ("\x1b[36m"));
	}

	if (r == ML_OK && (mask & ML_STYLE_MARK))
	{
		r = ml_str_append (out, ml_slice_cstr ("\x1b[2m"));
	}

	return r;
}

MlResult ml_markup_append (
	MlStr          *out,
	const MlMarkup *m,
	uint64_t       from,
	uint64_t       len
)
{
	MlResult r      = ML_OK;
	uint32_t shown  = 0;
	uint64_t end    = ml_choose (
		from + len > m->plain.len,
		m->plain.len,
		from + len
	);

	for (uint64_t i = from; i < end && r == ML_OK; i++)
	{
		uint32_t want = m->styles.data [i];

		if (want != shown)
		{
			r     = set_style (out, want);
			shown = want;
		}

		if (r == ML_OK)
		{
			r = ml_str_append (
				out,
				ml_slice_sub (ml_str_view (m->plain), i, 1)
			);
		}
	}

	if (r == ML_OK && shown != 0)
	{
		r = ml_str_append (out, ml_slice_cstr ("\x1b[0m"));
	}

	return r;
}
