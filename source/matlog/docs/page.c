#include <matlog/docs/page.h>

#include <matlog/core/utf8.h>

#include <string.h>

void ml_doc_page_init (MlDocPage *p)
{
	ml_str_init (&p->id);
	ml_str_init (&p->title);
	ml_str_init (&p->summary);
	ml_str_init (&p->keywords);
	ml_str_init (&p->see);
	MlDocBlockVec_init (&p->blocks);
}

void ml_doc_page_free (MlDocPage *p)
{
	for (uint64_t i = 0; i < p->blocks.len; i++)
	{
		ml_str_free (&p->blocks.data [i].text);
	}

	MlDocBlockVec_free (&p->blocks);
	ml_str_free (&p->see);
	ml_str_free (&p->keywords);
	ml_str_free (&p->summary);
	ml_str_free (&p->title);
	ml_str_free (&p->id);
}

static const char *const block_names [ML_DOC_BLOCK_COUNT] =
{
	[ML_DOC_HEADING]   = "heading",
	[ML_DOC_PARAGRAPH] = "paragraph",
	[ML_DOC_ITEM]      = "item",
	[ML_DOC_EXAMPLE]   = "example",
	[ML_DOC_KEYS]      = "keys",
	[ML_DOC_COMMAND]   = "command",
	[ML_DOC_CODE]      = "code"
};

const char *ml_doc_block_name (MlDocBlockKind kind)
{
	return ml_choose (kind < ML_DOC_BLOCK_COUNT, block_names [kind], "?");
}

/* ---- parsing ------------------------------------------------------------ */

