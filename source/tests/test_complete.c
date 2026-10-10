#include "test.h"
#include "test_keys.h"

#include <matlog/math/eval.h>
#include <matlog/ui/complete.h>
#include <matlog/ui/editor.h>
#include <matlog/ui/screen.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static int32_t has_item (const MlCompletion *c, const char *name)
{
	int32_t found = 0;

	for (uint64_t i = 0; i < c->count; i++)
	{
		found = found || ml_slice_equal (c->items [i], lit (name));
	}

	return found;
}

static int32_t ghost_is (MlCompletion c, const char *text)
{
	return (
		   c.ghost < ML_MAX_CANDIDATES
		&& ml_slice_equal (c.ghost_text, lit (text))
	);
}

static void test_commands (void)
{
	MlCompletion c = ml_complete_command (lit (""));

	/* nothing typed: every command is listed, none is suggested */
	CHECK (c.count == 39 && c.ghost == ML_MAX_CANDIDATES);
	CHECK (has_item (&c, "mode") && has_item (&c, "open!"));
	CHECK (c.ghost_text.len == 0);

	/* one candidate left: its rest is the ghost text */
	c = ml_complete_command (lit ("mod"));
	CHECK (c.count == 1 && has_item (&c, "mode"));
	CHECK (ghost_is (c, "e"));
	CHECK (c.append_space && c.token_start == 0);

	c = ml_complete_command (lit ("m"));
	CHECK (c.count == 3 && has_item (&c, "mode") && has_item (&c, "move"));
	CHECK (has_item (&c, "map"));
	CHECK (ghost_is (c, "ode"));

	/* several candidates: the first longer one is suggested */
	c = ml_complete_command (lit ("w"));
	CHECK (c.count == 3 && has_item (&c, "w") && has_item (&c, "write"));
	CHECK (has_item (&c, "wq"));
	CHECK (ghost_is (c, "rite") && !c.append_space);

	c = ml_complete_command (lit ("q"));
	CHECK (c.count == 3 && ghost_is (c, "uit"));

	c = ml_complete_command (lit ("open"));
	CHECK (c.count == 2 && ghost_is (c, "!"));

	/* a complete command that nothing extends has no ghost text */
	c = ml_complete_command (lit ("quit"));
	CHECK (c.count == 1 && c.ghost == ML_MAX_CANDIDATES);
	c = ml_complete_command (lit ("x"));
	CHECK (c.count == 1 && c.ghost == ML_MAX_CANDIDATES);

	/* nothing matches */
	c = ml_complete_command (lit ("zzz"));
	CHECK (c.count == 0 && c.ghost == ML_MAX_CANDIDATES);
	c = ml_complete_command (lit ("MO"));
	CHECK (c.count == 0);
}

static void test_arguments (void)
{
	MlCompletion c = ml_complete_command (lit ("mode "));

	/* the values of the argument are listed, and they start after the space */
	CHECK (c.count == 5 && c.token_start == 5);
	CHECK (has_item (&c, "algebra") && has_item (&c, "pga3"));
	CHECK (has_item (&c, "boolean") && c.ghost == ML_MAX_CANDIDATES);

	/* typing narrows the list down */
	c = ml_complete_command (lit ("mode p"));
	CHECK (c.count == 2 && has_item (&c, "pga2") && has_item (&c, "pga3"));
	CHECK (ghost_is (c, "ga2"));

	c = ml_complete_command (lit ("mode pga3"));
	CHECK (c.count == 1 && c.ghost == ML_MAX_CANDIDATES);

	c = ml_complete_command (lit ("mode l"));
	CHECK (c.count == 1 && has_item (&c, "lambda"));
	CHECK (ghost_is (c, "ambda") && !c.append_space);

	c = ml_complete_command (lit ("mode lambda"));
	CHECK (c.count == 1 && c.ghost == ML_MAX_CANDIDATES);

	/* until none is left */
	c = ml_complete_command (lit ("mode lx"));
	CHECK (c.count == 0 && c.ghost == ML_MAX_CANDIDATES);

	/* pga takes 2 or 3 */
	c = ml_complete_command (lit ("pga "));
	CHECK (c.count == 2 && has_item (&c, "2") && has_item (&c, "3"));
	c = ml_complete_command (lit ("pga 3"));
	CHECK (c.count == 1 && c.ghost == ML_MAX_CANDIDATES);

	/* only the first argument is completed, other commands have no values */
	CHECK (ml_complete_command (lit ("mode a b")).count == 0);
	CHECK (ml_complete_command (lit ("q ")).count == 0);
	CHECK (ml_complete_command (lit ("frob ")).count == 0);
	CHECK (ml_complete_command (lit ("open ")).count == 0);
}

