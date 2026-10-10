#include <meth/texts/texts.h>

#include <meth/core/names.h>
#include <meth/core/vec.h>
#include <meth/platform/config.h>
#include <meth/platform/fs.h>

#include <stdio.h>
#include <string.h>

typedef struct Entry
{
	MlStr key;
	MlStr text;
} Entry;

ML_VEC_DEFINE (EntryVec, Entry)

ML_VEC_DEFINE (TextIndexVec, uint64_t)

static EntryVec entries;
/*
 * By name number (core/names.h) of the code: the entry plus one, 0 for none.
 * It is made again when the names were released or the entries were swapped
 * (index_for is the generation of the names it was made for, 0 for none).
 */
static TextIndexVec by_name;
static uint64_t     index_for = 0;
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
	TextIndexVec_free (&by_name);
	index_for = 0;
}

void ml_texts_clear (void)
{
	generation++;
	free_entries ();
	EntryVec_init (&entries);
	started = 1;
}

static uint64_t find_in (const EntryVec *v, MlSlice key)
{
	uint64_t found = v->len;

	for (uint64_t i = 0; i < v->len && found == v->len; i++)
	{
		if (ml_slice_equal (ml_str_view (v->data [i].key), key))
		{
			found = i;
		}
	}

	return found;
}

/* Makes the index of the entries when it is not there for these names. */
static void ensure_index (void)
{
	if (index_for != ml_names_generation ())
	{
		TextIndexVec_clear (&by_name);

		for (uint64_t i = 0; i < entries.len; i++)
		{
			MlName id = ml_name (ml_str_view (entries.data [i].key));

			while (by_name.len <= id)
			{
				TextIndexVec_push (&by_name, 0);
			}

			by_name.data [id] = i + 1;
		}

		index_for = ml_names_generation ();
	}
}