static int32_t is_space (char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static MlSlice trim (MlSlice s)
{
	uint64_t start = 0;
	uint64_t end   = s.len;

	while (start < end && is_space (s.data [start]))
	{
		start++;
	}

	while (end > start && is_space (s.data [end - 1]))
	{
		end--;
	}

	return ml_slice_sub (s, start, end - start);
}

/* The next line of text from *pos on, without its line break. */
static MlSlice next_line (MlSlice text, uint64_t *pos)
{
	uint64_t start = *pos;
	uint64_t end   = start;

	while (end < text.len && text.data [end] != '\n')
	{
		end++;
	}

	*pos = ml_choose (end < text.len, end + 1, end);

	while (end > start && text.data [end - 1] == '\r')
	{
		end--;
	}

	return ml_slice_sub (text, start, end - start);
}

static int32_t is_word (MlSlice a, const char *b)
{
	return ml_slice_equal (a, ml_slice_cstr (b));
}

/* Stores a header entry; 0 when the name is not one of the page. */
static int32_t set_header (
	MlDocPage *p,
	MlSlice    name,
	MlSlice    value,
	MlResult  *r
)
{
	MlStr   *target = NULL;

	if (is_word (name, "id"))
	{
		target = &p->id;
	}

	else if (is_word (name, "title"))
	{
		target = &p->title;
	}

	else if (is_word (name, "summary"))
	{
		target = &p->summary;
	}

	else if (is_word (name, "keywords"))
	{
		target = &p->keywords;
	}

	else if (is_word (name, "see"))
	{
		target = &p->see;
	}

	if (target != NULL)
	{
		*r = ml_str_append (target, value);
	}

	return target != NULL;
}

static MlDocBlockKind fence_kind (MlSlice info, int32_t *ok)
{
	MlDocBlockKind kind = ML_DOC_CODE;

	*ok = 1;

	if (is_word (info, "example"))
	{
		kind = ML_DOC_EXAMPLE;
	}

	else if (is_word (info, "keys"))
	{
		kind = ML_DOC_KEYS;
	}

	else if (is_word (info, "command"))
	{
		kind = ML_DOC_COMMAND;
	}

	else if (info.len > 0 && !is_word (info, "code"))
	{
		*ok = 0;
	}

	return kind;
}

/* Adds a block with the text; the page owns the text afterwards. */
static MlResult push_block (
	MlDocPage      *p,
	MlDocBlockKind kind,
	int32_t        level,
	MlSlice        text
)
{
	MlDocBlock b =
	{
		.kind  = kind,
		.level = level
	};

	MlResult r;

	ml_str_init (&b.text);
	r = ml_str_append (&b.text, text);

	if (r == ML_OK)
	{
		r = MlDocBlockVec_push (&p->blocks, b);
	}

	if (r != ML_OK)
	{
		ml_str_free (&b.text);
	}

	return r;
}

MlResult ml_doc_parse (MlDocPage *p, MlSlice text, uint64_t *line)
{
	MlResult r        = ML_OK;
	uint64_t pos      = 0;
	uint64_t number   = 0;
	int32_t  in_head  = 1;
	int32_t  open = 0; /* the last block is a paragraph or item still growing */
	int32_t  fenced   = 0;
	MlDocBlockKind fence = ML_DOC_CODE;
	uint64_t fence_at = 0;
	MlStr    fence_text;

	ml_str_init (&fence_text);
	*line = 0;

	while (pos < text.len && r == ML_OK)
	{
		MlSlice raw   = next_line (text, &pos);
		MlSlice t     = trim (raw);

		number++;

		if (in_head && t.len == 0)
		{
			/* blank lines before the header, or the end of it */
			in_head = ml_choose (p->id.len > 0 || p->title.len > 0, 0, 1);
		}

		else if (in_head)
		{
			uint64_t colon = 0;

			while (colon < t.len && t.data [colon] != ':')
			{
				colon++;
			}

			if (
				   colon >= t.len
				|| !set_header (
					   p,
					   trim (ml_slice_sub (t, 0, colon)),
					   trim (ml_slice_sub (t, colon + 1, t.len - colon - 1)),
					   &r
				   )
			)
			{
				r = ML_ERR_PARSE;
			}
		}

		else if (fenced)
		{
			if (is_word (t, "```"))
			{
				r      = push_block (p, fence, 0, ml_str_view (fence_text));
				fenced = 0;
				ml_str_clear (&fence_text);
			}

			else
			{
				if (fence_text.len > 0)
				{
					r = ml_str_append (&fence_text, ml_slice_cstr ("\n"));
				}

				if (r == ML_OK)
				{
					r = ml_str_append (&fence_text, raw);
				}
			}
		}

		else if (t.len >= 3 && memcmp (t.data, "```", 3) == 0)
		{
			int32_t ok;

			fence    = fence_kind (trim (ml_slice_sub (t, 3, t.len - 3)), &ok);
			fenced   = 1;
			fence_at = number;
			open     = 0;
			r        = ml_choose (ok, ML_OK, ML_ERR_PARSE);
		}

		else if (t.len == 0)
		{
			open = 0;
		}

		else if (t.data [0] == '#')
		{
			int32_t level = 0;

			while ((uint64_t)level < t.len && t.data [level] == '#')
			{
				level++;
			}

			if (level > 3 || (uint64_t)level + 1 >= t.len
				|| t.data [level] != ' ')
			{
				r = ML_ERR_PARSE;
			}

			else
			{
				r = push_block (
					p,
					ML_DOC_HEADING,
					level,
					trim (ml_slice_sub (
						t,
						(uint64_t)level,
						t.len - (uint64_t)level
					))
				);
			}

			open = 0;
		}

		else if (t.len >= 2 && t.data [0] == '-' && t.data [1] == ' ')
		{
			r = push_block (
				p,
				ML_DOC_ITEM,
				0,
				trim (ml_slice_sub (t, 2, t.len - 2))
			);
			open = 1;
		}

		else if (open && p->blocks.len > 0)
		{
			MlDocBlock *last = &p->blocks.data [p->blocks.len - 1];
			int32_t    indented = raw.len > 0 && is_space (raw.data [0]);

			if (last->kind == ML_DOC_ITEM && !indented)
			{
				r    = push_block (p, ML_DOC_PARAGRAPH, 0, t);
				open = 1;
			}

			else
			{
				r = ml_str_append (&last->text, ml_slice_cstr (" "));

				if (r == ML_OK)
				{
					r = ml_str_append (&last->text, t);
				}
			}
		}

		else
		{
			r    = push_block (p, ML_DOC_PARAGRAPH, 0, t);
			open = 1;
		}

		if (r != ML_OK && *line == 0)
		{
			*line = number;
		}
	}

	if (r == ML_OK && fenced)
	{
		r     = ML_ERR_PARSE;
		*line = fence_at;
	}

	if (r == ML_OK && (p->id.len == 0 || p->title.len == 0))
	{
		r     = ML_ERR_PARSE;
		*line = 1;
	}

	ml_str_free (&fence_text);
	return r;
}

/* ---- links -------------------------------------------------------------- */

MlResult ml_doc_plain (MlStr *out, MlDocLinkVec *links, MlSlice text)
{
	MlResult r       = ML_OK;
	uint64_t pos     = 0;
	int32_t  in_code = 0;

	while (pos < text.len && r == ML_OK)
	{
		uint64_t close  = pos;
		uint64_t end    = pos;
		int32_t  is_link = 0;

		if (text.data [pos] == '`')
		{
			in_code = !in_code;
		}

		/* the text of a code span is not looked at for links */
		if (text.data [pos] == '[' && !in_code)
		{
			close = pos + 1;

			while (close < text.len && text.data [close] != ']'
				   && text.data [close] != '[')
			{
				close++;
			}

			is_link = (
				   close + 1 < text.len
				&& text.data [close] == ']'
				&& text.data [close + 1] == '('
			);

			if (is_link)
			{
				end = close + 2;

				while (end < text.len && text.data [end] != ')'
					   && text.data [end] != ' ')
				{
					end++;
				}

				is_link = end < text.len && text.data [end] == ')'
						  && end > close + 2;
			}
		}

		if (is_link)
		{
			MlDocLink l =
			{
				.start  = out->len,
				.len    = close - pos - 1,
				.target = ml_slice_sub (text, close + 2, end - close - 2)
			};

			r = ml_str_append (
				out,
				ml_slice_sub (text, pos + 1, close - pos - 1)
			);

			if (r == ML_OK)
			{
				r = MlDocLinkVec_push (links, l);
			}

			pos = end + 1;
		}

		else
		{
			r = ml_str_append (out, ml_slice_sub (text, pos, 1));
			pos++;
		}
	}

	return r;
}

/* ---- layout ------------------------------------------------------------- */

void ml_doc_lines_free (MlDocLineVec *lines)
{
	for (uint64_t i = 0; i < lines->len; i++)
	{
		ml_str_free (&lines->data [i].text);
		ml_str_free (&lines->data [i].targets);
		MlDocLinkVec_free (&lines->data [i].links);
	}

	MlDocLineVec_free (lines);
}

MlSlice ml_doc_line_target (const MlDocLine *line, uint64_t n)
{
	MlSlice  all   = ml_str_view (line->targets);
	uint64_t pos   = 0;
	uint64_t index = 0;
	MlSlice  found = ml_slice ("", 0);

	while (pos < all.len)
	{
		uint64_t end = pos;

		while (end < all.len && all.data [end] != '\n')
		{
			end++;
		}

		if (index == n)
		{
			found = ml_slice_sub (all, pos, end - pos);
		}

		index++;
		pos = end + 1;
	}

	return found;
}

/* A new line of a style, with the text; it is pushed to lines. */
static MlResult push_line (
	MlDocLineVec *lines,
	MlDocStyle   style,
	uint64_t     block,
	MlSlice      text
)
{
	MlDocLine l =
	{
		.style = style,
		.block = block
	};

	MlResult r;

	ml_str_init (&l.text);
	ml_str_init (&l.targets);
	MlDocLinkVec_init (&l.links);
	r = ml_str_append (&l.text, text);

	if (r == ML_OK)
	{
		r = MlDocLineVec_push (lines, l);
	}

	if (r != ML_OK)
	{
		ml_str_free (&l.text);
		ml_str_free (&l.targets);
		MlDocLinkVec_free (&l.links);
	}

	return r;
}

/* Adds to the last line the links of plain that lie in a..b, shifted by shift.
 */
static MlResult add_links (
	MlDocLineVec       *lines,
	const MlDocLinkVec *links,
	uint64_t           a,
	uint64_t           b,
	uint64_t           shift
)
{
	MlResult   r    = ML_OK;
	MlDocLine *last = &lines->data [lines->len - 1];

	for (uint64_t i = 0; i < links->len && r == ML_OK; i++)
	{
		uint64_t s = links->data [i].start;
		uint64_t e = s + links->data [i].len;
		uint64_t from = ml_choose (s > a, s, a);
		uint64_t to   = ml_choose (e < b, e, b);

		if (from < to)
		{
			MlDocLink l =
			{
				.start  = from - a + shift,
				.len    = to - from,
				.target = ml_slice ("", 0)
			};

			r = MlDocLinkVec_push (&last->links, l);

			if (r == ML_OK)
			{
				r = ml_str_append (&last->targets, links->data [i].target);
			}

			if (r == ML_OK)
			{
				r = ml_str_append (&last->targets, ml_slice_cstr ("\n"));
			}
		}
	}

	return r;
}

/*
 * Wraps the plain text at word boundaries in lines of at most width columns:
 * the first line starts with first, the others with rest.
 */
static MlResult wrap (
	MlDocLineVec       *lines,
	MlDocStyle         style,
	uint64_t           block,
	MlSlice            plain,
	const MlDocLinkVec *links,
	MlSlice            first,
	MlSlice            rest,
	uint64_t           width
)
{
	MlResult r       = ML_OK;
	uint64_t pos     = 0;
	int32_t  is_first = 1;

	while (pos < plain.len && r == ML_OK)
	{
		MlSlice  prefix = ml_choose (is_first, first, rest);
		uint64_t room   = ml_choose (width > prefix.len, width - prefix.len, 1);
		uint64_t start;
		uint64_t end;
		uint64_t used = 0;
		int32_t  more = 1;
		MlStr    text;

		while (pos < plain.len && plain.data [pos] == ' ')
		{
			pos++;
		}

		start = pos;
		end   = pos;

		/* words are added while they fit; one word always goes in */
		while (pos < plain.len && more)
		{
			uint64_t word_end = pos;
			uint64_t w;

			while (word_end < plain.len && plain.data [word_end] != ' ')
			{
				word_end++;
			}

			w    = ml_utf8_width (ml_slice_sub (plain, pos, word_end - pos));
			more = !(used > 0 && used + 1 + w > room);

			if (more)
			{
				used = used + ml_choose (used > 0, 1, 0) + w;
				end  = word_end;
				pos  = word_end;

				while (pos < plain.len && plain.data [pos] == ' ')
				{
					pos++;
				}
			}
		}

		ml_str_init (&text);
		r = ml_str_append (&text, prefix);

		if (r == ML_OK)
		{
			r = ml_str_append (&text, ml_slice_sub (plain, start, end - start));
		}

		if (r == ML_OK)
		{
			r = push_line (lines, style, block, ml_str_view (text));
		}

		if (r == ML_OK)
		{
			r = add_links (lines, links, start, end, prefix.len);
		}

		ml_str_free (&text);
		is_first = 0;

		if (pos == start)
		{
			pos = plain.len;
		}
	}

	return r;
}

/* The lines of text, one line each, in a style. */
static MlResult push_text_lines (
	MlDocLineVec *lines,
	MlDocStyle   style,
	uint64_t     block,
	MlSlice      text
)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	while (pos <= text.len && r == ML_OK)
	{
		uint64_t end = pos;

		while (end < text.len && text.data [end] != '\n')
		{
			end++;
		}

		r = push_line (
			lines,
			style,
			block,
			ml_slice_sub (text, pos, end - pos)
		);
		pos = end + 1;
	}

	return r;
}

