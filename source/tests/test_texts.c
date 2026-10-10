#include "test.h"

#include <matlog/logic/logic.h>
#include <matlog/math/value.h>
#include <matlog/parse/parser.h>
#include <matlog/platform/fs.h>
#include <matlog/session/settings.h>
#include <matlog/texts/texts.h>
#include <matlog/tutorial/lessons.h>
#include <matlog/ui/keymap.h>
#include <matlog/ui/hint.h>

#include <string.h>

#define CONFIG_DIR  "matlog_test_config/texts"
#define CONFIG_FILE "matlog_test_config/texts/settings.mlcfg"
#define TEXT_FILE   "matlog_test_config/texts/texts/de.mltext"

#define OWN_FILE  "matlog_test_config/texts/lessons/en/10-mine.mltext"
#define LATE_FILE "matlog_test_config/texts/lessons/en/20-late.mltext"

#define OWN_LESSON \
	"[lessons]\n" \
	"mine.title = Mine\n" \
	"mine.stage = 2\n" \
	"mine.1.check = steps\n" \
	"mine.1.text = Show the steps.\n" \
	"mine.1.answer = <F3>\n"

#define LATE_LESSON \
	"[lessons]\n" \
	"zlate.title = Late\n" \
	"zlate.stage = 2\n" \
	"zlate.1.check = steps\n" \
	"zlate.1.text = Again.\n" \
	"zlate.1.answer = <F3>\n"

#define GERMAN \
	"[errors]\n" \
	"div_zero = Division durch null\n" \
	"undefined_name_of = unbekannter Name '{name}'\n"

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* 1 when the two texts are the same. */
static int32_t is (const char *got, const char *want)
{
	int32_t same = strcmp (got, want) == 0;

	if (!same)
	{
		printf ("  \"%s\", expected \"%s\"\n", got, want);
	}

	return same;
}

static void use_test_config (void)
{
#ifdef _WIN32
	_putenv_s ("MATLOG_CONFIG", CONFIG_DIR);
#else
	setenv ("MATLOG_CONFIG", CONFIG_DIR, 1);
#endif
}

/* The catalog text: sections, continuation lines, escapes, comments. */
static void test_format (void)
{
	uint64_t bad = 0;

	ml_texts_clear ();
	CHECK (
		ml_texts_add (
			lit (
				"# a comment\n"
				"[errors]\n"
				"div = division by zero\n"
				"long = first part\n"
				"    second part\n"
				"\tthird part\n"
				"two = a\\nb and \\\\ done\n"
				"\n"
				"[hints]\n"
				"1 = one\n"
				"2 = two\n"
				"= nothing here\n"
				"no equals sign here\n"
				"3 = three = still three\r\n"
			),
			&bad
		) == ML_OK
	);

	CHECK (is (ml_text ("errors.div"), "division by zero"));
	CHECK (is (ml_text ("errors.two"), "a\nb and \\ done"));
	CHECK (is (ml_text_in ("hints", "2"), "two"));
	CHECK (is (ml_text ("hints.3"), "three = still three"));
	CHECK (
		is (ml_text ("errors.long"), "first part second part third part")
	);

	/* a line that is nothing is counted, the entry with no name too */
	CHECK (bad == 2);

	/* a code without a text is shown as it is */
	CHECK (is (ml_text ("errors.nothing"), "errors.nothing"));

	/* the entries of a section, in the order of the file */
	CHECK (ml_text_count ("hints") == 3);
	CHECK (is (ml_text_nth ("hints", 0), "one"));
	CHECK (is (ml_text_nth ("hints", 2), "three = still three"));
	CHECK (is (ml_text_nth_name ("hints", 1), "2"));
	CHECK (is (ml_text_nth ("hints", 9), ""));
	CHECK (ml_text_count ("errors") == 3);
	CHECK (ml_text_count ("nothing") == 0);

	ml_texts_reload ("en", NULL);
}

