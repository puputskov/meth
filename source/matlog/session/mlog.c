#include <matlog/session/mlog.h>

#include <matlog/core/utf8.h>
#include <matlog/platform/fs.h>

static int32_t starts_with (MlSlice s, const char *prefix)
{
	MlSlice p = ml_slice_cstr (prefix);

	return s.len >= p.len && ml_slice_equal (ml_slice_sub (s, 0, p.len), p);
}

static int32_t is_blank (MlSlice s)
{
	int32_t blank = 1;

	for (uint64_t i = 0; i < s.len && blank; i++)
	{
		blank = s.data [i] == ' ' || s.data [i] == '\t';
	}

	return blank;
}

/* Index of the first space in s, s.len when there is none. */
static uint64_t find_space (MlSlice s)
{
	uint64_t i = 0;

	while (i < s.len && s.data [i] != ' ')
	{
		i++;
	}

	return i;
}

/* Decides what a single line (without its line ending) means. */
static MlLineView classify (MlSlice line)
{
	MlLineView view = {
		.kind   = ML_LINE_UNKNOWN,
		.domain = ML_DOMAIN_ALGEBRA,
		.text   = line
	};

	if (!ml_utf8_valid (line))
	{
		view.kind = ML_LINE_UNKNOWN;
	}

	else if (is_blank (line))
	{
		view.kind = ML_LINE_BLANK;
		view.text = ml_slice ("", 0);
	}

	else if (line.data [0] == '#')
	{
		/* "# text": the one space after the marker is not part of the text */
		uint64_t skip = ml_choose (
			(line.len > 1 && line.data [1] == ' '),
			2,
			1
		);

		view.kind = ML_LINE_NOTE;
		view.text = ml_slice_sub (line, skip, line.len - skip);
	}

	else if (starts_with (line, "m:"))
	{
		uint64_t       space = find_space (line);
		MlDomainResult d     = ml_domain_from_name (
			ml_slice_sub (line, 2, space - 2)
		);

		if (d.result == ML_OK)
		{
			uint64_t skip = ml_choose (
				space < line.len,
				space + 1,
				line.len
			);

			view.kind   = ML_LINE_MATH;
			view.domain = d.domain;
			view.text   = ml_slice_sub (line, skip, line.len - skip);
		}
	}

	else if (
		   starts_with (line, "include \"")
		&& line.len >= 10
		&& line.data [line.len - 1] == '"'
	)
	{
		view.kind = ML_LINE_INCLUDE;
		view.text = ml_slice_sub (line, 9, line.len - 10);
	}

	return view;
}

MlResult ml_mlog_parse (MlSession *s, MlSlice text)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	/* byte order marks, as many as there are: writing never adds one */
	while (
		starts_with (ml_slice_sub (text, pos, text.len - pos), "\xEF\xBB\xBF")
	)
	{
		pos += 3;
	}

	while (r == ML_OK && pos < text.len)
	{
		uint64_t   end = pos;
		MlSlice    line;
		MlLineView view;

		while (end < text.len && text.data [end] != '\n')
		{
			end++;
		}

		line = ml_slice_sub (text, pos, end - pos);

		while (line.len > 0 && line.data [line.len - 1] == '\r')
		{
			line.len--;
		}

		view = classify (line);
		r    = ml_session_append (s, view.kind, view.domain, view.text);
		pos  = end + 1;
	}

	return r;
}

static MlResult format_line (MlStr *out, MlLineView v)
{
	MlResult r = ML_OK;

	if (v.kind == ML_LINE_NOTE)
	{
		r = ml_str_append (
			out,
			ml_slice_cstr (
				ml_choose (
					v.text.len > 0,
					"# ",
					"#"
				)
			)
		);
		r = ml_choose (
			r == ML_OK,
			ml_str_append (out, v.text),
			r
		);
	}

	else if (v.kind == ML_LINE_MATH)
	{
		r = ml_str_append (out, ml_slice_cstr ("m:"));
		r = ml_choose (
			r == ML_OK,
			ml_str_append (out, ml_slice_cstr (ml_domain_name (v.domain))),
			r
		);

		if (r == ML_OK && v.text.len > 0)
		{
			r = ml_str_append (out, ml_slice_cstr (" "));
			r = ml_choose (
				r == ML_OK,
				ml_str_append (out, v.text),
				r
			);
		}
	}

	else if (v.kind == ML_LINE_INCLUDE)
	{
		r = ml_str_append (out, ml_slice_cstr ("include \""));
		r = ml_choose (
			r == ML_OK,
			ml_str_append (out, v.text),
			r
		);
		r = ml_choose (
			r == ML_OK,
			ml_str_append (out, ml_slice_cstr ("\"")),
			r
		);
	}

	else if (v.kind == ML_LINE_UNKNOWN)
	{
		r = ml_str_append (out, v.text);
	}

	/* blank lines are written as nothing */

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr ("\n"));
	}

	return r;
}

MlResult ml_mlog_format (MlStr *out, const MlSession *s)
{
	MlResult r = ML_OK;

	for (uint64_t i = 0; i < s->lines.len && r == ML_OK; i++)
	{
		r = format_line (out, ml_session_get (s, i));
	}

	return r;
}

MlResult ml_mlog_load (MlSession *s, MlSlice path)
{
	MlResult  r = ML_OK;
	MlStr     file;
	MlSession loaded;

	ml_str_init (&file);
	ml_session_init (&loaded);

	r = ml_fs_read_file (&file, path);

	if (r == ML_OK)
	{
		r = ml_mlog_parse (&loaded, ml_str_view (file));
	}

	if (r == ML_OK)
	{
		r = ml_session_set_path (&loaded, path);
	}

	if (r == ML_OK)
	{
		for (uint64_t i = 0; i < loaded.lines.len; i++)
		{
			MlLineView v = ml_session_get (&loaded, i);

			loaded.domain = ml_choose (
				v.kind == ML_LINE_MATH,
				v.domain,
				loaded.domain
			);
		}

		ml_session_clear_history (&loaded);
		loaded.dirty    = 0;
		loaded.cursor   = 0;
		loaded.revision = s->revision + 1;
		ml_session_free (s);
		*s = loaded;
		ml_session_init (&loaded);
	}

	/* single cleanup stage: loaded is empty again after a successful move */
	ml_session_free (&loaded);
	ml_str_free (&file);
	return r;
}

MlResult ml_mlog_save (MlSession *s)
{
	MlResult r = ml_choose (
		s->path.len > 0,
		ML_OK,
		ML_ERR_INVALID
	);
	MlStr out;

	ml_str_init (&out);

	if (r == ML_OK)
	{
		r = ml_mlog_format (&out, s);
	}

	if (r == ML_OK)
	{
		r = ml_fs_write_file_atomic (ml_str_view (s->path), ml_str_view (out));
	}

	if (r == ML_OK)
	{
		s->dirty = 0;
	}

	ml_str_free (&out);
	return r;
}
