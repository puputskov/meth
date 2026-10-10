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
	CHECK (count == 15);
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

void run_texts_tests (void);

void run_texts_tests (void)
{
	test_format ();
	test_override_and_add ();
	test_fill ();
	test_language_file ();
	test_complete ();
	test_hints ();
	test_lessons ();
	test_lesson_files ();
}
