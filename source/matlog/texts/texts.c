#include <matlog/texts/texts.h>

#include <matlog/core/vec.h>
#include <matlog/platform/config.h>
#include <matlog/platform/fs.h>

#include <stdio.h>
#include <string.h>

typedef struct Entry
{
	MlStr key;
	MlStr text;
} Entry;

ML_VEC_DEFINE (EntryVec, Entry)

static EntryVec entries;
static int32_t  started = 0;
static char     current [32] = "en";
static uint64_t generation = 1;

static void free_entries (void)
{
	for (uint64_t i = 0; i < entries.len; i++)
	{
		ml_str_free (&entries.data [i].key);
		ml_str_free (&entries.data [i].text);
	}

	EntryVec_free (&entries);
}

void ml_texts_clear (void)
{
	generation++;
	free_entries ();
	EntryVec_init (&entries);
	started = 1;
}

static uint64_t find (MlSlice key)
{
	uint64_t found = entries.len;

	for (uint64_t i = 0; i < entries.len && found == entries.len; i++)
	{
		if (ml_slice_equal (ml_str_view (entries.data [i].key), key))
		{
			found = i;
		}
	}

	return found;
}

/* Sets the text of a key: replaces the one there, or adds the entry. */
static MlResult put (MlSlice key, MlSlice text)
{
	MlResult r  = ML_OK;
	uint64_t at = find (key);

	generation++;

	if (at < entries.len)
	{
		ml_str_clear (&entries.data [at].text);
		r = ml_str_append (&entries.data [at].text, text);
	}

	else
	{
		Entry e;

		ml_str_init (&e.key);
		ml_str_init (&e.text);
		r = ml_str_append (&e.key, key);

		if (r == ML_OK)
		{
			r = ml_str_append (&e.text, text);
		}

		if (r == ML_OK)
		{
			r = EntryVec_push (&entries, e);
		}

		if (r != ML_OK)
		{
			ml_str_free (&e.key);
			ml_str_free (&e.text);
		}
	}

	return r;
}

static MlSlice trim (MlSlice s)
{
	uint64_t a = 0;
	uint64_t b = s.len;

	while (a < b && (s.data [a] == ' ' || s.data [a] == '\t'))
	{
		a++;
	}

	while (b > a && (s.data [b - 1] == ' ' || s.data [b - 1] == '\t'))
	{
		b--;
	}

	return ml_slice_sub (s, a, b - a);
}

/* The text with \n and \\ read as the line break and the backslash. */
static MlResult unescape (MlStr *out, MlSlice text)
{
	MlResult r = ML_OK;

	for (uint64_t i = 0; i < text.len && r == ML_OK; i++)
	{
		char c = text.data [i];

		if (c == '\\' && i + 1 < text.len && text.data [i + 1] == 'n')
		{
			r = ml_str_append (out, ml_slice_cstr ("\n"));
			i++;
		}

		else if (c == '\\' && i + 1 < text.len && text.data [i + 1] == '\\')
		{
			r = ml_str_append (out, ml_slice_cstr ("\\"));
			i++;
		}

		else
		{
			r = ml_str_append (out, ml_slice_sub (text, i, 1));
		}
	}

	return r;
}

/* The state of the reader: the section, and the entry being continued. */
typedef struct Reader
{
	MlStr section;
	MlStr key;   /* the entry that a continuation line adds to */
	MlStr text;  /* its text so far, unescaped */
} Reader;

/* Stores the entry the reader has collected. */
static MlResult flush (Reader *rd)
{
	MlResult r = ML_OK;

	if (rd->key.len > 0)
	{
		r = put (ml_str_view (rd->key), ml_str_view (rd->text));
		ml_str_clear (&rd->key);
		ml_str_clear (&rd->text);
	}

	return r;
}

/* One line of a catalog; bad is counted for a line that is nothing. */
static MlResult read_line (Reader *rd, MlSlice line, uint64_t *bad)
{
	MlResult r       = ML_OK;
	MlSlice  t       = trim (line);
	int32_t  cont    = (
		   line.len > 0
		&& (line.data [0] == ' ' || line.data [0] == '\t')
	);
	uint64_t eq      = 0;

	while (eq < t.len && t.data [eq] != '=')
	{
		eq++;
	}

	if (t.len == 0 || t.data [0] == '#')
	{
		r = ML_OK;
	}

	else if (cont && rd->key.len > 0)
	{
		r = ml_str_append (&rd->text, ml_slice_cstr (" "));

		if (r == ML_OK)
		{
			r = unescape (&rd->text, t);
		}
	}

	else if (t.data [0] == '[' && t.data [t.len - 1] == ']')
	{
		r = flush (rd);
		ml_str_clear (&rd->section);

		if (r == ML_OK)
		{
			r = ml_str_append (
				&rd->section,
				trim (ml_slice_sub (t, 1, t.len - 2))
			);
		}
	}

	else if (eq < t.len && trim (ml_slice_sub (t, 0, eq)).len > 0)
	{
		MlSlice name = trim (ml_slice_sub (t, 0, eq));

		r = flush (rd);

		if (r == ML_OK && rd->section.len > 0)
		{
			r = ml_str_appendf (&rd->key, "%s.", ml_str_cstr (rd->section));
		}

		if (r == ML_OK)
		{
			r = ml_str_append (&rd->key, name);
		}

		if (r == ML_OK)
		{
			r = unescape (
				&rd->text,
				trim (ml_slice_sub (t, eq + 1, t.len - eq - 1))
			);
		}
	}

	else
	{
		(*bad)++;
	}

	return r;
}

