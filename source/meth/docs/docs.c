#include <meth/docs/docs.h>
#include <meth/docs/examples.h>
#include <meth/docs/snippets.h>

#include <meth/texts/texts.h>
#include <meth/session/results.h>
#include <meth/session/session.h>

void ml_docs_init (MlDocs *d)
{
	MlDocPageVec_init (&d->pages);
}

void ml_docs_free (MlDocs *d)
{
	for (uint64_t i = 0; i < d->pages.len; i++)
	{
		ml_doc_page_free (&d->pages.data [i]);
	}

	MlDocPageVec_free (&d->pages);
}

MlResult ml_docs_load (
	MlDocs         *d,
	const MlKeymap *keys,
	uint64_t       *page,
	uint64_t       *line
)
{
	MlResult r    = ML_OK;
	MlKeymap own;
	int32_t  made = keys == NULL;

	*page = 0;
	*line = 0;

	if (made)
	{
		r    = ml_keymap_init (&own);
		keys = &own;
	}

	for (uint64_t i = 0; i < ml_doc_source_count && r == ML_OK; i++)
	{
		MlDocPage p;
		MlStr     text;

		ml_doc_page_init (&p);
		ml_str_init (&text);
		r = ml_keymap_expand (
			&text,
			keys,
			ml_slice_cstr (ml_doc_sources [i].text),
			'{',
			'}',
			0
		);

		if (r == ML_OK)
		{
			r = ml_doc_parse (&p, ml_str_view (text), line);
		}

		if (r == ML_OK)
		{
			r = MlDocPageVec_push (&d->pages, p);
		}

		if (r != ML_OK)
		{
			*page = i;
			ml_doc_page_free (&p);
		}

		ml_str_free (&text);
	}

	if (made)
	{
		ml_keymap_free (&own);
	}

	/* the examples are pages too (made when the pages are loaded) */
	if (r == ML_OK)
	{
		r = ml_examples_add_pages (d);

		if (r == ML_OK)
		{
			r = ml_snippets_add_page (d);
		}

		if (r != ML_OK)
		{
			*page = ml_doc_source_count;
		}
	}

	return r;
}

uint64_t ml_docs_find (const MlDocs *d, MlSlice id)
{
	uint64_t found = d->pages.len;

	for (uint64_t i = 0; i < d->pages.len && found == d->pages.len; i++)
	{
		if (ml_slice_equal (ml_str_view (d->pages.data [i].id), id))
		{
			found = i;
		}
	}

	return found;
}

/* ---- search ------------------------------------------------------------- */

void ml_doc_hits_free (MlDocHitVec *hits)
{
	for (uint64_t i = 0; i < hits->len; i++)
	{
		ml_str_free (&hits->data [i].where);
	}

	MlDocHitVec_free (hits);
}

static int32_t word_char (char c)
{
	return (
		   (c >= '0' && c <= '9')
		|| (c >= 'a' && c <= 'z')
		|| (c >= 'A' && c <= 'Z')
		|| c == '_'
		|| (uint8_t)c >= 0x80
	);
}

static char lower (char c)
{
	return ml_choose (c >= 'A' && c <= 'Z', (char)(c - 'A' + 'a'), c);
}

/* Is there a word in text equal to q, or beginning with q? q is lower case. */
static int32_t has_word (MlSlice text, MlSlice q, int32_t prefix)
{
	int32_t  found = 0;
	uint64_t pos   = 0;

	while (pos < text.len && !found)
	{
		uint64_t end = pos;

		while (end < text.len && word_char (text.data [end]))
		{
			end++;
		}

		if (end > pos && (end - pos == q.len || (prefix && end - pos > q.len)))
		{
			found = 1;

			for (uint64_t i = 0; i < q.len && found; i++)
			{
				found = lower (text.data [pos + i]) == q.data [i];
			}
		}

		pos = ml_choose (end > pos, end, pos + 1);
	}

	return found;
}

/* The beginning of a text, cut at a space, as a line to show. */
static MlResult snippet (MlStr *out, MlSlice text)
{
	uint64_t end = ml_choose (text.len > 70, 70, text.len);

	while (end < text.len && end > 0 && text.data [end] != ' ' && end > 40)
	{
		end--;
	}

	return ml_str_append (out, ml_slice_sub (text, 0, end));
}

/* The weights of the places a word can be found in. */
#define W_TITLE    100
#define W_ID       90
#define W_KEYWORDS 60
#define W_HEADING  40
#define W_SUMMARY  30
#define W_BODY     10

