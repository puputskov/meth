#include "test.h"
#include "test_keys.h"

#include <matlog/docs/docs.h>
#include <matlog/platform/fs.h>
#include <matlog/tutorial/lessons.h>
#include <matlog/ui/hint.h>
#include <matlog/ui/keymap.h>

#include <stdlib.h>
#include <string.h>

#define CONFIG_DIR  "matlog_test_config/keymap"
#define CONFIG_FILE "matlog_test_config/keymap/settings.mlcfg"

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static MlKey ctrl (uint32_t letter, uint32_t extra)
{
	MlKey k =
	{
		.code = ML_KEY_CHAR,
		.cp   = letter,
		.mods = ML_MOD_CTRL | extra
	};

	return k;
}

static MlKey code (MlKeyCode c, uint32_t mods)
{
	MlKey k =
	{
		.code = c,
		.cp   = 0,
		.mods = mods
	};

	return k;
}

static void use_test_config (void)
{
#ifdef _WIN32
	_putenv_s ("MATLOG_CONFIG", CONFIG_DIR);
#else
	setenv ("MATLOG_CONFIG", CONFIG_DIR, 1);
#endif
	ml_fs_remove (lit (CONFIG_FILE));
}

/* 1 when the text parses to the key. */
static int32_t parses_to (const char *text, MlKey want)
{
	MlKeyParse p  = ml_key_parse (lit (text));
	int32_t    ok = (
		   p.result == ML_OK
		&& p.key.code == want.code
		&& p.key.cp == want.cp
		&& p.key.mods == want.mods
	);

	if (!ok)
	{
		printf ("  \"%s\": %s\n", text, p.problem);
	}

	return ok;
}

static void test_parse (void)
{
	MlStr spec;

	CHECK (parses_to ("Ctrl+Z", ctrl ('z', 0)));
	CHECK (parses_to ("ctrl+z", ctrl ('z', 0)));
	CHECK (parses_to ("Ctrl+Shift+Z", ctrl ('z', ML_MOD_SHIFT)));
	CHECK (parses_to ("Shift+Ctrl+Z", ctrl ('z', ML_MOD_SHIFT)));
	CHECK (parses_to ("Alt+Left", code (ML_KEY_LEFT, ML_MOD_ALT)));
	CHECK (
		parses_to (
			"Ctrl+Alt+Home",
			code (ML_KEY_HOME, ML_MOD_CTRL | ML_MOD_ALT)
		)
	);
	CHECK (parses_to ("Ctrl+Enter", code (ML_KEY_ENTER, ML_MOD_CTRL)));
	CHECK (parses_to ("Shift+Enter", code (ML_KEY_ENTER, ML_MOD_SHIFT)));
	CHECK (parses_to ("F3", code (ML_KEY_F3, 0)));
	CHECK (parses_to ("Shift+F12", code (ML_KEY_F12, ML_MOD_SHIFT)));
	CHECK (parses_to ("Ctrl+PageUp", code (ML_KEY_PAGE_UP, ML_MOD_CTRL)));
	CHECK (parses_to ("Ctrl+PgDn", code (ML_KEY_PAGE_DOWN, ML_MOD_CTRL)));
	CHECK (parses_to ("Alt+Escape", code (ML_KEY_ESCAPE, ML_MOD_ALT)));
	CHECK (parses_to ("Ctrl++", ctrl ('+', 0)));
	CHECK (parses_to ("Ctrl+=", ctrl ('=', 0)));

	/* keys that belong to editing, and things that are no key */
	CHECK (ml_key_parse (lit ("a")).result == ML_ERR_PARSE);
	CHECK (ml_key_parse (lit ("Left")).result == ML_ERR_PARSE);
	CHECK (ml_key_parse (lit ("Shift+Left")).result == ML_ERR_PARSE);
	CHECK (ml_key_parse (lit ("Enter")).result == ML_ERR_PARSE);
	CHECK (ml_key_parse (lit ("Backspace")).result == ML_ERR_PARSE);
	CHECK (ml_key_parse (lit ("Ctrl+")).result == ML_ERR_PARSE);
	CHECK (ml_key_parse (lit ("")).result == ML_ERR_PARSE);
	CHECK (ml_key_parse (lit ("Ctrl+Nonsense")).result == ML_ERR_PARSE);
	CHECK (ml_key_parse (lit ("Ctrl+ab")).result == ML_ERR_PARSE);

	/* the canonical text parses back to the same key */
	ml_str_init (&spec);
	CHECK (ml_key_spec (&spec, ctrl ('z', ML_MOD_SHIFT)) == ML_OK);
	CHECK (ml_slice_equal (ml_str_view (spec), lit ("Ctrl+Shift+Z")));
	ml_str_clear (&spec);
	CHECK (
		ml_key_spec (&spec, code (ML_KEY_PAGE_UP, ML_MOD_CTRL | ML_MOD_ALT))
		== ML_OK
	);
	CHECK (ml_slice_equal (ml_str_view (spec), lit ("Ctrl+Alt+PageUp")));
	ml_str_free (&spec);
}