MlResult ml_texts_add (MlSlice text, uint64_t *bad)
{
	MlResult r    = ML_OK;
	uint64_t pos  = 0;
	uint64_t none = 0;
	Reader   rd;

	ml_str_init (&rd.section);
	ml_str_init (&rd.key);
	ml_str_init (&rd.text);

	if (bad == NULL)
	{
		bad = &none;
	}

	while (pos < text.len && r == ML_OK)
	{
		uint64_t end = pos;
		MlSlice  line;

		while (end < text.len && text.data [end] != '\n')
		{
			end++;
		}

		line = ml_slice_sub (text, pos, end - pos);

		if (line.len > 0 && line.data [line.len - 1] == '\r')
		{
			line.len--;
		}

		r   = read_line (&rd, line, bad);
		pos = end + 1;
	}

	if (r == ML_OK)
	{
		r = flush (&rd);
	}

	ml_str_free (&rd.section);
	ml_str_free (&rd.key);
	ml_str_free (&rd.text);

	return r;
}

/* The built in catalogs (the English texts). */
static MlResult add_built_in (void)
{
	MlResult r = ML_OK;

	for (uint64_t i = 0; i < ml_catalog_source_count && r == ML_OK; i++)
	{
		r = ml_texts_add (ml_slice_cstr (ml_catalog_sources [i].text), NULL);
	}

	return r;
}

/* Reads the catalog file, if there is one; a missing file is not an error. */
static MlResult add_file (MlSlice path, uint64_t *bad)
{
	MlStr    text;
	MlResult r = ML_OK;

	ml_str_init (&text);

	if (ml_fs_read_file (&text, path) == ML_OK)
	{
		r = ml_texts_add (ml_str_view (text), bad);
	}

	ml_str_free (&text);

	return r;
}

ML_VEC_DEFINE (NameVec, MlStr)

/* The names of the lines of text, sorted. */
static MlResult sorted_names (NameVec *out, MlSlice lines)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	while (pos < lines.len && r == ML_OK)
	{
		uint64_t end = pos;
		MlStr    name;

		while (end < lines.len && lines.data [end] != '\n')
		{
			end++;
		}

		ml_str_init (&name);
		r = ml_str_append (&name, ml_slice_sub (lines, pos, end - pos));

		if (r == ML_OK && name.len > 0)
		{
			r = NameVec_push (out, name);
		}

		else
		{
			ml_str_free (&name);
		}

		pos = end + 1;
	}

	/* insertion sort: a folder has a handful of files */
	for (uint64_t i = 1; i < out->len; i++)
	{
		for (
			uint64_t k = i;
			k > 0 && strcmp (
				ml_str_cstr (out->data [k - 1]),
				ml_str_cstr (out->data [k])
			) > 0;
			k--
		)
		{
			MlStr swap       = out->data [k - 1];

			out->data [k - 1] = out->data [k];
			out->data [k]     = swap;
		}
	}

	return r;
}

/* Reads the catalog files of a folder, in the order of their names. */
static MlResult add_folder (MlSlice dir, uint64_t *bad)
{
	MlStr    listing;
	NameVec  names;
	MlResult r;

	ml_str_init (&listing);
	NameVec_init (&names);
	r = ml_fs_list (&listing, dir, ml_slice_cstr (".mltext"));

	if (r == ML_OK)
	{
		r = sorted_names (&names, ml_str_view (listing));
	}

	for (uint64_t i = 0; i < names.len && r == ML_OK; i++)
	{
		MlStr path;

		ml_str_init (&path);
		r = ml_str_appendf (
			&path,
			"%.*s/%s",
			(int32_t)dir.len,
			dir.data,
			ml_str_cstr (names.data [i])
		);

		if (r == ML_OK)
		{
			r = add_file (ml_str_view (path), bad);
		}

		ml_str_free (&path);
	}

	for (uint64_t i = 0; i < names.len; i++)
	{
		ml_str_free (&names.data [i]);
	}

	NameVec_free (&names);
	ml_str_free (&listing);

	return r;
}

/*
 * The files of the settings folder over the English texts: texts/<language>
 * .mltext, then the lessons in lessons/<language>/, each file in the order of
 * its name.
 */
