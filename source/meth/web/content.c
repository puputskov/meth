#include <meth/web/web.h>

#include "internal.h"

#include <stdio.h>

#include <meth/docs/examples.h>
#include <meth/docs/snippets.h>
#include <meth/tutorial/lessons.h>
#include <meth/ui/theme.h>

/*
 * The content of the program as data for the page: the documentation, the
 * examples and snippets, the lessons of the tutorial and the themes (see
 * meth/web/web.h).
 */

/* ---- documentation ----------------------------------------------------- */

static MlResult load_docs (MlWeb *w)
{
	MlResult r = ML_OK;

	if (!w->docs_loaded)
	{
		uint64_t page;
		uint64_t line;

		r = ml_docs_load (&w->docs, NULL, &page, &line);
		w->docs_loaded = r == ML_OK;
	}

	return r;
}

MlResult ml_web_docs (MlWeb *w)
{
	MlResult r = load_docs (w);

	ml_str_clear (&w->out);
	r = lit (r, &w->out, "[");

	for (uint64_t i = 0; r == ML_OK && i < w->docs.pages.len; i++)
	{
		r = lit (r, &w->out, ml_choose (i > 0, ",", ""));
		r = ml_choose (
			r == ML_OK,
			ml_doc_page_json (&w->out, &w->docs.pages.data [i], 0),
			r
		);
	}

	return lit (r, &w->out, "]");
}

MlResult ml_web_doc (MlWeb *w, MlSlice id)
{
	MlResult r  = load_docs (w);
	uint64_t at = ml_docs_find (&w->docs, id);

	ml_str_clear (&w->out);

	if (r == ML_OK && at >= w->docs.pages.len)
	{
		r = ML_ERR_INVALID;
	}

	else if (r == ML_OK)
	{
		r = ml_doc_page_json (&w->out, &w->docs.pages.data [at], 1);
	}

	return r;
}

MlResult ml_web_docs_search (MlWeb *w, MlSlice query)
{
	MlResult    r = load_docs (w);
	MlDocHitVec hits;

	ml_str_clear (&w->out);
	MlDocHitVec_init (&hits);

	if (r == ML_OK)
	{
		r = ml_docs_search (&w->docs, query, &hits);
	}

	r = lit (r, &w->out, "[");

	for (uint64_t i = 0; r == ML_OK && i < hits.len; i++)
	{
		const MlDocPage *p = &w->docs.pages.data [hits.data [i].page];

		r = lit (r, &w->out, ml_choose (i > 0, ",{\"score\":", "{\"score\":"));
		r = num (r, &w->out, hits.data [i].score);
		r = lit (r, &w->out, ",\"id\":");
		r = str (r, &w->out, ml_str_view (p->id));
		r = lit (r, &w->out, ",\"title\":");
		r = str (r, &w->out, ml_str_view (p->title));
		r = lit (r, &w->out, ",\"where\":");
		r = str (r, &w->out, ml_str_view (hits.data [i].where));
		r = lit (r, &w->out, "}");
	}

	ml_doc_hits_free (&hits);
	return lit (r, &w->out, "]");
}

/* ---- examples and snippets --------------------------------------------- */

MlResult ml_web_examples (MlWeb *w)
{
	MlResult r = ML_OK;

	ml_str_clear (&w->out);
	r = lit (r, &w->out, "[");

	for (uint64_t i = 0; r == ML_OK && i < ml_example_count (); i++)
	{
		MlStr title;
		MlStr summary;

		ml_str_init (&title);
		ml_str_init (&summary);
		r = ml_example_title (&title, i);
		r = ml_choose (r == ML_OK, ml_example_summary (&summary, i), r);
		r = lit (r, &w->out, ml_choose (i > 0, ",{\"name\":", "{\"name\":"));
		r = str (r, &w->out, ml_example_name (i));
		r = lit (r, &w->out, ",\"title\":");
		r = str (r, &w->out, ml_str_view (title));
		r = lit (r, &w->out, ",\"summary\":");
		r = str (r, &w->out, ml_str_view (summary));
		r = lit (r, &w->out, ",\"area\":");
		r = str (r, &w->out, ml_example_area (i));
		r = lit (r, &w->out, "}");
		ml_str_free (&title);
		ml_str_free (&summary);
	}

	return lit (r, &w->out, "]");
}

MlResult ml_web_example (MlWeb *w, MlSlice name)
{
	uint64_t at = ml_example_find (name);
	MlResult r  = ML_OK;

	ml_str_clear (&w->out);

	if (at >= ml_example_count ())
	{
		r = ML_ERR_INVALID;
	}

	else
	{
		r = ml_str_append (&w->out, ml_example_text (at));
	}

	return r;
}

MlResult ml_web_snippets (MlWeb *w)
{
	MlResult r = ML_OK;

	ml_str_clear (&w->out);
	r = lit (r, &w->out, "[");

	for (uint64_t i = 0; r == ML_OK && i < ml_snippet_count (); i++)
	{
		MlStr summary;

		ml_str_init (&summary);
		r = ml_snippet_summary (&summary, ml_snippet_text (i));
		r = lit (r, &w->out, ml_choose (i > 0, ",{\"name\":", "{\"name\":"));
		r = str (r, &w->out, ml_snippet_name (i));
		r = lit (r, &w->out, ",\"summary\":");
		r = str (r, &w->out, ml_str_view (summary));
		r = lit (r, &w->out, "}");
		ml_str_free (&summary);
	}

	return lit (r, &w->out, "]");
}

