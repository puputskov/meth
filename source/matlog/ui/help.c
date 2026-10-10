#include <matlog/ui/help.h>

#include <matlog/platform/key.h>

void ml_help_init (MlHelp *h)
{
	ml_docs_init (&h->docs);
	MlDocHitVec_init (&h->hits);
	MlHelpMarkVec_init (&h->history);
	ml_str_init (&h->query);
	h->loaded   = 0;
	h->open     = 0;
	h->results  = 0;
	h->page     = 0;
	h->top      = 0;
	h->has_link = 0;
	h->link     = 0;
	h->hit      = 0;
	h->cols     = 80;
	h->rows     = 24;
	h->keys     = NULL;
}

void ml_help_reload (MlHelp *h)
{
	if (h->loaded)
	{
		ml_docs_free (&h->docs);
		ml_docs_init (&h->docs);
		h->loaded  = 0;
		h->page    = 0;
		h->top     = 0;
		h->results = 0;
		MlHelpMarkVec_clear (&h->history);
		ml_doc_hits_free (&h->hits);
		MlDocHitVec_init (&h->hits);
	}
}

void ml_help_free (MlHelp *h)
{
	ml_doc_hits_free (&h->hits);
	MlHelpMarkVec_free (&h->history);
	ml_str_free (&h->query);
	ml_docs_free (&h->docs);
}

void ml_help_set_view (MlHelp *h, uint32_t cols, uint32_t rows)
{
	h->cols = ml_choose (cols > 0, cols, 80);
	h->rows = ml_choose (rows > 0, rows, 24);
}

uint64_t ml_help_width (const MlHelp *h)
{
	uint64_t room = ml_choose (h->cols > 2, h->cols - 2, 1);

	return ml_choose (room > 78, 78, ml_choose (room < 20, 20, room));
}

/* The rows of the page area: the screen without the status line. */
static uint64_t area_rows (const MlHelp *h)
{
	return ml_choose (h->rows > 1, h->rows - 1, 1);
}

