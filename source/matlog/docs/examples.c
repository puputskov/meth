#include <matlog/docs/examples.h>

#include <matlog/session/mlog.h>
#include <matlog/session/results.h>

/* The groups of the table of contents, in the order they are shown. */
typedef struct Area
{
	const char *prefix;
	const char *title;
} Area;

static const Area areas [] = {
	{ .prefix = "basics",   .title = "Basics" },
	{ .prefix = "algebra",  .title = "Algebra" },
	{ .prefix = "calculus", .title = "Calculus" },
	{ .prefix = "linalg",   .title = "Linear algebra" },
	{ .prefix = "units",    .title = "Units" },
	{ .prefix = "stats",    .title = "Statistics" },
	{ .prefix = "complex",  .title = "Complex numbers" },
	{ .prefix = "quat",     .title = "Quaternions" },
	{ .prefix = "logic",    .title = "Boolean logic" },
	{ .prefix = "lambda",   .title = "Lambda calculus" },
	{ .prefix = "robotics", .title = "Robotics" }
};

uint64_t ml_example_count (void)
{
	return ml_example_source_count;
}

MlSlice ml_example_name (uint64_t i)
{
	return ml_slice_cstr (
		ml_choose (i < ml_example_source_count, ml_example_sources [i].name, "")
	);
}

MlSlice ml_example_text (uint64_t i)
{
	return ml_slice_cstr (
		ml_choose (i < ml_example_source_count, ml_example_sources [i].text, "")
	);
}

uint64_t ml_example_find (MlSlice name)
{
	uint64_t found = ml_example_source_count;

	for (
		uint64_t i = 0;
		i < ml_example_source_count && found == ml_example_source_count;
		i++
	)
	{
		if (ml_slice_equal (name, ml_example_name (i)))
		{
			found = i;
		}
	}

	return found;
}

/* The word of the name before the first dash. */
static MlSlice prefix_of (MlSlice name)
{
	uint64_t n = 0;

	while (n < name.len && name.data [n] != '-')
	{
		n++;
	}

	return ml_slice_sub (name, 0, n);
}

MlSlice ml_example_area (uint64_t i)
{
	MlSlice prefix = prefix_of (ml_example_name (i));
	MlSlice found  = prefix;

	for (uint64_t k = 0; k < ml_count (areas); k++)
	{
		if (ml_slice_equal (prefix, ml_slice_cstr (areas [k].prefix)))
		{
			found = ml_slice_cstr (areas [k].title);
		}
	}

	return found;
}

/* The text of the n-th note of the session (0 based), appended to out. */
static MlResult nth_note (MlStr *out, const MlSession *s, uint64_t n)
{
	MlResult r    = ML_OK;
	uint64_t seen = 0;

	for (uint64_t i = 0; i < s->lines.len && r == ML_OK; i++)
	{
		MlLineView v = ml_session_get (s, i);

		if (v.kind == ML_LINE_NOTE)
		{
			if (seen == n)
			{
				r = ml_str_append (out, v.text);
			}

			seen++;
		}
	}

	return r;
}

static MlResult note_of (MlStr *out, uint64_t i, uint64_t n)
{
	MlSession s;
	MlResult  r;

	ml_session_init (&s);
	r = ml_mlog_parse (&s, ml_example_text (i));

	if (r == ML_OK)
	{
		r = nth_note (out, &s, n);
	}

	ml_session_free (&s);
	return r;
}

MlResult ml_example_title (MlStr *out, uint64_t i)
{
	return note_of (out, i, 0);
}

MlResult ml_example_summary (MlStr *out, uint64_t i)
{
	return note_of (out, i, 1);
}

/* Appends the rows of text, each indented by the given prefix. */
static MlResult append_rows (MlStr *out, MlSlice text, const char *indent)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	while (r == ML_OK && pos < text.len)
	{
		uint64_t end = pos;

		while (end < text.len && text.data [end] != '\n')
		{
			end++;
		}

		r   = ml_str_appendf (
			out,
			"%s%.*s\n",
			indent,
			(int)(end - pos),
			text.data + pos
		);
		pos = end + 1;
	}

	return r;
}