static void type (MlEditor *ed, const char *text)
{
	for (uint64_t i = 0; text [i] != '\0'; i++)
	{
		MlKey k =
		{
			.code = ML_KEY_CHAR,
			.cp   = (uint8_t)text [i],
			.mods = 0
		};

		CHECK (ml_editor_handle_key (ed, k) == ML_OK);
	}
}

static void press (MlEditor *ed, MlKeyCode code)
{
	MlKey k = { .code = code, .cp = 0, .mods = 0 };

	CHECK (ml_editor_handle_key (ed, k) == ML_OK);
}

static int32_t command_is (const MlEditor *ed, const char *expected)
{
	int32_t same = ml_slice_equal (ml_str_view (ed->edit.text), lit (expected));

	if (!same)
	{
		printf (
			"  command \"%s\", expected \"%s\"\n",
			ml_str_cstr (ed->edit.text),
			expected
		);
	}

	return same;
}

static void test_editor_keys (void)
{
	MlEditor ed;

	ml_editor_init (&ed);

	/* no completion outside the command line */
	CHECK (ml_editor_completion (&ed).count == 0);
	test_leave (&ed);
	CHECK (ml_editor_completion (&ed).count == 0);

	/* :mo then Tab gives ":mode ", the values are listed, typing narrows */
	test_normal (&ed, ":mod");
	CHECK (ml_editor_completion (&ed).count == 1);
	press (&ed, ML_KEY_TAB);
	CHECK (command_is           (&ed, "mode "));
	CHECK (ml_editor_completion (&ed).count == 5);
	type (&ed, "l");
	CHECK (ml_editor_completion (&ed).count == 1);
	press (&ed, ML_KEY_TAB);
	CHECK (command_is (&ed, "mode lambda"));
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_LAMBDA);

	/* Right at the end of the line accepts the ghost text as well */
	test_normal (&ed, ":mode b");
	press (&ed, ML_KEY_RIGHT);
	CHECK (command_is (&ed, "mode boolean"));
	press (&ed, ML_KEY_RIGHT);
	CHECK (command_is (&ed, "mode boolean"));
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_BOOLEAN);

	/* Tab without a suggestion does nothing */
	test_normal (&ed, ":zzz");
	press (&ed, ML_KEY_TAB);
	CHECK (command_is (&ed, "zzz"));
	press (&ed, ML_KEY_ESCAPE);
	test_normal (&ed, ":mode ");
	press (&ed, ML_KEY_TAB);
	CHECK (command_is (&ed, "mode "));
	press (&ed, ML_KEY_ESCAPE);

	/* a command that needs no argument gets no space */
	test_normal (&ed, ":qu");
	press (&ed, ML_KEY_TAB);
	CHECK (command_is (&ed, "quit"));
	press (&ed, ML_KEY_ESCAPE);

	/* the ghost text only exists with the cursor at the end of the line */
	test_normal (&ed, ":mo");
	press (&ed, ML_KEY_LEFT);
	CHECK (ml_editor_completion (&ed).count == 0);
	press (&ed, ML_KEY_TAB);
	CHECK (command_is (&ed, "mo"));

	ml_editor_free (&ed);
}