static MlResult ensure_loaded (MlHelp *h)
{
	MlResult r = ML_OK;

	if (!h->loaded)
	{
		uint64_t page;
		uint64_t line;

		r = ml_docs_load (&h->docs, h->keys, &page, &line);

		if (r == ML_OK)
		{
			h->loaded = 1;
		}

		else
		{
			ml_docs_free (&h->docs);
			ml_docs_init (&h->docs);
		}
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

MlResult ml_help_set_page (MlHelp *h, MlSlice text)
{
	MlResult  r = ensure_loaded (h);
	MlDocPage p;
	uint64_t  line = 0;

	ml_doc_page_init (&p);

	if (r == ML_OK)
	{
		r = ml_doc_parse (&p, text, &line);
	}

	if (r == ML_OK)
	{
		uint64_t at = ml_docs_find (&h->docs, ml_str_view (p.id));

		if (at < h->docs.pages.len)
		{
			ml_doc_page_free (&h->docs.pages.data [at]);
			h->docs.pages.data [at] = p;
		}

		else
		{
			r = MlDocPageVec_push (&h->docs.pages, p);
		}
	}

	if (r != ML_OK)
	{
		ml_doc_page_free (&p);
	}

	return r;
}

MlResult ml_help_open (MlHelp *h, MlSlice topic)
{
	MlResult r    = ensure_loaded (h);
	MlSlice  want = trim (topic);

	if (r == ML_OK)
	{
		uint64_t page = ml_docs_find (
			&h->docs,
			ml_choose (want.len > 0, want, ml_slice_cstr ("index"))
		);

		ml_doc_hits_free (&h->hits);
		MlDocHitVec_init (&h->hits);
		h->history.len = 0;
		h->results     = 0;
		h->top         = 0;
		h->has_link    = 0;
		h->hit         = 0;

		if (page < h->docs.pages.len)
		{
			h->page = page;
			h->open = 1;
		}

		else if (want.len == 0)
		{
			h->page = 0;
			h->open = h->docs.pages.len > 0;
		}

		else
		{
			r = ml_docs_search (&h->docs, want, &h->hits);

			if (r == ML_OK && h->hits.len == 0)
			{
				r = ML_ERR_INVALID;
			}

			else if (r == ML_OK && h->hits.len == 1)
			{
				h->page = h->hits.data [0].page;
				h->open = 1;
			}

			else if (r == ML_OK)
			{
				h->results = 1;
				h->open    = 1;
				ml_str_clear (&h->query);
				r = ml_str_append (&h->query, want);
			}
		}
	}

	return r;
}

/* ---- the lines ---------------------------------------------------------- */

MlResult ml_help_layout (const MlHelp *h, MlDocLineVec *lines)
{
	return ml_doc_layout (
		lines,
		&h->docs.pages.data [h->page],
		ml_help_width (h)
	);
}

uint64_t ml_help_link_count (const MlDocLineVec *lines)
{
	uint64_t n = 0;

	for (uint64_t i = 0; i < lines->len; i++)
	{
		n += lines->data [i].links.len;
	}

	return n;
}

void ml_help_link_place (
	const MlDocLineVec *lines,
	uint64_t           n,
	uint64_t           *line,
	uint64_t           *in_line
)
{
	uint64_t seen = 0;

	*line    = 0;
	*in_line = 0;

	for (uint64_t i = 0; i < lines->len; i++)
	{
		uint64_t count = lines->data [i].links.len;

		if (n >= seen && n < seen + count)
		{
			*line    = i;
			*in_line = n - seen;
		}

		seen += count;
	}
}

/* ---- keys --------------------------------------------------------------- */

static uint64_t clamp_top (uint64_t top, uint64_t total, uint64_t area)
{
	uint64_t max = ml_choose (total > area, total - area, 0);

	return ml_choose (top > max, max, top);
}

/* Scrolls so that line is visible. */
static uint64_t show_line (uint64_t top, uint64_t line, uint64_t area)
{
	uint64_t t = top;

	if (line < t)
	{
		t = line;
	}

	else if (line >= t + area)
	{
		t = line + 1 - area;
	}

	return t;
}

static MlResult push_mark (MlHelp *h)
{
	MlHelpMark m =
	{
		.page     = h->page,
		.top      = h->top,
		.link     = ml_choose (h->results, h->hit, h->link),
		.has_link = h->has_link,
		.results  = h->results
	};

	return MlHelpMarkVec_push (&h->history, m);
}

/* Shows a page: the history keeps where we were. */
static MlResult go_to (MlHelp *h, uint64_t page)
{
	MlResult r = push_mark (h);

	if (r == ML_OK)
	{
		h->page     = page;
		h->results  = 0;
		h->top      = 0;
		h->has_link = 0;
	}

	return r;
}

static MlResult go_back (MlHelp *h)
{
	if (h->history.len > 0)
	{
		MlHelpMark m = h->history.data [h->history.len - 1];

		h->history.len--;
		h->page     = m.page;
		h->top      = m.top;
		h->results  = m.results;
		h->has_link = m.has_link && !m.results;
		h->link     = ml_choose (m.results, 0, m.link);
		h->hit      = ml_choose (m.results, m.link, 0);
	}

	return ML_OK;
}

/* The keys on a list of search results. */
static MlResult results_key (MlHelp *h, MlKey key)
{
	MlResult r    = ML_OK;
	uint64_t area = area_rows (h);

	if (key.code == ML_KEY_UP && h->hit > 0)
	{
		h->hit--;
	}

	else if (key.code == ML_KEY_DOWN && h->hit + 1 < h->hits.len)
	{
		h->hit++;
	}

	else if (key.code == ML_KEY_PAGE_UP)
	{
		h->hit = ml_choose (h->hit > area, h->hit - area, 0);
	}

	else if (key.code == ML_KEY_PAGE_DOWN)
	{
		h->hit = ml_choose (
			h->hit + area < h->hits.len,
			h->hit + area,
			h->hits.len - 1
		);
	}

	else if (key.code == ML_KEY_HOME)
	{
		h->hit = 0;
	}

	else if (key.code == ML_KEY_END)
	{
		h->hit = h->hits.len - 1;
	}

	else if (key.code == ML_KEY_ENTER)
	{
		r = go_to (h, h->hits.data [h->hit].page);
	}

	/* the list starts after two lines, the title and a blank one */
	h->top = show_line (h->top, h->hit + 2, area);
	h->top = clamp_top (h->top, h->hits.len + 2, area);
	return r;
}

static MlResult page_key (MlHelp *h, MlKey key)
{
	MlResult     r     = ML_OK;
	uint64_t     area  = area_rows (h);
	MlDocLineVec lines;
	uint64_t     total = 0;
	uint64_t     links = 0;

	MlDocLineVec_init (&lines);
	r = ml_help_layout (h, &lines);

	if (r == ML_OK)
	{
		total = lines.len;
		links = ml_help_link_count (&lines);
	}

	if (r != ML_OK)
	{
		total = 0;
	}

	else if (key.code == ML_KEY_UP)
	{
		h->top = ml_choose (h->top > 0, h->top - 1, 0);
	}

	else if (key.code == ML_KEY_DOWN)
	{
		h->top++;
	}

	else if (key.code == ML_KEY_PAGE_UP)
	{
		h->top = ml_choose (h->top > area - 1, h->top - (area - 1), 0);
	}

	else if (key.code == ML_KEY_PAGE_DOWN)
	{
		h->top += ml_choose (area > 1, area - 1, 1);
	}

	else if (key.code == ML_KEY_HOME)
	{
		h->top = 0;
	}

	else if (key.code == ML_KEY_END)
	{
		h->top = total;
	}

	else if (key.code == ML_KEY_TAB && links > 0)
	{
		int32_t back = (key.mods & ML_MOD_SHIFT) != 0;

		if (!h->has_link)
		{
			h->link = ml_choose (back, links - 1, 0);
		}

		else if (back)
		{
			h->link = ml_choose (h->link > 0, h->link - 1, links - 1);
		}

		else
		{
			h->link = ml_choose (h->link + 1 < links, h->link + 1, 0);
		}

		h->has_link = 1;

		{
			uint64_t line;
			uint64_t in_line;

			ml_help_link_place (&lines, h->link, &line, &in_line);
			h->top = show_line (h->top, line, area);
		}
	}

	else if (key.code == ML_KEY_ENTER && h->has_link && h->link < links)
	{
		uint64_t line;
		uint64_t in_line;
		uint64_t to;

		ml_help_link_place (&lines, h->link, &line, &in_line);
		to = ml_docs_find (
			&h->docs,
			ml_doc_line_target (&lines.data [line], in_line)
		);

		if (to < h->docs.pages.len)
		{
			r = go_to (h, to);
		}
	}

	h->top = clamp_top (h->top, total, area);
	ml_doc_lines_free (&lines);
	return r;
}

MlResult ml_help_key (MlHelp *h, MlKey key, int32_t *search)
{
	MlResult r = ML_OK;

	*search = 0;

	if (
		   key.code == ML_KEY_ESCAPE
		|| (key.code == ML_KEY_CHAR && key.cp == 'q' && key.mods == 0)
	)
	{
		h->open = 0;
	}

	else if (key.code == ML_KEY_CHAR && key.cp == '/' && key.mods == 0)
	{
		h->open = 0;
		*search = 1;
	}

	else if (key.code == ML_KEY_CHAR && key.cp == 'h' && key.mods == 0)
	{
		uint64_t home = ml_docs_find (&h->docs, ml_slice_cstr ("index"));

		if (home < h->docs.pages.len && (h->results || home != h->page))
		{
			r = go_to (h, home);
		}
	}

	else if (key.code == ML_KEY_BACKSPACE)
	{
		r = go_back (h);
	}

	else if (h->results && h->hits.len > 0)
	{
		r = results_key (h, key);
	}

	else if (!h->results)
	{
		r = page_key (h, key);
	}

	return r;
}