static void test_block (void)
{
	uint64_t bad = 0;

	ml_texts_clear ();
	CHECK (
		ml_texts_add (
			lit (
				"[help]\n"
				"a = <<\n"
				"usage: x\n"
				"\n"
				"  indented \\n # not a comment = nor an entry\n"
				"[not a section]\n"
				">>\n"
				"b = after the block\n"
				"c = <<\r\n"
				"crlf\r\n"
				">>\r\n"
				"d = <<\n"
				"open to the end\n"
			),
			&bad
		) == ML_OK
	);

	CHECK (bad == 0);
	CHECK (
		is (
			ml_text ("help.a"),
			"usage: x\n\n  indented \\n # not a comment = nor an entry\n"
			"[not a section]\n"
		)
	);
	CHECK (is (ml_text ("help.b"), "after the block"));
	CHECK (is (ml_text ("help.c"), "crlf\n"));
	CHECK (is (ml_text ("help.d"), "open to the end\n"));
	CHECK (ml_text_count ("help") == 4);
}

static int32_t has_line (const MlStr *report, const char *line)
{
	MlSlice r    = ml_str_view (*report);
	MlSlice want = lit (line);
	int32_t found = 0;

	for (uint64_t i = 0; i + want.len <= r.len && !found; i++)
	{
		found = (
			   ml_slice_equal (ml_slice_sub (r, i, want.len), want)
			&& (i == 0 || r.data [i - 1] == '\n')
			&& (i + want.len == r.len || r.data [i + want.len] == '\n')
		);
	}

	return found;
}

static void test_check (void)
{
	MlStr    report;
	uint64_t problems = 99;

	ml_str_init (&report);
	CHECK (ml_texts_reload ("en", NULL) == ML_OK);

	/* the English texts themselves are fine */
	CHECK (
		ml_texts_check (
			&report,
			lit (
				"[errors]\n"
				"div_zero = division by zero\n"
				"[messages]\n"
				"exported = exported to {path}\n"
			),
			&problems
		) == ML_OK
	);
	CHECK (problems > 0);
	CHECK (!has_line (&report, "not a known text: errors.div_zero"));
	CHECK (!has_line (&report, "missing: errors.div_zero"));
	CHECK (has_line (&report, "missing: errors.syntax"));
	CHECK (!has_line (&report, "missing: messages.exported"));

	/* mistakes: a place lost, a place added, an unknown entry, a bad line */
	ml_str_clear (&report);
	CHECK (
		ml_texts_check (
			&report,
			lit (
				"[messages]\n"
				"exported = exportiert nach {pfad}\n"
				"theme_ignored = ({count} ignoriert)\n"
				"nothing_like_this = x\n"
				"this is no entry\n"
			),
			&problems
		) == ML_OK
	);
	CHECK (
		has_line (
			&report,
			"messages.exported: the place {path} of the English text is not "
			"in the translation"
		)
	);
	CHECK (
		has_line (
			&report,
			"messages.exported: the place {pfad} is not in the English text"
		)
	);
	CHECK (
		has_line (
			&report,
			"messages.theme_ignored: the place {report} of the English text "
			"is not in the translation"
		)
	);
	CHECK (has_line (&report, "not a known text: messages.nothing_like_this"));
	CHECK (has_line (&report, "1 lines are no entry"));
	CHECK (!has_line (&report, "missing: errors.syntax"));

	/* a file of one lesson is only blamed for the entries of that lesson */
	ml_str_clear (&report);
	CHECK (
		ml_texts_check (
			&report,
			lit ("[lessons]\nnotes.title = Notizen\n"),
			&problems
		) == ML_OK
	);
	CHECK (has_line (&report, "missing: lessons.notes.stage"));
	CHECK (!has_line (&report, "missing: lessons.math.title"));

	/* the texts in use are the same as before */
	CHECK (is (ml_text ("errors.div_zero"), "division by zero"));
	ml_str_free (&report);
}