MlResult ml_web_snippet (MlWeb *w, MlSlice name)
{
	MlSnippetFound f;

	ml_str_clear (&w->out);
	f = ml_snippet_find (&w->out, name);
	return ml_choose (
		f.result == ML_OK && !f.found,
		ML_ERR_INVALID,
		f.result
	);
}

/* ---- the tutorial ------------------------------------------------------ */

static MlResult add_step (MlResult r, MlStr *o, const MlStep *st)
{
	r = lit (r, o, "{\"text\":");
	r = str (r, o, ml_slice_cstr (st->text));
	r = lit (r, o, ",\"check\":");
	r = num (r, o, (uint64_t)st->check);
	r = lit (r, o, ",\"arg\":");
	r = num (r, o, (uint64_t)st->arg);
	r = lit (r, o, ",\"param\":");
	r = str (r, o, ml_slice_cstr (ml_choose (st->param, st->param, "")));
	r = lit (r, o, ",\"param2\":");
	r = str (r, o, ml_slice_cstr (ml_choose (st->param2, st->param2, "")));
	r = lit (r, o, ",\"hint\":");
	r = str (r, o, ml_slice_cstr (ml_choose (st->hint, st->hint, "")));
	r = lit (r, o, ",\"answer\":");
	r = str (r, o, ml_slice_cstr (ml_choose (st->answer, st->answer, "")));
	return lit (r, o, "}");
}

MlResult ml_web_lessons (MlWeb *w)
{
	MlResult r = ML_OK;

	ml_str_clear (&w->out);
	r = lit (r, &w->out, "[");

	for (uint64_t i = 0; r == ML_OK && i < ml_lesson_count (); i++)
	{
		MlLesson l = ml_lesson_at (i);

		r = lit (r, &w->out, ml_choose (i > 0, ",{\"id\":", "{\"id\":"));
		r = str (r, &w->out, ml_slice_cstr (l.id));
		r = lit (r, &w->out, ",\"title\":");
		r = str (r, &w->out, ml_slice_cstr (l.title));
		r = lit (r, &w->out, ",\"stage\":");
		r = num (r, &w->out, (uint64_t)l.stage);
		r = lit (r, &w->out, ",\"steps\":[");

		for (uint64_t k = 0; k < l.step_count; k++)
		{
			r = lit (r, &w->out, ml_choose (k > 0, ",", ""));
			r = add_step (r, &w->out, &l.steps [k]);
		}

		r = lit (r, &w->out, "]}");
	}

	return lit (r, &w->out, "]");
}

/* ---- themes ------------------------------------------------------------ */

/* "#rrggbb", or null for the colour of the terminal (here: of the page). */
static MlResult add_color (MlResult r, MlStr *o, MlColor c)
{
	char hex [8];

	if (c.set)
	{
		snprintf (hex, sizeof hex, "#%02x%02x%02x", c.r, c.g, c.b);
		r = lit (r, o, "\"");
		r = lit (r, o, hex);
		r = lit (r, o, "\"");
	}

	else
	{
		r = lit (r, o, "null");
	}

	return r;
}

static MlResult add_theme (MlResult r, MlStr *o, const MlTheme *t, uint64_t bad)
{
	r = lit (r, o, "{\"name\":");
	r = str (r, o, ml_slice_cstr (t->name));
	r = lit (r, o, ",\"background\":");
	r = add_color (r, o, t->background);
	r = lit (r, o, ",\"paint\":");
	r = flag (r, o, t->paint);
	r = lit (r, o, ",\"bad\":");
	r = num (r, o, bad);
	r = lit (r, o, ",\"classes\":{");

	for (uint64_t c = 0; c < ML_CLASS_COUNT; c++)
	{
		const MlStyle *st = &t->styles [c];

		r = lit (r, o, ml_choose (c > 0, ",\"", "\""));
		r = lit (r, o, ml_class_name ((MlClass)c));
		r = lit (r, o, "\":{\"fg\":");
		r = add_color (r, o, st->fg);
		r = lit (r, o, ",\"bg\":");
		r = add_color (r, o, st->bg);
		r = lit (r, o, ",\"bold\":");
		r = flag (r, o, st->bold);
		r = lit (r, o, ",\"dim\":");
		r = flag (r, o, st->dim);
		r = lit (r, o, ",\"underline\":");
		r = flag (r, o, st->underline);
		r = lit (r, o, ",\"reverse\":");
		r = flag (r, o, st->reverse);
		r = lit (r, o, "}");
	}

	return lit (r, o, "}}");
}

MlResult ml_web_themes (MlWeb *w)
{
	MlResult r = ML_OK;

	ml_str_clear (&w->out);
	r = lit (r, &w->out, "[");

	for (uint64_t i = 0; r == ML_OK && i < ml_theme_builtin_count (); i++)
	{
		MlTheme t = ml_theme_default ();

		r = ml_theme_builtin (&t, ml_slice_cstr (ml_theme_builtin_name (i)));
		r = lit (r, &w->out, ml_choose (i > 0, ",", ""));
		r = add_theme (r, &w->out, &t, 0);
	}

	return lit (r, &w->out, "]");
}

MlResult ml_web_theme_text (MlWeb *w, MlSlice text)
{
	MlTheme  t   = ml_theme_default ();
	uint64_t bad = 0;
	MlResult r;

	ml_str_clear (&w->out);
	r = ml_theme_apply_text (&t, text, NULL, &bad);
	return add_theme (r, &w->out, &t, bad);
}