/* What a word scores in a page, and where in the text it is first found. */
static uint64_t score_word (
	const MlDocPage *p,
	MlSlice         q,
	int32_t         prefix,
	MlSlice         *where
)
{
	uint64_t best = 0;

	if (has_word (ml_str_view (p->title), q, prefix))
	{
		best = W_TITLE;
	}

	else if (has_word (ml_str_view (p->id), q, prefix))
	{
		best = W_ID;
	}

	else if (has_word (ml_str_view (p->keywords), q, prefix))
	{
		best = W_KEYWORDS;
	}

	for (uint64_t i = 0; i < p->blocks.len; i++)
	{
		const MlDocBlock *b = &p->blocks.data [i];

		if (has_word (ml_str_view (b->text), q, prefix))
		{
			uint64_t w
				= ml_choose (b->kind == ML_DOC_HEADING, W_HEADING, W_BODY);

			if (where->len == 0)
			{
				*where = ml_str_view (b->text);
			}

			best = ml_choose (w > best, w, best);
		}
	}

	if (best < W_SUMMARY && has_word (ml_str_view (p->summary), q, prefix))
	{
		best = W_SUMMARY;
	}

	return best;
}

/* Puts hit before the first hit that is worse (stable for equal scores). */
static MlResult insert_hit (MlDocHitVec *hits, MlDocHit hit)
{
	MlResult r   = MlDocHitVec_push (hits, hit);
	uint64_t at  = hits->len;

	if (r == ML_OK)
	{
		at = hits->len - 1;

		while (at > 0 && hits->data [at - 1].score < hit.score)
		{
			hits->data [at] = hits->data [at - 1];
			at--;
		}

		hits->data [at] = hit;
	}

	return r;
}

MlResult ml_docs_search (const MlDocs *d, MlSlice query, MlDocHitVec *hits)
{
	MlResult r = ML_OK;
	MlStr    low;
	MlSlice  words [16];
	uint64_t count = 0;
	uint64_t pos   = 0;

	ml_str_init (&low);

	for (uint64_t i = 0; i < query.len && r == ML_OK; i++)
	{
		char c = lower (query.data [i]);

		r = ml_str_append (&low, ml_slice (&c, 1));
	}

	while (pos < low.len && count < ml_count (words))
	{
		uint64_t end = pos;

		while (end < low.len && word_char (low.data [end]))
		{
			end++;
		}

		if (end > pos)
		{
			words [count] = ml_slice_sub (ml_str_view (low), pos, end - pos);
			count++;
		}

		pos = ml_choose (end > pos, end, pos + 1);
	}

	for (uint64_t i = 0; i < d->pages.len && count > 0 && r == ML_OK; i++)
	{
		const MlDocPage *p     = &d->pages.data [i];
		uint64_t         total = 0;
		int32_t          all   = 1;
		MlSlice          where = ml_slice ("", 0);

		for (uint64_t w = 0; w < count && all; w++)
		{
			uint64_t s = score_word (p, words [w], w + 1 == count, &where);

			all    = s > 0;
			total += s;
		}

		if (all)
		{
			MlDocHit hit = {
				.page  = i,
				.score = total
			};

			ml_str_init (&hit.where);
			r = snippet (
				&hit.where,
				ml_choose (where.len > 0, where, ml_str_view (p->summary))
			);

			if (r == ML_OK)
			{
				r = insert_hit (hits, hit);
			}

			if (r != ML_OK)
			{
				ml_str_free (&hit.where);
			}
		}
	}

	ml_str_free (&low);
	return r;
}

/* ---- checks ------------------------------------------------------------- */

ML_VEC_DEFINE (U64Vec, uint64_t)
ML_VEC_DEFINE (SliceVec, MlSlice)
ML_VEC_DEFINE (ByteVec, uint8_t)

/*
 * A line of the report, from the docs_check section of the texts; counts a
 * problem. The places are {id} (the page) and {detail}.
 */
static MlResult problem (
	MlStr           *report,
	uint64_t        *problems,
	const MlDocPage *p,
	const char      *code,
	MlSlice         detail
)
{
	MlTextArg args [2] = {
		{ .name = "id",     .value = ml_str_view (p->id) },
		{ .name = "detail", .value = detail }
	};

	MlResult  r = ml_text_fill (report, ml_text (code), args, 2);

	r = ml_choose (
		r == ML_OK,
		ml_str_append (report, ml_slice_cstr ("\n")),
		r
	);
	(*problems)++;
	return r;
}

/* The ids of the words in a comma separated list. */
static MlSlice list_item (MlSlice list, uint64_t *pos)
{
	uint64_t start;
	uint64_t end;

	while (*pos < list.len
		   && (list.data [*pos] == ',' || list.data [*pos] == ' '))
	{
		(*pos)++;
	}

	start = *pos;

	while (*pos < list.len && list.data [*pos] != ',' && list.data [*pos] != ' '
	)
	{
		(*pos)++;
	}

	end = *pos;
	return ml_slice_sub (list, start, end - start);
}