static void test_override_and_add (void)
{
	ml_texts_reload ("en", NULL);
	CHECK (is (ml_text ("errors.div_zero"), "division by zero"));

	/* a later catalog replaces an entry and can add new ones */
	CHECK (
		ml_texts_add (
			lit (
				"[errors]\ndiv_zero = Division durch null\n"
				"[hints]\n99 = neu\n"
			),
			NULL
		) == ML_OK
	);
	CHECK (is (ml_text ("errors.div_zero"), "Division durch null"));
	CHECK (is (ml_text ("errors.syntax"), "syntax error"));
	CHECK (is (ml_text ("hints.99"), "neu"));
	CHECK (ml_text_count ("hints") == 13);

	/* the evaluator reports with the texts of the catalog */
	CHECK (is (ml_error_text (ML_EVAL_DIV_ZERO), "Division durch null"));

	ml_texts_reload ("en", NULL);
	CHECK (is (ml_error_text (ML_EVAL_DIV_ZERO), "division by zero"));
}

static void test_fill (void)
{
	MlStr     out;
	MlTextArg args [2] = {
		{ .name = "name", .value = { .data = "x", .len = 1 } },
		{ .name = "n",    .value = { .data = "42", .len = 2 } }
	};

	ml_str_init (&out);
	CHECK (
		ml_text_fill (&out, "no '{name}' at {n}, {other} {", args, 2) == ML_OK
	);
	CHECK (is (ml_str_cstr (out), "no 'x' at 42, {other} {"));
	ml_str_free (&out);

	ml_str_init (&out);
	CHECK (ml_text_fill (&out, "{ACTION_UNDO} {name}", args, 1) == ML_OK);
	CHECK (is (ml_str_cstr (out), "{ACTION_UNDO} x"));
	ml_str_free (&out);
}

/* The setting names a language, whose file is read over the English one. */
static void test_language_file (void)
{
	MlStr    shown;
	uint64_t bad = 9;
	MlSlice  display = lit ("display");
	MlSlice  setting = lit ("language");

	use_test_config ();
	ml_fs_remove (lit (CONFIG_FILE));
	ml_fs_remove (lit (TEXT_FILE));
	CHECK (ml_fs_make_dirs (lit (CONFIG_DIR "/texts")) == ML_OK);
	CHECK (ml_fs_write_file_atomic (lit (TEXT_FILE), lit (GERMAN)) == ML_OK);

	/* without the setting the English texts stay */
	CHECK (ml_texts_use_settings (&bad) == ML_OK);
	CHECK (bad == 0);
	CHECK (is (ml_texts_language (), "en"));
	CHECK (is (ml_error_text (ML_EVAL_DIV_ZERO), "division by zero"));

	CHECK (ml_settings_set (display, setting, lit ("de")) == ML_OK);
	CHECK (ml_texts_use_settings (NULL) == ML_OK);
	CHECK (is (ml_texts_language (), "de"));
	CHECK (is (ml_error_text (ML_EVAL_DIV_ZERO), "Division durch null"));
	CHECK (
		is (
			ml_error_text (ML_EVAL_TYPE),
			"these operands cannot be combined"
		)
	);

	/* the message with a name uses the place for it */
	ml_str_init (&shown);
	CHECK (
		ml_error_message (
			&shown,
			ml_value_error (ML_EVAL_UNDEFINED_NAME, 0, 3),
			lit ("abc + 1")
		) == ML_OK
	);
	CHECK (is (ml_str_cstr (shown), "unbekannter Name 'abc'"));
	ml_str_free (&shown);

	/* a language with no file is English */
	CHECK (ml_settings_set (display, setting, lit ("xx")) == ML_OK);
	CHECK (ml_texts_use_settings (NULL) == ML_OK);
	CHECK (is (ml_error_text (ML_EVAL_DIV_ZERO), "division by zero"));

	ml_settings_remove (display, setting);
	ml_fs_remove (lit (CONFIG_FILE));
	ml_fs_remove (lit (TEXT_FILE));
	ml_texts_reload ("en", NULL);
}

/* 1 when the code has a text of its own (not just the code shown back). */
static int32_t has_text (const char *code)
{
	int32_t has = strcmp (ml_text_in ("errors", code), code) != 0;

	if (!has)
	{
		printf ("  no text for %s\n", code);
	}

	return has;
}