static void test_actions (void)
{
	for (uint32_t a = 0; a < ML_ACT_COUNT; a++)
	{
		MlSlice name = lit (ml_action_name ((MlAction)a));

		CHECK (ml_action_from_name (name) == (MlAction)a);
		CHECK (strlen (ml_action_summary ((MlAction)a)) > 0);
	}

	CHECK (ml_action_from_name (lit ("nonsense")) == ML_ACT_COUNT);
	CHECK (ml_action_from_name (lit ("")) == ML_ACT_COUNT);
}

static void test_table (void)
{
	MlKeymap km;

	CHECK (ml_keymap_init (&km) == ML_OK);

	/* the defaults, as the editor had them */
	CHECK (ml_keymap_lookup (&km, ctrl ('z', 0)) == ML_ACT_UNDO);
	CHECK (ml_keymap_lookup (&km, ctrl ('z', ML_MOD_SHIFT)) == ML_ACT_REDO);
	CHECK (ml_keymap_lookup (&km, ctrl ('y', 0)) == ML_ACT_REDO);
	CHECK (ml_keymap_lookup (&km, ctrl ('r', 0)) == ML_ACT_REDO);
	CHECK (
		ml_keymap_lookup (&km, code (ML_KEY_ENTER, ML_MOD_CTRL))
		== ML_ACT_BLOCK_NEW
	);
	CHECK (
		ml_keymap_lookup (&km, code (ML_KEY_ENTER, ML_MOD_SHIFT))
		== ML_ACT_BLOCK_NEW
	);
	CHECK (
		ml_keymap_lookup (&km, code (ML_KEY_ENTER, ML_MOD_ALT))
		== ML_ACT_FOLD
	);
	CHECK (ml_keymap_lookup (&km, code (ML_KEY_F3, 0)) == ML_ACT_STEPS);
	CHECK (ml_keymap_lookup (&km, code (ML_KEY_F1, 0)) == ML_ACT_HELP);
	CHECK (
		ml_keymap_lookup (&km, code (ML_KEY_UP, ML_MOD_ALT))
		== ML_ACT_LINES_UP
	);

	/* a terminal that reports Ctrl+Shift+Z with a capital letter */
	CHECK (ml_keymap_lookup (&km, ctrl ('Z', ML_MOD_SHIFT)) == ML_ACT_REDO);

	/* plain keys and unbound keys have no action */
	CHECK (ml_keymap_lookup (&km, code (ML_KEY_UP, 0)) == ML_ACT_NONE);
	CHECK (ml_keymap_lookup (&km, ctrl ('k', 0)) == ML_ACT_NONE);
	CHECK (ml_keymap_lookup (&km, code (ML_KEY_ENTER, 0)) == ML_ACT_NONE);

	/* a key can be bound again, and removed */
	CHECK (ml_keymap_bind (&km, ctrl ('k', 0), ML_ACT_COMMAND) == ML_OK);
	CHECK (ml_keymap_lookup (&km, ctrl ('k', 0)) == ML_ACT_COMMAND);
	CHECK (ml_keymap_bind (&km, ctrl ('z', 0), ML_ACT_SAVE) == ML_OK);
	CHECK (ml_keymap_lookup (&km, ctrl ('z', 0)) == ML_ACT_SAVE);
	CHECK (ml_keymap_unbind (&km, ctrl ('z', 0)));
	CHECK (ml_keymap_lookup (&km, ctrl ('z', 0)) == ML_ACT_NONE);
	CHECK (!ml_keymap_unbind (&km, ctrl ('z', 0)));

	ml_keymap_free (&km);
}