/*
 * The pages a page leads to (see entries and links in the text); a target that
 * does not exist is a problem when report is not NULL.
 */
static MlResult page_links (
	const MlDocs *d,
	uint64_t     page,
	U64Vec       *out,
	MlStr        *report,
	uint64_t     *problems
)
{
	MlResult         r    = ML_OK;
	const MlDocPage *p    = &d->pages.data [page];
	MlSlice          see  = ml_str_view (p->see);
	uint64_t         pos  = 0;
	MlStr            plain;
	MlDocLinkVec     links;

	ml_str_init (&plain);
	MlDocLinkVec_init (&links);

	while (pos < see.len && r == ML_OK)
	{
		MlSlice  id = list_item (see, &pos);
		uint64_t to = ml_docs_find (d, id);

		if (id.len > 0 && to < d->pages.len)
		{
			r = U64Vec_push (out, to);
		}

		else if (id.len > 0 && report != NULL)
		{
			r = problem (report, problems, p, "docs_check.see_missing", id);
		}
	}

	for (uint64_t b = 0; b < p->blocks.len && r == ML_OK; b++)
	{
		const MlDocBlock *blk = &p->blocks.data [b];

		if (
			   blk->kind == ML_DOC_PARAGRAPH
			|| blk->kind == ML_DOC_ITEM
			|| blk->kind == ML_DOC_HEADING
		)
		{
			ml_str_clear (&plain);
			links.len = 0;
			r         = ml_doc_plain (&plain, &links, ml_str_view (blk->text));

			for (uint64_t k = 0; k < links.len && r == ML_OK; k++)
			{
				uint64_t to = ml_docs_find (d, links.data [k].target);

				if (to < d->pages.len)
				{
					r = U64Vec_push (out, to);
				}

				else if (report != NULL)
				{
					r = problem (
						report,
						problems,
						p,
						"docs_check.link_missing",
						links.data [k].target
					);
				}
			}
		}
	}

	MlDocLinkVec_free (&links);
	ml_str_free (&plain);
	return r;
}

/* The domain of an example: 0 algebra, 1 physics, 2 pga2, 3 pga3. */
static MlDomain example_domain (int32_t mode)
{
	MlDomain d = ML_DOMAIN_ALGEBRA;

	switch (mode)
	{
		case 1:
		{
			d = ML_DOMAIN_PHYSICS;
		} break;

		case 2:
		{
			d = ML_DOMAIN_PGA2;
		} break;

		case 3:
		{
			d = ML_DOMAIN_PGA3;
		} break;

		default:
		{
			d = ML_DOMAIN_ALGEBRA;
		} break;
	}

	return d;
}

MlResult ml_docs_check_example (
	MlSlice  example,
	int32_t  mode,
	MlStr    *report,
	uint64_t *problems
)
{
	MlResult  r   = ML_OK;
	MlSession s;
	MlResults res;
	U64Vec    at;      /* the session line each expectation belongs to */
	SliceVec want;
	uint64_t  pos = 0;

	ml_session_init (&s);
	ml_results_init (&res);
	res.free_symbols     = 1;
	res.env.free_symbols = 1;
	U64Vec_init (&at);
	SliceVec_init (&want);

	while (pos <= example.len && r == ML_OK)
	{
		uint64_t end = pos;
		MlSlice  row;

		while (end < example.len && example.data [end] != '\n')
		{
			end++;
		}

		row = ml_slice_sub (example, pos, end - pos);

		if (row.len >= 2 && row.data [0] == '=' && row.data [1] == '>')
		{
			MlSlice expected = ml_slice_sub (row, 2, row.len - 2);

			while (expected.len > 0 && expected.data [0] == ' ')
			{
				expected = ml_slice_sub (expected, 1, expected.len - 1);
			}

			r = U64Vec_push (
				&at,
				ml_choose (s.lines.len > 0, s.lines.len - 1, 0)
			);
			r = ml_choose (r == ML_OK, SliceVec_push (&want, expected), r);
		}

		else if (row.len > 0)
		{
			r = ml_session_append (
				&s,
				ML_LINE_MATH,
				example_domain (mode),
				row
			);
		}

		pos = end + 1;
	}

	if (r == ML_OK)
	{
		r = ml_results_update (&res, &s, 0);
	}

	for (uint64_t i = 0; i < want.len && r == ML_OK; i++)
	{
		MlResultView v   = ml_results_get (&res, at.data [i]);
		MlLineView   in  = ml_session_get (&s, at.data [i]);
		MlStr        got;

		ml_str_init (&got);
		r = ml_str_append (
			&got,
			ml_choose (
				v.kind == ML_RESULT_ERROR,
				ml_slice_cstr ("! "),
				ml_slice ("", 0)
			)
		);
		r = ml_choose (r == ML_OK, ml_str_append (&got, v.text), r);

		if (r == ML_OK && !ml_slice_equal (ml_str_view (got), want.data [i]))
		{
			MlTextArg args [3] = {
				{ .name = "text", .value = in.text },
				{ .name = "got",  .value = ml_str_view (got) },
				{ .name = "want", .value = want.data [i] }
			};

			r = ml_text_fill (
				report,
				ml_text ("docs_check.example_differs"),
				args,
				3
			);
			r = ml_choose (
				r == ML_OK,
				ml_str_append (report, ml_slice_cstr ("\n")),
				r
			);
			(*problems)++;
		}

		ml_str_free (&got);
	}

	SliceVec_free (&want);
	U64Vec_free (&at);
	ml_results_free (&res);
	ml_session_free (&s);
	return r;
}

