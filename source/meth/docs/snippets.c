#include <meth/docs/snippets.h>

#include <meth/docs/examples.h>
#include <meth/platform/config.h>
#include <meth/platform/fs.h>
#include <meth/platform/path.h>
#include <meth/session/mlog.h>

#define SNIPPET_SUFFIX ".meth"

uint64_t ml_snippet_count (void)
{
	return ml_snippet_source_count;
}

MlSlice ml_snippet_name (uint64_t i)
{
	return ml_slice_cstr (
		ml_choose (i < ml_snippet_source_count, ml_snippet_sources [i].name, "")
	);
}

MlSlice ml_snippet_text (uint64_t i)
{
	return ml_slice_cstr (
		ml_choose (i < ml_snippet_source_count, ml_snippet_sources [i].text, "")
	);
}

int32_t ml_snippet_name_ok (MlSlice name)
{
	int32_t ok = name.len > 0 && name.len <= 40;

	for (uint64_t i = 0; i < name.len; i++)
	{
		char c = name.data [i];

		ok = (
			   ok
			&& (
				   (c >= 'a' && c <= 'z')
				|| (c >= '0' && c <= '9')
				|| c == '-'
				|| c == '_'
			)
		);
	}

	return ok;
}

/* The folder of the user's snippets. */
static MlResult snippets_dir (MlStr *out)
{
	MlStr    config;
	MlResult r;

	ml_str_init (&config);
	r = ml_config_dir (&config);

	if (r == ML_OK)
	{
		r = ml_path_join (
			out,
			ml_str_view (config),
			ml_slice_cstr ("snippets")
		);
	}

	ml_str_free (&config);
	return r;
}

/* Appends the file text of the user's snippet; found is 0 when none. */
static MlSnippetFound user_snippet (MlStr *out, MlSlice name)
{
	MlSnippetFound f = {
		.result = ML_OK,
		.found  = 0
	};

	MlStr dir;
	MlStr file;
	MlStr path;

	ml_str_init (&dir);
	ml_str_init (&file);
	ml_str_init (&path);

	if (snippets_dir (&dir) == ML_OK)
	{
		f.result = ml_str_appendf (
			&file,
			"%.*s" SNIPPET_SUFFIX,
			(int)name.len,
			name.data
		);

		if (f.result == ML_OK)
		{
			f.result = ml_path_join (
				&path,
				ml_str_view (dir),
				ml_str_view (file)
			);
		}

		if (f.result == ML_OK && ml_fs_exists (ml_str_view (path)))
		{
			f.result = ml_fs_read_file (out, ml_str_view (path));
			f.found  = f.result == ML_OK;

			/* a file that cannot be read is no snippet */
			f.result = ml_choose (f.result == ML_ERR_NOMEM, f.result, ML_OK);
		}
	}

	ml_str_free (&path);
	ml_str_free (&file);
	ml_str_free (&dir);
	return f;
}

MlSnippetFound ml_snippet_find (MlStr *out, MlSlice name)
{
	MlSnippetFound f = {
		.result = ML_OK,
		.found  = 0
	};

	if (ml_snippet_name_ok (name))
	{
		f = user_snippet (out, name);

		for (
			uint64_t i = 0;
			i < ml_snippet_count () && f.result == ML_OK && !f.found;
			i++
		)
		{
			if (ml_slice_equal (name, ml_snippet_name (i)))
			{
				f.result = ml_str_append (out, ml_snippet_text (i));
				f.found  = f.result == ML_OK;
			}
		}
	}

	return f;
}

MlResult ml_snippet_summary (MlStr *out, MlSlice text)
{
	MlSession s;
	MlResult  r;

	ml_session_init (&s);
	r = ml_mlog_parse (&s, text);

	for (uint64_t i = 0; i < s.lines.len && r == ML_OK && out->len == 0; i++)
	{
		MlLineView v = ml_session_get (&s, i);

		if (v.kind == ML_LINE_NOTE)
		{
			r = ml_str_append (out, v.text);
		}
	}

	ml_session_free (&s);
	return r;
}

MlResult ml_snippet_lines (MlSession *out, MlSlice text)
{
	MlSession s;
	MlResult  r;
	int32_t   skipped = 0;

	ml_session_init (&s);
	r = ml_mlog_parse (&s, text);

	for (uint64_t i = 0; i < s.lines.len && r == ML_OK; i++)
	{
		MlLineView v = ml_session_get (&s, i);

		if (!skipped && v.kind == ML_LINE_NOTE)
		{
			skipped = 1;
		}

		else
		{
			r = ml_session_append (out, v.kind, v.domain, v.text);
		}
	}

	ml_session_free (&s);
	return r;
}