static void test_apply (void)
{
	MlKeymap km;
	MlStr    report;
	uint64_t bad = 0;

	CHECK (ml_keymap_init (&km) == ML_OK);
	ml_str_init (&report);
	CHECK (
		ml_keymap_apply_text (
			&km,
			lit (
				"[display]\n"
				"theme = dark\n"
				"[keys]\n"
				"# a comment\n"
				"Ctrl+K = command\n"
				"Ctrl+Z = none\n"
				"Ctrl+Q = nonsense\n"
				"Q = save\n"
				"Alt+F4 = quit\n"
				"[other]\n"
				"Ctrl+L = none\n"
			),
			&report,
			&bad
		) == ML_OK
	);
	CHECK (bad == 2);
	CHECK (ml_keymap_lookup (&km, ctrl ('k', 0)) == ML_ACT_COMMAND);
	CHECK (ml_keymap_lookup (&km, ctrl ('z', 0)) == ML_ACT_NONE);
	CHECK (ml_keymap_lookup (&km, ctrl ('q', 0)) == ML_ACT_QUIT);
	CHECK (ml_keymap_lookup (&km, code (ML_KEY_F4, ML_MOD_ALT)) == ML_ACT_QUIT);

	/* only the section [keys] counts */
	CHECK (ml_keymap_lookup (&km, ctrl ('l', 0)) == ML_ACT_SELECT_LINE);

	/* the report names the lines */
	CHECK (
		strstr (ml_str_cstr (report), "line 7: not an action: nonsense")
		!= NULL
	);
	CHECK (
		strstr (ml_str_cstr (report), "line 8: a key needs Ctrl or Alt")
		!= NULL
	);

	ml_str_free (&report);
	ml_keymap_free (&km);
}

static void test_format (void)
{
	MlKeymap km;
	MlStr    keys;
	MlStr    table;

	CHECK (ml_keymap_init (&km) == ML_OK);
	ml_str_init (&keys);
	ml_str_init (&table);
	CHECK (ml_keymap_keys_of (&keys, &km, ML_ACT_REDO) == ML_OK);
	CHECK (
		ml_slice_equal (
			ml_str_view (keys),
			lit ("Ctrl+Y, Ctrl+Shift+Z, Ctrl+R")
		)
	);

	/* the page of the documentation shows the default table */
	CHECK (ml_keymap_format (&table, &km) == ML_OK);
	{
		const char *page = NULL;

		for (uint64_t i = 0; i < ml_doc_source_count; i++)
		{
			if (strcmp (ml_doc_sources [i].name, "keys") == 0)
			{
				page = ml_doc_sources [i].text;
			}
		}

		CHECK (page != NULL);
		CHECK (page != NULL && strstr (page, ml_str_cstr (table)) != NULL);
	}

	ml_str_free (&table);
	ml_str_free (&keys);
	ml_keymap_free (&km);
}

static void press_combo (MlEditor *ed, MlKey k)
{
	CHECK (ml_editor_handle_key (ed, k) == ML_OK);
}

