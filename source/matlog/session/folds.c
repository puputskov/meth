#include <matlog/session/folds.h>

#include <matlog/platform/config.h>
#include <matlog/platform/fs.h>
#include <matlog/platform/path.h>

#define SETTINGS_FILE "settings.mlcfg"

MlResult ml_folds_format (MlStr *out, const MlSession *s)
{
	MlResult r     = ML_OK;
	int32_t  first = 1;

	for (uint64_t i = 0; i < s->lines.len && r == ML_OK; i++)
	{
		MlBlock b = ml_session_block (s, i);

		if (b.first == i && b.folded && b.last > b.first)
		{
			r = ml_str_appendf (
				out,
				"%s%u",
				ml_choose (first, "", " "),
				(uint32_t)i
			);
			first = 0;
		}
	}

	return r;
}

static int32_t is_blank (char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static MlSlice trim_right (MlSlice s)
{
	uint64_t end = s.len;

	while (end > 0 && is_blank (s.data [end - 1]))
	{
		end--;
	}

	return ml_slice_sub (s, 0, end);
}

static MlSlice trim_both (MlSlice s)
{
	uint64_t start = 0;

	while (start < s.len && is_blank (s.data [start]))
	{
		start++;
	}

	return trim_right (ml_slice_sub (s, start, s.len - start));
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

/* A "key = value" line: the text before the first " = " and the rest. */
typedef struct Entry
{
	int32_t found;
	MlSlice key;
	MlSlice value;
} Entry;

static Entry split_entry (MlSlice line)
{
	Entry e =
	{
		.found = 0,
		.key   = ml_slice ("", 0),
		.value = ml_slice ("", 0)
	};

	for (uint64_t i = 0; i + 3 <= line.len && !e.found; i++)
	{
		if (ml_slice_equal (ml_slice_sub (line, i, 3), ml_slice_cstr (" = ")))
		{
			e.found = 1;
			e.key   = trim_both (ml_slice_sub (line, 0, i));
			e.value = trim_both (
				ml_slice_sub (line, i + 3, line.len - i - 3)
			);
		}
	}

	return e;
}

static int32_t is_section (MlSlice line)
{
	return line.len > 0 && line.data [0] == '[';
}

static int32_t is_folds (MlSlice line)
{
	return ml_slice_equal (line, ml_slice_cstr ("[folds]"));
}

/* The settings file of the user, and the folder it is in. */
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

static int32_t is_digit (char c)
{
	return c >= '0' && c <= '9';
}

/* Folds the blocks that start at the line numbers listed in value. */
static void apply_folds (MlSession *s, MlSlice value)
{
	uint64_t pos = 0;

	while (pos < value.len)
	{
		uint64_t n      = 0;
		int32_t  digits = 0;

		while (pos < value.len && !is_digit (value.data [pos]))
		{
			pos++;
		}

		while (pos < value.len && is_digit (value.data [pos]))
		{
			n      = n * 10 + (uint64_t)(value.data [pos] - '0');
			digits = 1;
			pos++;
		}

		if (digits && n < s->lines.len)
		{
			MlBlock b = ml_session_block (s, n);

			if (b.first == n && b.last > b.first)
			{
				ml_session_set_folded (s, n, 1);
			}
		}
	}
}

MlResult ml_folds_load (MlSession *s)
{
	MlResult r = ML_OK;
	MlStr    dir;
	MlStr    path;
	MlStr    text;

	ml_str_init (&dir);
	ml_str_init (&path);
	ml_str_init (&text);

	if (
		   s->path.len > 0
		&& settings_path (&path, &dir) == ML_OK
		&& ml_fs_exists (ml_str_view (path))
	)
	{
		r = ml_fs_read_file (&text, ml_str_view (path));
	}

	if (r == ML_OK)
	{
		LineIter it       = { .text = ml_str_view (text), .pos = 0 };
		int32_t  in_folds = 0;
		MlSlice  raw;

		while (iter_next (&it, &raw))
		{
			MlSlice line = trim_both (raw);
			Entry   e    = split_entry (line);

			if (is_section (line))
			{
				in_folds = is_folds (line);
			}

			else if (
				   in_folds
				&& e.found
				&& ml_slice_equal (e.key, ml_str_view (s->path))
			)
			{
				apply_folds (s, e.value);
			}
		}
	}

	ml_str_free (&text);
	ml_str_free (&path);
	ml_str_free (&dir);
	return r;
}

static MlResult append_entry (MlStr *out, MlSlice path, MlSlice entry)
{
	return ml_str_appendf (
		out,
		"%.*s = %.*s\n",
		(int)path.len,
		path.data,
		(int)entry.len,
		entry.data
	);
}

/*
 * The settings text with the entry of path replaced by entry (removed when
 * entry is empty). The entry goes at the end of the section [folds], which
 * is made at the end of the text when it is missing.
 */
static MlResult rewrite (
	MlStr   *out,
	MlSlice old,
	MlSlice path,
	MlSlice entry
)
{
	MlResult r        = ML_OK;
	LineIter it       = { .text = old, .pos = 0 };
	int32_t  in_folds = 0;
	int32_t  placed   = entry.len == 0;
	MlSlice  raw;

	while (r == ML_OK && iter_next (&it, &raw))
	{
		MlSlice line = trim_both (raw);
		Entry   e    = split_entry (line);

		if (is_section (line) && in_folds && !placed)
		{
			r      = append_entry (out, path, entry);
			placed = 1;
		}

		if (is_section (line))
		{
			in_folds = is_folds (line);
		}

		if (
			   r == ML_OK
			&& !(in_folds && e.found && ml_slice_equal (e.key, path))
		)
		{
			r = ml_str_append (out, trim_right (raw));

			if (r == ML_OK)
			{
				r = ml_str_append (out, ml_slice_cstr ("\n"));
			}
		}
	}

	if (r == ML_OK && !placed && in_folds)
	{
		r = append_entry (out, path, entry);
	}

	else if (r == ML_OK && !placed)
	{
		r = ml_str_appendf (
			out,
			"%s[folds]\n",
			ml_choose (out->len > 0, "\n", "")
		);

		if (r == ML_OK)
		{
			r = append_entry (out, path, entry);
		}
	}

	return r;
}

MlResult ml_folds_save (const MlSession *s)
{
	MlResult r = ML_OK;
	MlStr    dir;
	MlStr    path;
	MlStr    entry;
	MlStr    old;
	MlStr    next;

	ml_str_init (&dir);
	ml_str_init (&path);
	ml_str_init (&entry);
	ml_str_init (&old);
	ml_str_init (&next);

	if (s->path.len > 0 && settings_path (&path, &dir) == ML_OK)
	{
		r = ml_folds_format (&entry, s);

		if (r == ML_OK && ml_fs_exists (ml_str_view (path)))
		{
			r = ml_fs_read_file (&old, ml_str_view (path));
		}

		if (r == ML_OK)
		{
			r = rewrite (
				&next,
				ml_str_view (old),
				ml_str_view (s->path),
				ml_str_view (entry)
			);
		}

		if (
			   r == ML_OK
			&& !ml_slice_equal (ml_str_view (old), ml_str_view (next))
		)
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
	}

	ml_str_free (&next);
	ml_str_free (&old);
	ml_str_free (&entry);
	ml_str_free (&path);
	ml_str_free (&dir);
	return r;
}