/*
 * Every code the program has a text for, and no text that no code uses: the
 * English file and the tables of codes in the C files say the same.
 */
static void test_complete (void)
{
	uint64_t used = 0;

	ml_texts_reload ("en", NULL);

	for (uint32_t k = 0; k < ML_EVAL_KIND_COUNT; k++)
	{
		const char *code = ml_error_code ((MlErrorKind)k);

		CHECK (has_text (code));
		CHECK (
			is (ml_error_text ((MlErrorKind)k), ml_text_in ("errors", code))
		);
		used++;
	}

	for (uint32_t k = 0; k < ML_PARSE_CODE_COUNT; k++)
	{
		CHECK (has_text (ml_parse_code_name ((MlParseCode)k)));
		used++;
	}

	for (uint32_t k = 1; k < ML_LOGIC_COUNT; k++)
	{
		CHECK (has_text (ml_logic_error_code ((MlLogicError)k)));
		CHECK (strlen (ml_logic_error_text ((MlLogicError)k)) > 0);
		used++;
	}

	/* the texts of what has no code, and of a code out of range */
	CHECK (
		is (
			ml_text_in ("errors", "undefined_name_of"),
			"undefined name '{name}'"
		)
	);
	CHECK (is (ml_text_in ("errors", "unknown"), "unknown error"));
	CHECK (is (ml_text_in ("errors", "parse_unknown"), "unknown error"));
	used += 3;

	/* every entry of the section is one of those */
	CHECK (ml_text_count ("errors") == used);
}

/* The hints are the section [hints] and the code asks the catalog for them. */
static void test_hints (void)
{
	ml_texts_reload ("en", NULL);
	CHECK (ml_hint_count () == 12);
	CHECK (ml_hint_count () == ml_text_count ("hints"));
	CHECK (ml_slice_equal (ml_hint_at (0), lit (ml_text_nth ("hints", 0))));
	CHECK (ml_slice_equal (ml_hint_at (12), ml_hint_at (0)));

	/* a hint added in a file is shown too */
	CHECK (
		ml_texts_add (lit ("[hints]\n13 = {F9} does something\n"), NULL)
		== ML_OK
	);
	CHECK (ml_hint_count () == 13);
	CHECK (ml_slice_equal (ml_hint_at (12), lit ("{F9} does something")));
	ml_texts_reload ("en", NULL);
}

/* A lesson in a file of the lessons folder of the settings folder. */
static void test_lesson_files (void)
{
	uint64_t count;
	uint64_t at;

	use_test_config ();
	ml_fs_remove (lit (CONFIG_FILE));
	ml_fs_remove (lit (OWN_FILE));
	ml_fs_remove (lit (LATE_FILE));
	CHECK (ml_fs_make_dirs (lit (CONFIG_DIR "/lessons/en")) == ML_OK);
	ml_texts_reload ("en", NULL);
	count = ml_lesson_count ();

	/* two files: the lessons come in the order of the names of the files */
	CHECK (
		ml_fs_write_file_atomic (lit (LATE_FILE), lit (LATE_LESSON))
		== ML_OK
	);
	CHECK (ml_fs_write_file_atomic (lit (OWN_FILE), lit (OWN_LESSON)) == ML_OK);
	CHECK (ml_texts_reload ("en", NULL) == ML_OK);
	CHECK (ml_lesson_count () == count + 2);
	at = ml_lesson_find (lit ("mine"));
	CHECK (at == count);
	CHECK (is (ml_lesson_at (at).title, "Mine"));
	CHECK (ml_lesson_find (lit ("zlate")) == count + 1);

	ml_fs_remove (lit (OWN_FILE));
	ml_fs_remove (lit (LATE_FILE));
	ml_texts_reload ("en", NULL);
	CHECK (ml_lesson_count () == count);
}