/* One list item of the page: the name and the summary of a snippet text. */
static MlResult page_item (MlStr *out, MlSlice name, MlSlice text)
{
	MlStr    summary;
	MlResult r;

	ml_str_init (&summary);
	r = ml_snippet_summary (&summary, text);

	if (r == ML_OK)
	{
		r = ml_str_appendf (
			out,
			"- `%.*s`: %s\n",
			(int)name.len,
			name.data,
			ml_str_cstr (summary)
		);
	}

	ml_str_free (&summary);
	return r;
}

/* Adds the items of the user's snippets, a section if there are any. */
static MlResult page_user (MlStr *out)
{
	MlResult r = ML_OK;
	MlStr    dir;
	MlStr    names;
	uint64_t pos = 0;
	int32_t  any = 0;

	ml_str_init (&dir);
	ml_str_init (&names);

	if (snippets_dir (&dir) == ML_OK)
	{
		r = ml_fs_list (
			&names,
			ml_str_view (dir),
			ml_slice_cstr (SNIPPET_SUFFIX)
		);
	}

	while (r == ML_OK && pos < names.len)
	{
		MlSlice  all = ml_str_view (names);
		uint64_t end = pos;
		MlSlice  name;

		while (end < all.len && all.data [end] != '\n')
		{
			end++;
		}

		name = ml_slice_sub (all, pos, end - pos - (sizeof SNIPPET_SUFFIX - 1));

		if (end - pos > sizeof SNIPPET_SUFFIX - 1 && ml_snippet_name_ok (name))
		{
			MlStr          text;
			MlSnippetFound f;

			ml_str_init (&text);
			f = ml_snippet_find (&text, name);
			r = f.result;

			if (r == ML_OK && f.found)
			{
				if (!any)
				{
					r   = ml_str_append (out, ml_slice_cstr ("\n# Yours\n"));
					any = 1;
				}

				if (r == ML_OK)
				{
					r = page_item (out, name, ml_str_view (text));
				}
			}

			ml_str_free (&text);
		}

		pos = end + 1;
	}

	ml_str_free (&names);
	ml_str_free (&dir);
	return r;
}

static MlResult page_text (MlStr *out, int32_t with_user)
{
	MlResult r = ml_str_append (
		out,
		ml_slice_cstr (
			"id: snippets\n"
			"title: Snippets\n"
			"summary: Small pieces of a session to put in and change.\n"
			"keywords: snippets, templates, insert, boilerplate\n"
			"see: examples, commands\n"
			"\n"
			"`:snippet NAME` puts the lines of a snippet into your session "
			"after the cursor line, as one change that undo takes back. "
			"They are a start: change the numbers and names. Your own "
			"snippets are session files named `NAME.meth` in the folder "
			"`snippets` of the settings folder (see [settings](settings)); "
			"the first note of the file is its summary, and a file hides "
			"the built in snippet of the same name. The names start with the "
			"area they belong to, as the names of the "
			"[examples](examples) do.\n"
		)
	);

	/* the built in ones, a heading for each area (the last one: no area) */
	for (uint64_t k = 0; k <= ml_area_count () && r == ML_OK; k++)
	{
		int32_t any = 0;

		for (uint64_t i = 0; i < ml_snippet_count () && r == ML_OK; i++)
		{
			if (ml_area_find (ml_snippet_name (i)) == k)
			{
				if (!any)
				{
					r = ml_str_appendf (
						out,
						"\n# %.*s\n",
						(int)ml_choose (
							k < ml_area_count (),
							ml_area_title (k).len,
							5
						),
						ml_choose (
							k < ml_area_count (),
							ml_area_title (k).data,
							"Other"
						)
					);
					any = 1;
				}

				if (r == ML_OK)
				{
					r = page_item (
						out,
						ml_snippet_name (i),
						ml_snippet_text (i)
					);
				}
			}
		}
	}

	if (r == ML_OK && with_user)
	{
		r = page_user (out);
	}

	return r;
}

MlResult ml_snippets_page (MlStr *out)
{
	return page_text (out, 1);
}

MlResult ml_snippets_add_page (MlDocs *d)
{
	MlResult  r;
	MlStr     text;
	MlDocPage p;
	uint64_t  line = 0;

	ml_str_init (&text);
	ml_doc_page_init (&p);
	r = page_text (&text, 0);

	if (r == ML_OK)
	{
		r = ml_doc_parse (&p, ml_str_view (text), &line);
	}

	if (r == ML_OK)
	{
		r = MlDocPageVec_push (&d->pages, p);
	}

	if (r != ML_OK)
	{
		ml_doc_page_free (&p);
	}

	ml_str_free (&text);
	return r;
}