MlResult ml_docs_check (const MlDocs *d, MlStr *report, uint64_t *problems)
{
	MlResult r = ML_OK;
	U64Vec   queue;
	ByteVec seen;

	*problems = 0;
	U64Vec_init (&queue);
	ByteVec_init (&seen);

	for (uint64_t i = 0; i < d->pages.len && r == ML_OK; i++)
	{
		const MlDocPage *p = &d->pages.data [i];

		if (i < ml_doc_source_count
			&& !ml_slice_equal (
				ml_str_view (p->id),
				ml_slice_cstr (ml_doc_sources [i].name)
			))
		{
			r = problem (
				report,
				problems,
				p,
				"docs_check.id_file",
				ml_slice_cstr (ml_doc_sources [i].name)
			);
		}

		if (r == ML_OK && ml_docs_find (d, ml_str_view (p->id)) != i)
		{
			r = problem (
				report,
				problems,
				p,
				"docs_check.id_twice",
				ml_slice ("", 0)
			);
		}

		if (r == ML_OK)
		{
			queue.len = 0;
			r         = page_links (d, i, &queue, report, problems);
		}

		for (uint64_t b = 0; b < p->blocks.len && r == ML_OK; b++)
		{
			if (p->blocks.data [b].kind == ML_DOC_EXAMPLE)
			{
				MlStr    bad;
				uint64_t n = 0;

				ml_str_init (&bad);
				r = ml_docs_check_example (
					ml_str_view (p->blocks.data [b].text),
					p->blocks.data [b].level,
					&bad,
					&n
				);

				if (r == ML_OK && n > 0)
				{
					r = problem (
						report,
						problems,
						p,
						"docs_check.example_bad",
						ml_str_view (bad)
					);
					(*problems) += n - 1;
				}

				ml_str_free (&bad);
			}
		}
	}

	/* every page can be reached from the index by following links */
	if (r == ML_OK && d->pages.len > 0)
	{
		uint64_t home = ml_docs_find (d, ml_slice_cstr ("index"));

		if (home >= d->pages.len)
		{
			r = ml_text_fill (
				report,
				ml_text ("docs_check.no_index"),
				NULL,
				0
			);
			r = ml_choose (
				r == ML_OK,
				ml_str_append (report, ml_slice_cstr ("\n")),
				r
			);
			(*problems)++;
		}

		else
		{
			uint64_t head = 0;

			for (uint64_t i = 0; i < d->pages.len && r == ML_OK; i++)
			{
				r = ByteVec_push (&seen, 0);
			}

			queue.len = 0;

			if (r == ML_OK)
			{
				seen.data [home] = 1;
				r                = U64Vec_push (&queue, home);
			}

			while (head < queue.len && r == ML_OK)
			{
				U64Vec next;

				U64Vec_init (&next);
				r = page_links (d, queue.data [head], &next, NULL, problems);

				for (uint64_t k = 0; k < next.len && r == ML_OK; k++)
				{
					if (!seen.data [next.data [k]])
					{
						seen.data [next.data [k]] = 1;
						r = U64Vec_push (&queue, next.data [k]);
					}
				}

				U64Vec_free (&next);
				head++;
			}

			for (uint64_t i = 0; i < d->pages.len && r == ML_OK; i++)
			{
				if (!seen.data [i])
				{
					r = problem (
						report,
						problems,
						&d->pages.data [i],
						"docs_check.unreachable",
						ml_slice ("", 0)
					);
				}
			}
		}
	}

	ByteVec_free (&seen);
	U64Vec_free (&queue);
	return r;
}