/* The lessons of the tutorial are entries of the catalog. */
static void test_lessons (void)
{
	uint64_t count;
	MlLesson lesson;

	ml_texts_reload ("en", NULL);
	count = ml_lesson_count ();
	CHECK (count == 16);
	CHECK (is (ml_lesson_at (0).id, "notes"));
	CHECK (is (ml_lesson_at (0).title, "Writing and notes"));
	CHECK (ml_lesson_first (1) == 0);
	CHECK (ml_lesson_first (2) == 7);
	CHECK (ml_lesson_find (lit ("math")) == 1);
	CHECK (ml_lesson_at (1).steps [0].check == ML_CHECK_ACTION);
	CHECK (ml_lesson_at (1).steps [0].arg == (int32_t)ML_ACT_BLOCK_NEW);
	CHECK (ml_lesson_at (0).steps [1].arg == (int32_t)ML_KEY_UP);

	/* a word mended in a file shows in the step */
	CHECK (
		ml_texts_add (lit ("[lessons]\nnotes.1.text = Hello. Type.\n"), NULL)
		== ML_OK
	);
	CHECK (is (ml_lesson_at (0).steps [0].text, "Hello. Type."));

	/* a new lesson is new entries; one of stage 2 goes after the others */
	CHECK (
		ml_texts_add (
			lit (
				"[lessons]\n"
				"own.title = My lesson\n"
				"own.stage = 2\n"
				"own.1.check = notes\n"
				"own.1.arg = 2\n"
				"own.1.text = Write two notes.\n"
				"own.1.answer = a<Enter>b<Enter>\n"
				"own.2.check = state\n"
				"own.2.arg = help\n"
				"own.2.text = Open the help.\n"
				"own.2.answer = <F1>\n"
				"own.3.check = nonsense\n"
				"own.3.text = A check that does not exist is left out.\n"
				"own.4.check = key\n"
				"own.4.arg = NoSuchKey\n"
				"own.4.text = A key that does not exist is left out.\n"
			),
			NULL
		) == ML_OK
	);
	CHECK (ml_lesson_count () == count + 1);
	lesson = ml_lesson_at (ml_lesson_find (lit ("own")));
	CHECK (lesson.stage == 2);
	CHECK (lesson.step_count == 2);
	CHECK (lesson.steps [0].check == ML_CHECK_NOTES);
	CHECK (lesson.steps [0].arg == 2);
	CHECK (lesson.steps [1].check == ML_CHECK_STATE);
	CHECK (lesson.steps [1].arg == ML_TUT_HELP);
	CHECK (ml_lesson_find (lit ("own")) == count);

	ml_texts_reload ("en", NULL);
	CHECK (ml_lesson_count () == count);
	CHECK (
		is (
			ml_lesson_at (0).steps [0].text,
			ml_text ("lessons.notes.1.text")
		)
	);
}

/* ---- every entry of the catalog is used ---------------------------------- */

static const char *const source_folders [] = {
	"source/app", "source/matlog", "source/matlog/cli", "source/matlog/core",
	"source/matlog/docs", "source/matlog/export", "source/matlog/logic",
	"source/matlog/math", "source/matlog/parse", "source/matlog/platform",
	"source/matlog/session", "source/matlog/sym", "source/matlog/texts",
	"source/matlog/tutorial", "source/matlog/ui"
};

/* The text of every .c file of the library, but not the generated tables. */
static MlResult read_sources (MlStr *all)
{
	MlResult r = ML_OK;

	for (uint64_t f = 0; f < ml_count (source_folders) && r == ML_OK; f++)
	{
		MlStr    names;
		uint64_t pos = 0;

		ml_str_init (&names);
		r = ml_fs_list (&names, lit (source_folders [f]), lit (".c"));

		while (r == ML_OK && pos < names.len)
		{
			uint64_t end = pos;
			MlStr    path;
			MlStr    text;

			while (end < names.len && names.data [end] != '\n')
			{
				end++;
			}

			ml_str_init (&path);
			ml_str_init (&text);
			r = ml_str_appendf (
				&path,
				"%s/%.*s",
				source_folders [f],
				(int)(end - pos),
				names.data + pos
			);

			if (r == ML_OK && strstr (ml_str_cstr (path), "_data.c") == NULL)
			{
				r = ml_fs_read_file (&text, ml_str_view (path));
				r = ml_choose (
					r == ML_OK,
					ml_str_append (all, ml_str_view (text)),
					r
				);
			}

			ml_str_free (&path);
			ml_str_free (&text);
			pos = end + 1;
		}

		ml_str_free (&names);
	}

	return r;
}