/*
 * A math line and its result as lines of a code block: the line, then the
 * value after an equals sign (a matrix as its typeset rows), or the message
 * of the error.
 */
static MlResult append_math (
	MlStr        *code,
	MlLineView   line,
	MlResultView res
)
{
	MlResult r = append_rows (code, line.text, "");

	if (r == ML_OK && res.kind == ML_RESULT_VALUE && res.block.len > 0)
	{
		r = append_rows (code, res.block, "  ");
	}

	else if (r == ML_OK && res.kind == ML_RESULT_VALUE)
	{
		r = ml_str_appendf (
			code,
			"  = %.*s\n",
			(int)res.text.len,
			res.text.data
		);
	}

	else if (r == ML_OK && res.kind == ML_RESULT_ERROR)
	{
		r = ml_str_appendf (
			code,
			"  error: %.*s\n",
			(int)res.text.len,
			res.text.data
		);
	}

	return r;
}

/* Writes the code gathered so far as a block of the page, and clears it. */
static MlResult flush_code (MlStr *page, MlStr *code)
{
	MlResult r = ML_OK;

	if (code->len > 0)
	{
		r = ml_str_appendf (page, "```code\n%s```\n\n", ml_str_cstr (*code));
		ml_str_clear (code);
	}

	return r;
}

/* The words of the name, after the area, as keywords. */
static MlResult name_words (MlStr *out, MlSlice name)
{
	MlResult r = ML_OK;

	for (uint64_t i = prefix_of (name).len; i < name.len && r == ML_OK; i++)
	{
		r = ml_str_append (
			out,
			ml_choose (
				name.data [i] == '-',
				ml_slice_cstr (", "),
				ml_slice (name.data + i, 1)
			)
		);
	}

	return r;
}

MlResult ml_example_page (MlStr *out, uint64_t i)
{
	MlResult  r = ML_OK;
	MlSession s;
	MlResults res;
	MlStr     title;
	MlStr     summary;
	MlStr     code;
	MlStr     words;
	MlDomain  domain = ML_DOMAIN_ALGEBRA;
	uint64_t  note   = 0;

	ml_session_init (&s);
	ml_results_init (&res);
	ml_str_init (&title);
	ml_str_init (&summary);
	ml_str_init (&code);
	ml_str_init (&words);
	r = ml_mlog_parse (&s, ml_example_text (i));

	if (r == ML_OK)
	{
		r = ml_results_update (&res, &s, 0);
	}

	if (r == ML_OK)
	{
		r = nth_note (&title, &s, 0);
	}

	if (r == ML_OK)
	{
		r = nth_note (&summary, &s, 1);
	}

	if (r == ML_OK)
	{
		r = name_words (&words, ml_example_name (i));
	}

	if (r == ML_OK)
	{
		MlSlice area = ml_example_area (i);

		r = ml_str_appendf (
			out,
			"id: ex-%.*s\n"
			"title: %s\n"
			"summary: %s\n"
			"keywords: example, %.*s%s\n"
			"see: examples\n"
			"\n",
			(int)ml_example_name (i).len,
			ml_example_name (i).data,
			ml_str_cstr (title),
			ml_str_cstr (summary),
			(int)area.len,
			area.data,
			ml_str_cstr (words)
		);
	}

	for (uint64_t k = 0; k < s.lines.len && r == ML_OK; k++)
	{
		MlLineView line = ml_session_get (&s, k);

		if (line.kind == ML_LINE_NOTE)
		{
			/* the title and the summary are in the header */
			if (note >= 2)
			{
				r = flush_code (out, &code);

				if (r == ML_OK)
				{
					r = ml_str_appendf (
						out,
						"%.*s\n\n",
						(int)line.text.len,
						line.text.data
					);
				}
			}

			note++;
		}

		else if (line.kind == ML_LINE_MATH)
		{
			if (line.domain != domain)
			{
				domain = line.domain;
				r      = ml_str_appendf (
					&code,
					"-- %s\n",
					ml_domain_name (domain)
				);
			}

			if (r == ML_OK)
			{
				r = append_math (&code, line, ml_results_get (&res, k));
			}
		}
	}

	if (r == ML_OK)
	{
		r = flush_code (out, &code);
	}

	if (r == ML_OK)
	{
		r = ml_str_appendf (
			out,
			"Put these lines into your own session with `:example %.*s`, "
			"or print the file with `matlog examples %.*s`.\n",
			(int)ml_example_name (i).len,
			ml_example_name (i).data,
			(int)ml_example_name (i).len,
			ml_example_name (i).data
		);
	}

	ml_str_free (&words);
	ml_str_free (&code);
	ml_str_free (&summary);
	ml_str_free (&title);
	ml_results_free (&res);
	ml_session_free (&s);
	return r;
}