static MlResult row_text (MlStr *out, MlSlice row)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	ml_str_clear (out);

	while (r == ML_OK && pos < row.len)
	{
		if (row.data [pos] == 0x1b)
		{
			pos += 2;

			while (
				   pos < row.len
				&& !(row.data [pos] >= '@' && row.data [pos] <= '~')
			)
			{
				pos++;
			}

			pos++;
		}

		else
		{
			r = ml_str_append (out, ml_slice_sub (row, pos, 1));
			pos++;
		}
	}

	return r;
}

static int32_t plain_row_is (const MlLayout *l, uint64_t idx, const char *text)
{
	MlStr   p;
	int32_t same;

	ml_str_init (&p);
	same = (
		   idx < l->rows.len
		&& row_text (&p, ml_str_view (l->rows.data [idx])) == ML_OK
		&& ml_slice_equal (ml_str_view (p), lit (text))
	);

	if (!same)
	{
		printf (
			"  row %u is \"%s\", expected \"%s\"\n",
			(uint32_t)idx,
			ml_str_cstr (p),
			text
		);
	}

	ml_str_free (&p);
	return same;
}

static void test_screen (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	ml_editor_init (&ed);
	ml_screen_init (&s, 40, 5);

	/*
	 * the ghost text follows the typed text in a dim style; the cursor stays
	 * after the typed text
	 */
	test_normal (&ed, ":mod");
	l = ml_screen_layout (&s, &ed);
	CHECK (plain_row_is (&l, 4, ":mode"));
	CHECK (
		strstr (ml_str_cstr (l.rows.data [4]), "mod\x1b[2me\x1b[0m") != NULL
	);
	CHECK (l.cursor.row == 5 && l.cursor.col == 5);
	CHECK (plain_row_is (&l, 3, ""));
	ml_layout_free (&l);

	/* with two or more candidates they are listed above the command line */
	press (&ed, ML_KEY_TAB);
	l = ml_screen_layout (&s, &ed);
	CHECK (
		plain_row_is (&l, 3, "  algebra  lambda  pga2  pga3  boolean")
	);
	CHECK (plain_row_is (&l, 4, ":mode "));
	ml_layout_free (&l);

	/* narrowing the list: the first candidate is marked as the suggestion */
	type (&ed, "p");
	l = ml_screen_layout (&s, &ed);
	CHECK (plain_row_is (&l, 3, "  pga2  pga3"));
	CHECK (
		strstr (ml_str_cstr (l.rows.data [3]), "\x1b[7m  pga2\x1b[0m") != NULL
	);
	CHECK (plain_row_is (&l, 4, ":mode pga2"));
	ml_layout_free (&l);

	/* one candidate left: no list any more */
	type (&ed, "ga3");
	l = ml_screen_layout (&s, &ed);
	CHECK (plain_row_is (&l, 3, ""));
	ml_layout_free (&l);

	/* none left */
	type (&ed, "x");
	l = ml_screen_layout (&s, &ed);
	CHECK (plain_row_is (&l, 3, ""));
	CHECK (plain_row_is (&l, 4, ":mode pga3x"));
	ml_layout_free (&l);

	/* a narrow terminal clips the list and the ghost text */
	ml_screen_resize (&s, 12, 5);
	press (&ed, ML_KEY_ESCAPE);
	test_normal (&ed, ":mode ");
	l = ml_screen_layout (&s, &ed);
	CHECK (plain_row_is (&l, 3, "  algebra  l"));
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
}

static MlCompletion math (const MlEnv *env, const char *text)
{
	return ml_complete_math (env, ml_env_snapshot (env), lit (text));
}

