#include <matlog/session/settings.h>

#include <matlog/platform/config.h>
#include <matlog/platform/fs.h>
#include <matlog/platform/path.h>

#define SETTINGS_FILE "settings.mlcfg"

static int32_t is_blank (char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static MlSlice trim (MlSlice s)
{
	uint64_t start = 0;
	uint64_t end   = s.len;

	while (start < end && is_blank (s.data [start]))
	{
		start++;
	}

	while (end > start && is_blank (s.data [end - 1]))
	{
		end--;
	}

	return ml_slice_sub (s, start, end - start);
}

/* The lines of a text, one after the other. */
typedef struct LineIter
{
	MlSlice  text;
	uint64_t pos;
} LineIter;

/* The next line without its line break; 0 when there is none. */
static int32_t iter_next (LineIter *it, MlSlice *line)
{
	int32_t  more = it->pos < it->text.len;
	uint64_t end  = it->pos;

	while (end < it->text.len && it->text.data [end] != '\n')
	{
		end++;
	}

	if (more)
	{
		*line   = ml_slice_sub (it->text, it->pos, end - it->pos);
		it->pos = end + 1;
	}

	return more;
}

/* What a line is: a section header, an entry, or something else. */
typedef struct Parsed
{
	int32_t header;
	int32_t entry;
	MlSlice name;  /* the section of a header, the key of an entry */
	MlSlice value;
} Parsed;

static Parsed parse_line (MlSlice raw)
{
	MlSlice line = trim (raw);
	Parsed  p    =
	{
		.header = 0,
		.entry  = 0,
		.name   = ml_slice ("", 0),
		.value  = ml_slice ("", 0)
	};

	uint64_t eq = 0;

	while (eq < line.len && line.data [eq] != '=')
	{
		eq++;
	}

	if (
		   line.len >= 2
		&& line.data [0] == '['
		&& line.data [line.len - 1] == ']'
	)
	{
		p.header = 1;
		p.name   = trim (ml_slice_sub (line, 1, line.len - 2));
	}

	else if (line.len > 0 && line.data [0] != '#' && eq < line.len)
	{
		p.entry = 1;
		p.name  = trim (ml_slice_sub (line, 0, eq));
		p.value = trim (ml_slice_sub (line, eq + 1, line.len - eq - 1));
	}

	return p;
}

MlSetting ml_settings_text_get (MlSlice text, MlSlice section, MlSlice key)
{
	MlSetting s =
	{
		.found = 0,
		.value = ml_slice ("", 0)
	};

	LineIter it   = { .text = text, .pos = 0 };
	int32_t  in   = 0;
	MlSlice  raw;

	while (iter_next (&it, &raw))
	{
		Parsed p = parse_line (raw);

		if (p.header)
		{
			in = ml_slice_equal (p.name, section);
		}

		else if (in && p.entry && ml_slice_equal (p.name, key))
		{
			s.found = 1;
			s.value = p.value;
		}
	}

	return s;
}

static MlResult append_line (MlStr *out, MlSlice line)
{
	MlResult r = ml_str_append (out, line);

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr ("\n"));
	}

	return r;
}

static MlResult append_entry (MlStr *out, MlSlice key, MlSlice value)
{
	return ml_str_appendf (
		out,
		"%.*s = %.*s\n",
		(int)key.len,
		key.data,
		(int)value.len,
		value.data
	);
}

MlResult ml_settings_text_set (
	MlStr   *out,
	MlSlice text,
	MlSlice section,
	MlSlice key,
	MlSlice value
)
{
	MlResult r      = ML_OK;
	LineIter it     = { .text = text, .pos = 0 };
	int32_t  in     = 0;
	int32_t  placed = 0;
	MlSlice  raw;

	while (r == ML_OK && iter_next (&it, &raw))
	{
		Parsed p = parse_line (raw);

		/* leaving the section without having found the entry: add it here */
		if (p.header && in && !placed)
		{
			r      = append_entry (out, key, value);
			placed = 1;
		}

		if (p.header)
		{
			in = ml_slice_equal (p.name, section);
		}

		if (r == ML_OK && in && p.entry && ml_slice_equal (p.name, key))
		{
			r      = ml_choose (placed, ML_OK, append_entry (out, key, value));
			placed = 1;
		}

		else if (r == ML_OK)
		{
			r = append_line (out, ml_slice_sub (raw, 0, raw.len));
		}
	}

	if (r == ML_OK && !placed)
	{
		if (!in)
		{
			r = ml_str_appendf (
				out,
				"%s[%.*s]\n",
				ml_choose (out->len > 0, "\n", ""),
				(int)section.len,
				section.data
			);
		}

		if (r == ML_OK)
		{
			r = append_entry (out, key, value);
		}
	}

	return r;
}

/* The path of the settings file and the folder it is in. */
static MlResult settings_path (MlStr *path, MlStr *dir)
{
	MlResult r = ml_config_dir (dir);

	if (r == ML_OK)
	{
		r = ml_path_join (
			path,
			ml_str_view (*dir),
			ml_slice_cstr (SETTINGS_FILE)
		);
	}

	return r;
}

MlResult ml_settings_get (
	MlStr    *out,
	MlSlice  section,
	MlSlice  key,
	int32_t  *found
)
{
	MlResult r = ML_OK;
	MlStr    dir;
	MlStr    path;
	MlStr    text;

	ml_str_init (&dir);
	ml_str_init (&path);
	ml_str_init (&text);
	*found = 0;

	if (
		   settings_path (&path, &dir) == ML_OK
		&& ml_fs_exists (ml_str_view (path))
	)
	{
		r = ml_fs_read_file (&text, ml_str_view (path));
	}

	if (r == ML_OK)
	{
		MlSetting s = ml_settings_text_get (ml_str_view (text), section, key);

		*found = s.found;

		if (s.found)
		{
			r = ml_str_append (out, s.value);
		}
	}

	ml_str_free (&text);
	ml_str_free (&path);
	ml_str_free (&dir);
	return r;
}

MlResult ml_settings_set (MlSlice section, MlSlice key, MlSlice value)
{
	MlResult r = ML_OK;
	MlStr    dir;
	MlStr    path;
	MlStr    old;
	MlStr    next;

	ml_str_init (&dir);
	ml_str_init (&path);
	ml_str_init (&old);
	ml_str_init (&next);
	r = settings_path (&path, &dir);

	if (r == ML_OK && ml_fs_exists (ml_str_view (path)))
	{
		r = ml_fs_read_file (&old, ml_str_view (path));
	}

	if (r == ML_OK)
	{
		r = ml_settings_text_set (
			&next,
			ml_str_view (old),
			section,
			key,
			value
		);
	}

	if (r == ML_OK && !ml_slice_equal (ml_str_view (old), ml_str_view (next)))
	{
		r = ml_fs_make_dirs (ml_str_view (dir));

		if (r == ML_OK)
		{
			r = ml_fs_write_file_atomic (
				ml_str_view (path),
				ml_str_view (next)
			);
		}
	}

	ml_str_free (&next);
	ml_str_free (&old);
	ml_str_free (&path);
	ml_str_free (&dir);
	return r;
}