/* An example: inputs as typed, the lines after => as results. */
static MlResult push_example (MlDocLineVec *lines, uint64_t block, MlSlice text)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	while (pos <= text.len && r == ML_OK)
	{
		uint64_t end = pos;
		MlSlice  row;

		while (end < text.len && text.data [end] != '\n')
		{
			end++;
		}

		row = ml_slice_sub (text, pos, end - pos);

		if (row.len >= 2 && row.data [0] == '=' && row.data [1] == '>')
		{
			MlStr shown;

			ml_str_init (&shown);
			r = ml_str_append (&shown, ml_slice_cstr ("= "));

			if (r == ML_OK)
			{
				r = ml_str_append (
					&shown,
					trim (ml_slice_sub (row, 2, row.len - 2))
				);
			}

			if (r == ML_OK)
			{
				r = push_line (
					lines,
					ML_DOC_STYLE_RESULT,
					block,
					ml_str_view (shown)
				);
			}

			ml_str_free (&shown);
		}

		else
		{
			r = push_line (lines, ML_DOC_STYLE_INPUT, block, row);
		}

		pos = end + 1;
	}

	return r;
}

MlResult ml_doc_layout (
	MlDocLineVec    *lines,
	const MlDocPage *page,
	uint64_t        width
)
{
	MlResult r
		= push_line (lines, ML_DOC_STYLE_TITLE, 0, ml_str_view (page->title));

	if (r == ML_OK && page->summary.len > 0)
	{
		MlDocLinkVec none;

		MlDocLinkVec_init (&none);
		r = wrap (
			lines,
			ML_DOC_STYLE_TEXT,
			0,
			ml_str_view (page->summary),
			&none,
			ml_slice ("", 0),
			ml_slice ("", 0),
			width
		);
		MlDocLinkVec_free (&none);
	}

	for (uint64_t i = 0; i < page->blocks.len && r == ML_OK; i++)
	{
		const MlDocBlock *b = &page->blocks.data [i];
		MlDocLinkVec     links;
		MlStr            plain;

		MlDocLinkVec_init (&links);
		ml_str_init (&plain);
		r = push_line (lines, ML_DOC_STYLE_BLANK, i, ml_slice ("", 0));

		if (r == ML_OK
			&& (b->kind == ML_DOC_PARAGRAPH || b->kind == ML_DOC_ITEM
				|| b->kind == ML_DOC_HEADING))
		{
			r = ml_doc_plain (&plain, &links, ml_str_view (b->text));
		}

		if (r == ML_OK && b->kind == ML_DOC_HEADING)
		{
			r = wrap (
				lines,
				ML_DOC_STYLE_HEADING,
				i,
				ml_str_view (plain),
				&links,
				ml_slice ("", 0),
				ml_slice ("", 0),
				width
			);
		}

		else if (r == ML_OK && b->kind == ML_DOC_PARAGRAPH)
		{
			r = wrap (
				lines,
				ML_DOC_STYLE_TEXT,
				i,
				ml_str_view (plain),
				&links,
				ml_slice ("", 0),
				ml_slice ("", 0),
				width
			);
		}

		else if (r == ML_OK && b->kind == ML_DOC_ITEM)
		{
			r = wrap (
				lines,
				ML_DOC_STYLE_ITEM,
				i,
				ml_str_view (plain),
				&links,
				ml_slice_cstr ("- "),
				ml_slice_cstr ("  "),
				width
			);
		}

		else if (r == ML_OK && b->kind == ML_DOC_EXAMPLE)
		{
			r = push_example (lines, i, ml_str_view (b->text));
		}

		else if (r == ML_OK)
		{
			r = push_text_lines (
				lines,
				ml_choose (
					b->kind == ML_DOC_KEYS,
					ML_DOC_STYLE_KEYS,
					ml_choose (
						b->kind == ML_DOC_COMMAND,
						ML_DOC_STYLE_COMMAND,
						ML_DOC_STYLE_CODE
					)
				),
				i,
				ml_str_view (b->text)
			);
		}

		ml_str_free (&plain);
		MlDocLinkVec_free (&links);
	}

	return r;
}