/* The editor does what the table says, and :map changes the table. */
static void test_editor (void)
{
	MlEditor ed;
	MlStr    text;

	use_test_config ();
	ml_editor_init (&ed);
	ml_str_init (&text);

	/* undo is on Ctrl+Z at first */
	test_text (&ed, "abc");
	test_leave (&ed);
	CHECK (ed.session.lines.len == 1);
	press_combo (&ed, ctrl ('z', 0));
	CHECK (ed.session.lines.len == 0);

	/* :map moves it to Ctrl+K and writes the file */
	test_normal (&ed, ":map Ctrl+K undo");
	test_key (&ed, ML_KEY_ENTER, 0, 0);
	CHECK (ml_keymap_lookup (&ed.keymap, ctrl ('k', 0)) == ML_ACT_UNDO);
	CHECK (ml_fs_read_file (&text, lit (CONFIG_FILE)) == ML_OK);
	CHECK (strstr (ml_str_cstr (text), "[keys]\nCtrl+K = undo\n") != NULL);

	test_normal (&ed, ":unmap Ctrl+Z");
	test_key (&ed, ML_KEY_ENTER, 0, 0);
	CHECK (ml_keymap_lookup (&ed.keymap, ctrl ('z', 0)) == ML_ACT_NONE);

	test_text (&ed, "abc");
	test_leave (&ed);
	CHECK (ed.session.lines.len == 1);
	press_combo (&ed, ctrl ('z', 0));
	CHECK (ed.session.lines.len == 1);
	press_combo (&ed, ctrl ('k', 0));
	CHECK (ed.session.lines.len == 0);

	/* a key that is not one, or an action that is not */
	test_normal (&ed, ":map Left undo");
	test_key (&ed, ML_KEY_ENTER, 0, 0);
	CHECK (ml_keymap_lookup (&ed.keymap, code (ML_KEY_LEFT, 0)) == ML_ACT_NONE);
	test_normal (&ed, ":map Ctrl+J flip");
	test_key (&ed, ML_KEY_ENTER, 0, 0);
	CHECK (ml_keymap_lookup (&ed.keymap, ctrl ('j', 0)) == ML_ACT_NONE);

	/* a new editor reads the file: Ctrl+K is undo and Ctrl+Z is gone */
	{
		MlEditor again;

		ml_editor_init (&again);
		CHECK (ml_editor_load_keys (&again) == ML_OK);
		CHECK (ml_keymap_lookup (&again.keymap, ctrl ('k', 0)) == ML_ACT_UNDO);
		CHECK (ml_keymap_lookup (&again.keymap, ctrl ('z', 0)) == ML_ACT_NONE);
		CHECK (ml_keymap_lookup (&again.keymap, ctrl ('y', 0)) == ML_ACT_REDO);
		ml_editor_free (&again);
	}

	/* :keys opens the page with the table in use */
	test_normal (&ed, ":keys");
	test_key (&ed, ML_KEY_ENTER, 0, 0);
	CHECK (ed.state == ML_STATE_HELP);
	CHECK (ed.help.docs.pages.data [ed.help.page].id.len == 8);

	ml_str_free (&text);
	ml_editor_free (&ed);
	ml_fs_remove (lit (CONFIG_FILE));
}

/* 1 when the text expands to the expected text. */
static int32_t expands_to (
	const MlKeymap *km,
	const char     *text,
	char           open,
	char           close,
	int32_t        keep,
	const char     *want
)
{
	MlStr   out;
	int32_t ok;

	ml_str_init (&out);
	ok = (
		   ml_keymap_expand (&out, km, lit (text), open, close, keep)
		== ML_OK
		&& ml_slice_equal (ml_str_view (out), lit (want))
	);

	if (!ok)
	{
		printf ("  \"%s\" gives \"%s\"\n", text, ml_str_cstr (out));
	}

	ml_str_free (&out);
	return ok;
}