static void test_math (void)
{
	MlEnv        env;
	MlArena      arena;
	MlCompletion c;
	MlParseResult p;
	MlSlice      def = lit ("speed = 3");

	ml_env_init (&env);
	ml_arena_init (&arena, 0);
	p = ml_parse (def);
	ml_eval_commit (
		&env,
		def,
		&p.ast,
		ml_eval (&arena, &env, ml_env_snapshot (&env), def, &p.ast)
	);
	ml_ast_free (&p.ast);

	/* variables, functions, constants, units; the rest of the first one */
	c = math (&env, "sp");
	CHECK (c.count == 1 && has_item (&c, "speed") && ghost_is (c, "eed"));
	c = math (&env, "2 + sq");
	CHECK (has_item (&c, "sqrt") && ghost_is (c, "rt"));
	c = math (&env, "ps");
	CHECK (c.count == 0 && c.ghost >= ML_MAX_CANDIDATES);
	c = math (&env, "5 ki");
	CHECK (has_item (&c, "kilogram") == 0 && c.count == 0);
	c = math (&env, "3 ft to mi");
	CHECK (
		   has_item (&c, "mile")
		&& has_item (&c, "min")
		&& !has_item (&c, "mi")
	);
	c = math (&env, "2 zzz");
	CHECK (c.count == 0);
	c = math (&env, "p");
	CHECK (
		   has_item (&c, "pi")
		&& has_item (&c, "pascal")
		&& has_item (&c, "pound")
	);
	c = math (&env, "di");
	CHECK (has_item (&c, "dim") && has_item (&c, "diff"));
	c = math (&env, "1/he");
	CHECK (has_item (&c, "hectare") && has_item (&c, "henry"));
	c = math (&env, "12 N*mete");
	CHECK (ghost_is (c, "r"));

	/* the word is the name at the end: digits in front are a number */
	c = math (&env, "2sp");
	CHECK (c.token_start == 1 && has_item (&c, "speed"));
	c = math (&env, "2sp ");
	CHECK (c.count == 0);
	c = math (&env, "");
	CHECK (c.count == 0);
	c = math (&env, "3 α" "x");
	CHECK (c.token_start == 2);

	/* the environment as of the snapshot: later names are not offered */
	c = ml_complete_math (&env, 0, lit ("sp"));
	CHECK (c.count == 0);

	/* each name once, the newest binding first */
	{
		MlSlice again = lit ("speed = 4");

		p = ml_parse (again);
		ml_eval_commit (
			&env,
			again,
			&p.ast,
			ml_eval (&arena, &env, ml_env_snapshot (&env), again, &p.ast)
		);
		ml_ast_free (&p.ast);
	}

	c = math (&env, "spe");
	CHECK (c.count == 1);

	CHECK (ml_unit_name_count () > 50);
	CHECK (ml_slice_equal (ml_unit_name_at (0), lit ("m")));
	CHECK (ml_slice_equal (ml_unit_name_at (1), lit ("meter")));
	CHECK (ml_unit_name_at (ml_unit_name_count ()).len == 0);
	CHECK (ml_slice_equal (ml_function_name_at (0), lit ("sqrt")));
	CHECK (ml_function_name_at (ml_function_name_count ()).len == 0);

	ml_arena_free (&arena);
	ml_env_free (&env);
}

static void test_theme_args (void)
{
	MlSlice      files = lit (
		"solar.theme\ndark.theme\nnotes.txt\nsea.theme\n"
	);
	MlCompletion c     = ml_complete_command (lit ("theme "));

	/* the built in themes and reload */
	CHECK (has_item (&c, "dark") && has_item (&c, "mono"));
	CHECK (has_item (&c, "reload") && !has_item (&c, "solar"));

	/* the theme files of the user come with them, once, without the suffix */
	c = ml_complete_command_with (lit ("theme "), files);
	CHECK (has_item (&c, "solar") && has_item (&c, "sea"));
	CHECK (!has_item (&c, "notes") && !has_item (&c, "notes.txt"));
	CHECK (!has_item (&c, "solar.theme"));
	CHECK (c.count == 4 + 1 + 2);

	/* typing narrows them, and the rest becomes the ghost text */
	c = ml_complete_command_with (lit ("theme so"), files);
	CHECK (c.count == 1 && ghost_is (c, "lar"));

	/* other commands do not get them */
	c = ml_complete_command_with (lit ("mode "), files);
	CHECK (!has_item (&c, "solar"));
}

void run_complete_tests (void)
{
	test_theme_args ();
	test_commands ();
	test_arguments ();
	test_editor_keys ();
	test_screen ();
	test_math ();
}