static uint64_t find (MlSlice key)
{
	MlName id;

	ensure_index ();
	id = ml_name_find (key);

	return ml_choose (
		id < by_name.len && by_name.data [id] != 0,
		by_name.data [id] - 1,
		entries.len
	);
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

		if (r == ML_OK)
		{
			MlName id = ml_name (key);

			while (by_name.len <= id && r == ML_OK)
			{
				r = TextIndexVec_push (&by_name, 0);
			}

			if (r == ML_OK)
			{
				by_name.data [id] = entries.len;
			}
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
	int32_t raw; /* inside a block (code = <<  ...  >>): lines are verbatim */
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

/*
 * One line of a catalog; bad is counted for a line that is nothing. Inside a
 * block every line up to a line that is only >> is taken as it is, white
 * space, # and backslashes included, each with its line break.
 */
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

	if (rd->raw && ml_slice_equal (line, ml_slice_cstr (">>")))
	{
		rd->raw = 0;
		r       = flush (rd);
	}

	else if (rd->raw)
	{
		r = ml_str_append (&rd->text, line);

		if (r == ML_OK)
		{
			r = ml_str_append (&rd->text, ml_slice_cstr ("\n"));
		}
	}

	else if (t.len == 0 || t.data [0] == '#')
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
			MlSlice value = trim (ml_slice_sub (t, eq + 1, t.len - eq - 1));

			rd->raw = ml_slice_equal (value, ml_slice_cstr ("<<"));

			if (!rd->raw)
			{
				r = unescape (&rd->text, value);
			}
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
	rd.raw = 0;

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

MlSlice ml_text_number (char *buf, uint64_t n)
{
	char     digits [ML_TEXT_NUMBER];
	uint64_t len = 0;
	uint64_t at  = 0;

	do
	{
		digits [len++] = (char)('0' + n % 10);
		n             /= 10;
	} while (n > 0);

	for (uint64_t i = len; i > 0; i--)
	{
		buf [at++] = digits [i - 1];
	}

	buf [at] = '\0';
	return ml_slice (buf, at);
}

MlResult ml_text_fill2 (
	MlStr      *out,
	const char *code,
	const char *name1,
	MlSlice    value1,
	const char *name2,
	MlSlice    value2
)
{
	MlTextArg a [2] = {
		{ .name = name1, .value = value1 },
		{ .name = name2, .value = value2 }
	};

	return ml_text_fill (out, ml_text (code), a, 2);
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

/* ---- checking a translation ---- */

static void free_vec (EntryVec *v)
{
	for (uint64_t i = 0; i < v->len; i++)
	{
		ml_str_free (&v->data [i].key);
		ml_str_free (&v->data [i].text);
	}

	EntryVec_free (v);
}

/* 1 when the text has a place {name}. */
static int32_t has_place (MlSlice text, MlSlice name)
{
	int32_t found = 0;

	for (uint64_t i = 0; i + name.len + 2 <= text.len && !found; i++)
	{
		found = (
			   text.data [i] == '{'
			&& ml_slice_equal (ml_slice_sub (text, i + 1, name.len), name)
			&& text.data [i + 1 + name.len] == '}'
		);
	}

	return found;
}

/* One line of the report, from the texts_check section; counts a problem. */
static MlResult report_line (
	MlStr           *report,
	uint64_t        *problems,
	const char      *code,
	const MlTextArg *args,
	uint64_t        count
)
{
	MlResult r = ml_text_fill (report, ml_text (code), args, count);

	r = ml_choose (r == ML_OK, ml_str_append (report, ml_slice_cstr ("\n")), r);
	(*problems)++;
	return r;
}

/* Adds a line of the report for every place of `from` that `to` lacks. */
static MlResult compare_places (
	MlStr    *report,
	uint64_t *problems,
	MlSlice  key,
	MlSlice  from,
	MlSlice  to,
	const char *code
)
{
	MlResult r = ML_OK;

	for (uint64_t i = 0; i < from.len && r == ML_OK; i++)
	{
		uint64_t end = i + 1;

		while (
			   from.data [i] == '{'
			&& end < from.len
			&& (
				   (from.data [end] >= 'a' && from.data [end] <= 'z')
				|| (from.data [end] >= 'A' && from.data [end] <= 'Z')
				|| (from.data [end] >= '0' && from.data [end] <= '9')
				|| from.data [end] == '_'
			)
		)
		{
			end++;
		}

		if (
			   from.data [i] == '{'
			&& end > i + 1
			&& end < from.len
			&& from.data [end] == '}'
			&& !has_place (to, ml_slice_sub (from, i + 1, end - i - 1))
		)
		{
			MlSlice   place = ml_slice_sub (from, i, end - i + 1);
			MlTextArg a [2] = {
				{ .name = "key",   .value = key },
				{ .name = "place", .value = place }
			};

			r = report_line (report, problems, code, a, 2);
		}
	}

	return r;
}

/* 1 when the translation has an entry of the same section and group as key. */
static int32_t group_present (const EntryVec *trans, MlSlice key)
{
	uint64_t dot   = 0;
	uint64_t group = 0;
	int32_t  found = 0;

	while (dot < key.len && key.data [dot] != '.')
	{
		dot++;
	}

	group = dot + 1;

	/* the entries of a lesson are lessons.<id>.<what>: the group is the id */
	if (dot == 7 && memcmp (key.data, "lessons", 7) == 0)
	{
		while (group < key.len && key.data [group] != '.')
		{
			group++;
		}
	}

	else
	{
		group = dot;
	}

	for (uint64_t i = 0; i < trans->len && !found; i++)
	{
		MlSlice other = ml_str_view (trans->data [i].key);

		found = (
			   other.len >= group
			&& ml_slice_equal (
				ml_slice_sub (other, 0, group),
				ml_slice_sub (key, 0, group)
			)
		);
	}

	return found;
}

MlResult ml_texts_check (MlStr *report, MlSlice text, uint64_t *problems)
{
	EntryVec     saved       = entries;
	TextIndexVec saved_index = by_name;
	uint64_t     saved_for   = index_for;
	uint64_t     gen         = generation;
	int32_t      was         = started;
	EntryVec english;
	EntryVec trans;
	uint64_t bad     = 0;
	uint64_t none    = 0;
	MlResult r;

	if (problems == NULL)
	{
		problems = &none;
	}

	*problems = 0;
	EntryVec_init (&entries);
	TextIndexVec_init (&by_name);
	index_for = 0;
	r         = add_built_in ();
	english   = entries;
	EntryVec_init (&entries);
	TextIndexVec_free (&by_name);
	index_for = 0;
	r         = ml_choose (r == ML_OK, ml_texts_add (text, &bad), r);
	trans     = entries;
	TextIndexVec_free (&by_name);
	by_name   = saved_index;
	index_for = saved_for;
	entries   = saved;
	generation = gen;
	started    = was;

	for (uint64_t i = 0; i < english.len && r == ML_OK; i++)
	{
		MlSlice key = ml_str_view (english.data [i].key);
		uint64_t at = find_in (&trans, key);

		if (at == trans.len && group_present (&trans, key))
		{
			MlTextArg a = { .name = "key", .value = key };

			r = report_line (report, problems, "texts_check.missing", &a, 1);
		}

		else if (at < trans.len)
		{
			MlSlice e = ml_str_view (english.data [i].text);
			MlSlice t = ml_str_view (trans.data [at].text);

			r = compare_places (
				report,
				problems,
				key,
				e,
				t,
				"texts_check.place_missing"
			);
			r = ml_choose (
				r == ML_OK,
				compare_places (
					report,
					problems,
					key,
					t,
					e,
					"texts_check.place_extra"
				),
				r
			);
		}
	}

	for (uint64_t i = 0; i < trans.len && r == ML_OK; i++)
	{
		MlSlice key = ml_str_view (trans.data [i].key);

		if (find_in (&english, key) == english.len)
		{
			MlTextArg a = { .name = "key", .value = key };

			r = report_line (report, problems, "texts_check.unknown", &a, 1);
		}
	}

	if (r == ML_OK && bad > 0)
	{
		char      n [ML_TEXT_NUMBER];
		MlTextArg a = { .name = "count", .value = ml_text_number (n, bad) };

		r = report_line (report, problems, "texts_check.bad_lines", &a, 1);
		*problems += bad - 1;
	}

	free_vec (&english);
	free_vec (&trans);
	return r;
}