static void test_expand (void)
{
	MlKeymap km;

	CHECK (ml_keymap_init (&km) == ML_OK);

	/* the placeholder names the action, the key comes from the table */
	CHECK (ml_action_from_placeholder (lit ("ACTION_UNDO")) == ML_ACT_UNDO);
	CHECK (
		ml_action_from_placeholder (lit ("ACTION_BLOCK_NEW"))
		== ML_ACT_BLOCK_NEW
	);
	CHECK (ml_action_from_placeholder (lit ("ACTION_NOPE")) == ML_ACT_COUNT);
	CHECK (ml_action_from_placeholder (lit ("UNDO")) == ML_ACT_COUNT);
	CHECK (ml_action_from_placeholder (lit ("ACTION_NONE")) == ML_ACT_COUNT);

	CHECK (
		expands_to (
			&km,
			"{ACTION_UNDO} undoes",
			'{',
			'}',
			1,
			"{Ctrl+Z} undoes"
		)
	);
	CHECK (
		expands_to (
			&km,
			"{ACTION_UNDO} undoes",
			'{',
			'}',
			0,
			"Ctrl+Z undoes"
		)
	);
	CHECK (
		expands_to (
			&km,
			"<ACTION_SAVE><Enter>",
			'<',
			'>',
			1,
			"<Ctrl+S><Enter>"
		)
	);

	/* what is not a placeholder stays, an unknown action too */
	CHECK (
		expands_to (
			&km,
			"{Tab} and {ACTION_NOPE}",
			'{',
			'}',
			1,
			"{Tab} and {ACTION_NOPE}"
		)
	);
	CHECK (expands_to (&km, "{unclosed", '{', '}', 1, "{unclosed"));

	/* another binding shows; with none the action is named */
	CHECK (ml_keymap_unbind (&km, ml_key_parse (lit ("Ctrl+S")).key));
	CHECK (expands_to (&km, "{ACTION_SAVE}", '{', '}', 0, "save"));
	CHECK (
		ml_keymap_bind (
			&km,
			ml_key_parse (lit ("Alt+W")).key,
			ML_ACT_SAVE
		)
		== ML_OK
	);
	CHECK (expands_to (&km, "{ACTION_SAVE}", '{', '}', 0, "Alt+W"));

	ml_keymap_free (&km);
}

/* 1 when the text has the key as a word (Ctrl+L is not in Ctrl+Left). */
static int32_t mentions (const char *text, const char *spec)
{
	int32_t  found = 0;
	uint64_t n     = strlen (spec);
	const char *p  = strstr (text, spec);

	while (p != NULL)
	{
		char after  = p [n];
		char before = ml_choose (p > text, p [-1], ' ');

		found = found || (
			   !(after >= 'a' && after <= 'z')
			&& !(after >= 'A' && after <= 'Z')
			&& !(after >= '0' && after <= '9')
			&& before != '+'
		);
		p = strstr (p + 1, spec);
	}

	return found;
}

/* 1 when every placeholder between open and close names an action. */
static int32_t placeholders_ok (
	const char *where,
	const char *text,
	char       open,
	char       close
)
{
	int32_t    ok = 1;
	const char *p = strchr (text, open);

	while (p != NULL)
	{
		const char *e = strchr (p, close);

		if (e != NULL && strncmp (p + 1, "ACTION_", 7) == 0)
		{
			MlSlice name = ml_slice_sub (lit (p), 1, (uint64_t)(e - p) - 1);

			if (ml_action_from_placeholder (name) == ML_ACT_COUNT)
			{
				printf ("  %s: unknown action in \"%.20s\"\n", where, p);
				ok = 0;
			}
		}

		p = strchr (p + 1, open);
	}

	return ok;
}

/* 1 when no key of the default table is written out in the text. */
static int32_t no_keys_written (
	const MlKeymap *km,
	const char     *where,
	const char     *text
)
{
	int32_t ok = 1;

	for (uint64_t i = 0; i < km->bindings.len; i++)
	{
		MlKeyBinding b = km->bindings.data [i];
		MlStr        spec;
		MlKey        k =
		{
			.code = b.code,
			.cp   = b.cp,
			.mods = b.mods,
			.col  = 0,
			.row  = 0
		};

		ml_str_init (&spec);
		CHECK (ml_key_spec (&spec, k) == ML_OK);

		if (mentions (text, ml_str_cstr (spec)))
		{
			printf (
				"  %s: the key %s is written out\n",
				where,
				ml_str_cstr (spec)
			);
			ok = 0;
		}

		ml_str_free (&spec);
	}

	return ok;
}