/* 1 when the quoted text "needle" is in the sources. */
static int32_t quoted_in (const MlStr *all, const char *needle)
{
	MlStr    q;
	int32_t  found;

	ml_str_init (&q);
	found = (
		   ml_str_appendf (&q, "\"%s\"", needle) == ML_OK
		&& strstr (ml_str_cstr (*all), ml_str_cstr (q)) != NULL
	);
	ml_str_free (&q);
	return found;
}

/* The sections whose codes are made from the names of a table in the code. */
static int32_t table_section (const char *section)
{
	return (
		   strcmp (section, "errors") == 0
		|| strcmp (section, "actions") == 0
		|| strcmp (section, "options") == 0
		|| strcmp (section, "commands") == 0
		|| strcmp (section, "cli") == 0
		|| strcmp (section, "help") == 0
	);
}

/*
 * An entry is used when the C code names it: as the whole code, "messages.
 * no_help_for", or, for the sections whose code is built from a table (the
 * error codes, the actions, the options, the commands), as the name in that
 * table. The hints are numbered and the lessons are checked by their own
 * test (every lesson is played), so they are left out.
 */
/* Counts the entries of the section that no code names. */
static uint64_t unused_in (const MlStr *all, const char *section)
{
	uint64_t unused = 0;

	for (uint64_t n = 0; n < ml_text_count (section); n++)
	{
		const char *name = ml_text_nth_name (section, n);
		MlStr       code;
		int32_t     used;

		ml_str_init (&code);
		ml_str_appendf (&code, "%s.%s", section, name);
		used = (
			   quoted_in (all, ml_str_cstr (code))
			|| (table_section (section) && quoted_in (all, name))
		);

		if (!used)
		{
			printf ("  not used by any code: %s\n", ml_str_cstr (code));
			unused++;
		}

		ml_str_free (&code);
	}

	return unused;
}

/* The names of the [sections] of a catalog text, one per line. */
static void section_names (MlStr *out, const char *text)
{
	for (const char *p = strchr (text, '['); p != NULL; p = strchr (p + 1, '['))
	{
		const char *e = strchr (p, ']');

		if ((p == text || p [-1] == '\n') && e != NULL)
		{
			ml_str_appendf (out, "%.*s\n", (int)(e - p - 1), p + 1);
		}
	}
}

static void test_every_entry_is_used (void)
{
	MlStr    all;
	MlStr    sections;
	uint64_t unused = 0;
	uint64_t pos    = 0;

	ml_str_init (&all);
	ml_str_init (&sections);
	CHECK (ml_texts_reload ("en", NULL) == ML_OK);

	for (uint64_t i = 0; i < ml_catalog_source_count; i++)
	{
		section_names (&sections, ml_catalog_sources [i].text);
	}

	/* run from another folder, the sources are not there to read */
	CHECK (read_sources (&all) == ML_OK && all.len > 100000);

	while (pos < sections.len)
	{
		uint64_t end = pos;
		MlStr    name;

		while (end < sections.len && sections.data [end] != '\n')
		{
			end++;
		}

		ml_str_init (&name);
		ml_str_append (&name, ml_slice (sections.data + pos, end - pos));

		if (
			   strcmp (ml_str_cstr (name), "hints") != 0
			&& strcmp (ml_str_cstr (name), "lessons") != 0
		)
		{
			unused += unused_in (&all, ml_str_cstr (name));
		}

		ml_str_free (&name);
		pos = end + 1;
	}

	CHECK (unused == 0);
	ml_str_free (&sections);
	ml_str_free (&all);
}

void run_texts_tests (void);

void run_texts_tests (void)
{
	test_format ();
	test_block ();
	test_check ();
	test_override_and_add ();
	test_fill ();
	test_language_file ();
	test_complete ();
	test_hints ();
	test_lessons ();
	test_lesson_files ();
	test_every_entry_is_used ();
}