static MlResult add_user (const char *language, uint64_t *bad)
{
	MlStr       dir;
	MlStr       path;
	MlResult    r    = ML_OK;
	const char *lang = ml_choose (
		language != NULL && language [0] != '\0',
		language,
		"en"
	);

	ml_str_init (&dir);
	ml_str_init (&path);

	if (ml_config_dir (&dir) == ML_OK)
	{
		r = ml_str_appendf (
			&path,
			"%s/texts/%s.mltext",
			ml_str_cstr (dir),
			lang
		);

		if (r == ML_OK)
		{
			r = add_file (ml_str_view (path), bad);
		}

		if (r == ML_OK)
		{
			ml_str_clear (&path);
			r = ml_str_appendf (
				&path,
				"%s/lessons/%s",
				ml_str_cstr (dir),
				lang
			);
		}

		if (r == ML_OK)
		{
			r = add_folder (ml_str_view (path), bad);
		}
	}

	ml_str_free (&dir);
	ml_str_free (&path);

	return r;
}

const char *ml_texts_language (void)
{
	return current;
}

MlResult ml_texts_reload (const char *language, uint64_t *bad)
{
	MlResult r;

	if (bad != NULL)
	{
		*bad = 0;
	}

	snprintf (
		current,
		sizeof current,
		"%s",
		ml_choose (language != NULL && language [0] != '\0', language, "en")
	);
	ml_texts_clear ();
	r = add_built_in ();

	if (r == ML_OK)
	{
		r = add_user (language, bad);
	}

	return r;
}

/* The built in texts are there as soon as a text is asked for. */
static void ensure_started (void)
{
	if (!started)
	{
		ml_texts_clear ();
		add_built_in ();
	}
}

const char *ml_text (const char *code)
{
	uint64_t at;

	ensure_started ();
	at = find (ml_slice_cstr (code));

	return ml_choose (
		at < entries.len,
		ml_str_cstr (entries.data [at].text),
		code
	);
}

const char *ml_text_find (const char *code)
{
	uint64_t at;

	ensure_started ();
	at = find (ml_slice_cstr (code));

	return ml_choose (
		at < entries.len,
		ml_str_cstr (entries.data [at].text),
		NULL
	);
}

uint64_t ml_texts_generation (void)
{
	ensure_started ();

	return generation;
}

const char *ml_text_in (const char *section, const char *name)
{
	static char key [128];

	snprintf (key, sizeof key, "%s.%s", section, name);

	return ml_text (key);
}

/* The index of the n-th entry of the section, entries.len past the last. */
static uint64_t nth (const char *section, uint64_t n)
{
	uint64_t found = entries.len;
	uint64_t len   = strlen (section);

	ensure_started ();

	for (uint64_t i = 0; i < entries.len && found == entries.len; i++)
	{
		MlSlice key = ml_str_view (entries.data [i].key);

		if (
			   key.len > len + 1
			&& memcmp (key.data, section, len) == 0
			&& key.data [len] == '.'
		)
		{
			if (n == 0)
			{
				found = i;
			}

			else
			{
				n--;
			}
		}
	}

	return found;
}

uint64_t ml_text_count (const char *section)
{
	uint64_t n = 0;

	while (nth (section, n) < entries.len)
	{
		n++;
	}

	return n;
}

const char *ml_text_nth (const char *section, uint64_t n)
{
	uint64_t at = nth (section, n);

	return ml_choose (
		at < entries.len,
		ml_str_cstr (entries.data [at].text),
		""
	);
}

const char *ml_text_nth_name (const char *section, uint64_t n)
{
	uint64_t at = nth (section, n);

	return ml_choose (
		at < entries.len,
		ml_str_cstr (entries.data [at].key) + strlen (section) + 1,
		""
	);
}

MlResult ml_text_fill (
	MlStr           *out,
	const char      *text,
	const MlTextArg *args,
	uint64_t        count
)
{
	MlResult r   = ML_OK;
	MlSlice  s   = ml_slice_cstr (text);
	uint64_t pos = 0;

	while (pos < s.len && r == ML_OK)
	{
		uint64_t end   = pos + 1;
		int32_t  filled = 0;

		if (s.data [pos] == '{')
		{
			while (end < s.len && s.data [end] != '}' && s.data [end] != '{')
			{
				end++;
			}

			for (
				uint64_t i = 0;
				i < count && end < s.len && s.data [end] == '}' && !filled;
				i++
			)
			{
				MlSlice want = ml_slice_sub (s, pos + 1, end - pos - 1);

				if (ml_slice_equal (want, ml_slice_cstr (args [i].name)))
				{
					r      = ml_str_append (out, args [i].value);
					filled = 1;
				}
			}
		}

		if (filled)
		{
			pos = end + 1;
		}

		else
		{
			r = ml_str_append (out, ml_slice_sub (s, pos, 1));
			pos++;
		}
	}

	return r;
}