/* A text names actions, not keys: every placeholder is one, no key stands. */
static int32_t text_is_clean (
	const MlKeymap *km,
	const char     *where,
	const char     *text,
	char           open,
	char           close
)
{
	return (
		   placeholders_ok (where, text, open, close)
		&& no_keys_written (km, where, text)
	);
}

/* Hints, tutorial texts and documentation pages name actions, not keys. */
static void test_texts_name_actions (void)
{
	MlKeymap km;

	CHECK (ml_keymap_init (&km) == ML_OK);

	for (uint64_t i = 0; i < ml_hint_count (); i++)
	{
		CHECK (text_is_clean (&km, "hint", ml_hint_at (i).data, '{', '}'));
	}

	for (uint64_t l = 0; l < ml_lesson_count (); l++)
	{
		MlLesson lesson = ml_lesson_at (l);

		for (uint64_t s = 0; s < lesson.step_count; s++)
		{
			MlStep     step   = lesson.steps [s];
			const char *hint   = ml_choose (step.hint != NULL, step.hint, "");
			const char *answer = ml_choose (
				step.answer != NULL,
				step.answer,
				""
			);

			CHECK (text_is_clean (&km, lesson.id, step.text, '{', '}'));
			CHECK (text_is_clean (&km, lesson.id, hint, '{', '}'));
			CHECK (text_is_clean (&km, lesson.id, answer, '<', '>'));
		}
	}

	/* the page of the keys lists the keys themselves */
	for (uint64_t i = 0; i < ml_doc_source_count; i++)
	{
		MlDocSource page = ml_doc_sources [i];

		if (strcmp (page.name, "keys") != 0)
		{
			CHECK (text_is_clean (&km, page.name, page.text, '{', '}'));
		}
	}

	ml_keymap_free (&km);
}

/* The pages of the documentation show the keys of the user. */
static void test_docs_show_own_keys (void)
{
	MlKeymap km;
	MlDocs   d;
	uint64_t page;
	uint64_t line;
	uint64_t at;
	int32_t  found = 0;
	MlKey    old   = ml_key_parse (lit ("Ctrl+Z")).key;
	MlKey    mine  = ml_key_parse (lit ("Alt+U")).key;

	CHECK (ml_keymap_init (&km) == ML_OK);
	CHECK (ml_keymap_unbind (&km, old));
	CHECK (ml_keymap_bind (&km, mine, ML_ACT_UNDO) == ML_OK);
	ml_docs_init (&d);
	CHECK (ml_docs_load (&d, &km, &page, &line) == ML_OK);
	at = ml_docs_find (&d, lit ("editor"));
	CHECK (at < d.pages.len);

	for (uint64_t i = 0; at < d.pages.len; i++)
	{
		MlDocBlock b = d.pages.data [at].blocks.data [i];

		/* the key stands where Ctrl+Z was, the text after it in its column */
		found = found || strstr (
			ml_str_cstr (b.text),
			"Alt+U" "                  " "undo"
		) != NULL;
		CHECK (strstr (ml_str_cstr (b.text), "ACTION_") == NULL);
		at = ml_choose (i + 1 < d.pages.data [at].blocks.len, at, d.pages.len);
	}

	CHECK (found);
	ml_docs_free (&d);
	ml_keymap_free (&km);
}

void run_keymap_tests (void);

void run_keymap_tests (void)
{
	test_parse ();
	test_actions ();
	test_table ();
	test_apply ();
	test_format ();
	test_expand ();
	test_texts_name_actions ();
	test_docs_show_own_keys ();
	test_editor ();
}