MlResult ml_examples_contents (MlStr *out)
{
	MlResult r = ml_str_append (
		out,
		ml_slice_cstr (
			"id: examples\n"
			"title: Examples\n"
			"summary: Worked problems to read, run and copy into your "
			"session.\n"
			"keywords: examples, problems, how to, worked, solutions\n"
			"see: index\n"
			"\n"
			"Each example is a short session with notes that explain the "
			"steps; the results on the page are the ones the program gives "
			"now. `:example NAME` puts the lines of one into your session "
			"after the cursor, and `matlog examples NAME` prints it as a "
			".mlog file you can save.\n"
			"\n"
		)
	);

	for (uint64_t a = 0; a < ml_count (areas) && r == ML_OK; a++)
	{
		int32_t any = 0;

		for (uint64_t i = 0; i < ml_example_count () && r == ML_OK; i++)
		{
			if (
				ml_slice_equal (
					prefix_of (ml_example_name (i)),
					ml_slice_cstr (areas [a].prefix)
				)
			)
			{
				MlStr title;
				MlStr summary;

				ml_str_init (&title);
				ml_str_init (&summary);
				r = ml_example_title (&title, i);

				if (r == ML_OK)
				{
					r = ml_example_summary (&summary, i);
				}

				if (r == ML_OK && !any)
				{
					r   = ml_str_appendf (out, "# %s\n", areas [a].title);
					any = 1;
				}

				if (r == ML_OK)
				{
					r = ml_str_appendf (
						out,
						"- [%s](ex-%.*s): %s\n",
						ml_str_cstr (title),
						(int)ml_example_name (i).len,
						ml_example_name (i).data,
						ml_str_cstr (summary)
					);
				}

				ml_str_free (&summary);
				ml_str_free (&title);
			}
		}

		if (any && r == ML_OK)
		{
			r = ml_str_append (out, ml_slice_cstr ("\n"));
		}
	}

	return r;
}

/* Parses a page text and adds it to d. */
static MlResult add_page (MlDocs *d, const MlStr *text)
{
	MlResult  r    = ML_OK;
	MlDocPage p;
	uint64_t  line = 0;

	ml_doc_page_init (&p);
	r = ml_doc_parse (&p, ml_str_view (*text), &line);

	if (r == ML_OK)
	{
		r = MlDocPageVec_push (&d->pages, p);
	}

	if (r != ML_OK)
	{
		ml_doc_page_free (&p);
	}

	return r;
}

MlResult ml_examples_add_pages (MlDocs *d)
{
	MlResult r = ML_OK;
	MlStr    text;

	ml_str_init (&text);

	if (ml_example_count () > 0)
	{
		r = ml_examples_contents (&text);

		if (r == ML_OK)
		{
			r = add_page (d, &text);
		}
	}

	for (uint64_t i = 0; i < ml_example_count () && r == ML_OK; i++)
	{
		ml_str_clear (&text);
		r = ml_example_page (&text, i);

		if (r == ML_OK)
		{
			r = add_page (d, &text);
		}
	}

	ml_str_free (&text);
	return r;
}
