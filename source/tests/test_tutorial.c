#include "test.h"
#include "test_keys.h"

#include <matlog/platform/fs.h>
#include <matlog/tutorial/lessons.h>
#include <matlog/ui/screen.h>
#include <matlog/ui/tutorialpanel.h>

#include <stdlib.h>
#include <string.h>

#define CONFIG_DIR  "matlog_test_config/tutorial"
#define CONFIG_FILE "matlog_test_config/tutorial/settings.mlcfg"

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
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

/* Presses the keys of a script, and the ones the editor asks for. */
static void press_all (MlEditor *ed, const MlKeyVec *keys)
{
	MlKey asked;

	for (uint64_t i = 0; i < keys->len; i++)
	{
		CHECK (ml_editor_handle_key (ed, keys->data [i]) == ML_OK);

		while (ml_editor_take_pending (ed, &asked))
		{
			CHECK (ml_editor_handle_key (ed, asked) == ML_OK);
		}
	}
}

static void test_lessons_are_data (void)
{
	CHECK (ml_lesson_count () >= 10);
	CHECK (ml_lesson_first (1) == 0);
	CHECK (ml_lesson_first (2) > 0 && ml_lesson_first (2) < ml_lesson_count ());
	CHECK (ml_lesson_find (lit ("math")) < ml_lesson_count ());
	CHECK (ml_lesson_find (lit ("nothing")) == ml_lesson_count ());

	for (uint64_t l = 0; l < ml_lesson_count (); l++)
	{
		MlLesson lesson = ml_lesson_at (l);

		CHECK (strlen (lesson.id) > 0 && strlen (lesson.title) > 0);
		CHECK (lesson.step_count > 0);
		CHECK ((l < ml_lesson_first (2)) == (lesson.stage == 1));

		for (uint64_t s = 0; s < lesson.step_count; s++)
		{
			MlKeyVec keys;

			MlKeyVec_init (&keys);
			CHECK (strlen (lesson.steps [s].text) > 10);
			CHECK (strlen (lesson.steps [s].text) < 220);
			CHECK (ml_tutorial_answer (&keys, lesson.steps [s]) == ML_OK);
			CHECK (keys.len > 0);
			MlKeyVec_free (&keys);
		}
	}
}

static void test_answer_script (void)
{
	MlKeyVec keys;
	MlStep   step;

	MlKeyVec_init (&keys);
	memset (&step, 0, sizeof step);
	step.answer = "a<<b<Ctrl+Enter><Up>\xCE\xBB";
	CHECK (ml_tutorial_answer (&keys, step) == ML_OK);
	CHECK (keys.len == 6);
	CHECK (keys.data [0].code == ML_KEY_CHAR && keys.data [0].cp == 'a');
	CHECK (keys.data [1].code == ML_KEY_CHAR && keys.data [1].cp == '<');
	CHECK (keys.data [2].code == ML_KEY_CHAR && keys.data [2].cp == 'b');
	CHECK (keys.data [3].code == ML_KEY_ENTER);
	CHECK (keys.data [3].mods == ML_MOD_CTRL);
	CHECK (keys.data [4].code == ML_KEY_UP);
	CHECK (keys.data [5].cp == 0x3BB);

	/* a key that does not exist is an error */
	MlKeyVec_free (&keys);
	MlKeyVec_init (&keys);
	step.answer = "<Nonsense>";
	CHECK (ml_tutorial_answer (&keys, step) == ML_ERR_PARSE);
	step.answer = "<Enter";
	CHECK (ml_tutorial_answer (&keys, step) == ML_ERR_PARSE);
	MlKeyVec_free (&keys);
}

/* Every step of every lesson can be finished with its model answer. */
static void test_every_lesson (void)
{
	MlEditor ed;

	use_test_config ();
	ml_editor_init (&ed);
	CHECK (ml_editor_start_tutorial (&ed, lit ("")) == ML_OK);
	CHECK (ed.tutorial.active);
	CHECK (ed.tutorial.lesson == 0 && ed.tutorial.step == 0);

	for (uint64_t l = 0; l < ml_lesson_count () && ed.tutorial.active; l++)
	{
		MlLesson lesson = ml_lesson_at (l);

		for (uint64_t s = 0; s < lesson.step_count && ed.tutorial.active; s++)
		{
			MlKeyVec keys;
			uint64_t i;

			MlKeyVec_init (&keys);
			CHECK (ed.tutorial.active);
			CHECK (ed.tutorial.lesson == l && ed.tutorial.step == s);
			CHECK (ml_tutorial_answer (&keys, lesson.steps [s]) == ML_OK);

			/* the step is not done before its last key */
			for (i = 0; i + 1 < keys.len && ed.tutorial.active; i++)
			{
				CHECK (ml_editor_handle_key (&ed, keys.data [i]) == ML_OK);
				CHECK (ed.tutorial.lesson == l && ed.tutorial.step == s);
			}

			if (ed.tutorial.active)
			{
				MlKey last = keys.data [keys.len - 1];

				CHECK (ml_editor_handle_key (&ed, last) == ML_OK);
			}

			int32_t moved = (
				   ed.tutorial.finished
				|| ed.tutorial.step   != s
				|| ed.tutorial.lesson != l
			);

			if (!moved)
			{
				printf (
					"  lesson %s step %d did not finish: %s\n",
					lesson.id,
					(int)s + 1,
					lesson.steps [s].text
				);
			}

			CHECK (moved);

			/* a step that cannot be finished makes the rest meaningless */
			if (!moved)
			{
				ml_tutorial_stop (&ed.tutorial);
			}

			MlKeyVec_free (&keys);
		}
	}

	CHECK (ed.tutorial.finished && !ed.tutorial.active);
	ml_editor_free (&ed);
	ml_fs_remove (lit (CONFIG_FILE));
}

static void test_control (void)
{
	MlEditor ed;
	MlKey    f6   = { .code = ML_KEY_F6, .cp = 0, .mods = 0 };
	MlKey    back = { .code = ML_KEY_F6, .cp = 0, .mods = ML_MOD_SHIFT };

	use_test_config ();
	ml_editor_init (&ed);
	CHECK (ml_editor_start_tutorial (&ed, lit ("")) == ML_OK);

	/* a wrong try is never an error, and after a few keys there is a hint */
	CHECK (!ml_tutorial_hint_due (&ed.tutorial));
	test_text (&ed, "zzzzzzzz");
	CHECK (ed.tutorial.step == 0);
	CHECK (ml_tutorial_hint_due (&ed.tutorial));

	/* F6 skips, Shift+F6 goes back, and neither counts as a try */
	CHECK (ml_editor_handle_key (&ed, f6) == ML_OK);
	CHECK (ed.tutorial.step == 1 && ed.tutorial.keys == 0);
	CHECK (ml_editor_handle_key (&ed, back) == ML_OK);
	CHECK (ed.tutorial.step == 0);
	CHECK (ml_editor_handle_key (&ed, back) == ML_OK);
	CHECK (ed.tutorial.lesson == 0 && ed.tutorial.step == 0);

	/* the commands do the same (leaving the note by Esc may finish step 0) */
	test_key (&ed, ML_KEY_ESCAPE, 0, 0);
	{
		uint64_t before = ed.tutorial.step;

		test_normal (&ed, ":tutorial next");
		test_key (&ed, ML_KEY_ENTER, 0, 0);
		CHECK (ed.tutorial.step == before + 1);
		test_normal (&ed, ":tutorial back");
		test_key (&ed, ML_KEY_ENTER, 0, 0);
		CHECK (ed.tutorial.step == before);
	}

	test_normal (&ed, ":tutorial reset");
	test_key (&ed, ML_KEY_ENTER, 0, 0);
	uint64_t shown = ed.tutorial.step;

	/* :tutorial show types the model answer and the step is done */
	test_normal (&ed, ":tutorial show");
	test_key (&ed, ML_KEY_ENTER, 0, 0);
	CHECK (ed.pending.len > 0);
	{
		MlKey asked;

		while (ml_editor_take_pending (&ed, &asked))
		{
			CHECK (ml_editor_handle_key (&ed, asked) == ML_OK);
		}
	}

	CHECK (ed.tutorial.step == shown + 1);
	CHECK (ed.session.lines.len >= 1);

	/* :tutorial off leaves; the session stays */
	test_normal (&ed, ":tutorial off");
	test_key (&ed, ML_KEY_ENTER, 0, 0);
	CHECK (!ed.tutorial.active);
	CHECK (ed.session.lines.len >= 1);
	ml_editor_free (&ed);
	ml_fs_remove (lit (CONFIG_FILE));
}

static void test_progress (void)
{
	MlEditor ed;
	MlStr    text;

	use_test_config ();
	ml_editor_init (&ed);
	ml_str_init (&text);
	CHECK (ml_editor_start_tutorial (&ed, lit ("")) == ML_OK);

	/* finish the first lesson with its answers */
	{
		MlLesson lesson = ml_lesson_at (0);

		for (uint64_t s = 0; s < lesson.step_count; s++)
		{
			MlKeyVec keys;

			MlKeyVec_init (&keys);
			CHECK (ml_tutorial_answer (&keys, lesson.steps [s]) == ML_OK);
			press_all (&ed, &keys);
			MlKeyVec_free (&keys);
		}
	}

	CHECK (ed.tutorial.lesson == 1);
	CHECK (ml_fs_read_file (&text, lit (CONFIG_FILE)) == ML_OK);
	CHECK (strstr (ml_str_cstr (text), "[tutorial]\nlesson = notes\n") != NULL);

	/* a new start goes on with the next lesson */
	CHECK (ml_editor_start_tutorial (&ed, lit ("")) == ML_OK);
	CHECK (ed.tutorial.lesson == 1 && ed.tutorial.step == 0);

	/* syntax starts the second stage */
	CHECK (ml_editor_start_tutorial (&ed, lit ("syntax")) == ML_OK);
	CHECK (ed.tutorial.lesson == ml_lesson_first (2));

	/* reset forgets */
	test_normal (&ed, ":tutorial reset");
	test_key (&ed, ML_KEY_ENTER, 0, 0);
	uint64_t shown = ed.tutorial.step;
	CHECK (ml_editor_start_tutorial (&ed, lit ("")) == ML_OK);
	CHECK (ed.tutorial.lesson == 0);

	/* the command will not drop a session with changes without a word */
	test_leave (&ed);
	test_text (&ed, "keep me");
	test_leave (&ed);
	ed.session.dirty = 1;
	test_normal (&ed, ":tutorial");
	test_key (&ed, ML_KEY_ENTER, 0, 0);
	CHECK (strstr (ml_str_cstr (ed.message), "changes") != NULL);
	CHECK (ed.session.lines.len >= 1);
	test_normal (&ed, ":tutorial!");
	test_key (&ed, ML_KEY_ENTER, 0, 0);
	CHECK (ed.session.lines.len == 0);

	ml_str_free (&text);
	ml_editor_free (&ed);
	ml_fs_remove (lit (CONFIG_FILE));
}

static void test_panel (void)
{
	MlEditor ed;
	MlScreen s;
	MlLayout l;

	use_test_config ();
	ml_editor_init (&ed);
	ml_screen_init (&s, 80, 20);
	CHECK (ml_editor_start_tutorial (&ed, lit ("")) == ML_OK);

	/* the panel is above the lines: the title, the task, how to leave */
	l = ml_screen_layout (&s, &ed);
	CHECK (l.result == ML_OK && l.rows.len == 20);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "Tutorial  1 of") != NULL);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "Writing and notes") != NULL);
	CHECK (strstr (ml_str_cstr (l.rows.data [1]), "Welcome. A new") != NULL);
	CHECK (strstr (ml_str_cstr (l.rows.data [4]), "F6 skip") != NULL);

	/* the cursor is in the lines below it */
	CHECK (l.cursor.row > ML_TUTORIAL_PANEL_ROWS);
	ml_layout_free (&l);

	/* after a few useless keys the hint takes the place of the controls */
	test_text (&ed, "xxxxxxxx");
	l = ml_screen_layout (&s, &ed);
	CHECK (strstr (ml_str_cstr (l.rows.data [4]), "Hint: ") != NULL);
	ml_layout_free (&l);

	/* a small terminal has no room for it */
	ml_screen_resize (&s, 80, 8);
	l = ml_screen_layout (&s, &ed);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "Tutorial") == NULL);
	ml_layout_free (&l);

	/* off: the panel is gone */
	ml_screen_resize (&s, 80, 20);
	ml_tutorial_stop (&ed.tutorial);
	l = ml_screen_layout (&s, &ed);
	CHECK (strstr (ml_str_cstr (l.rows.data [0]), "Tutorial") == NULL);
	ml_layout_free (&l);

	ml_screen_free (&s);
	ml_editor_free (&ed);
	ml_fs_remove (lit (CONFIG_FILE));
}

void run_tutorial_tests (void);

void run_tutorial_tests (void)
{
	test_lessons_are_data ();
	test_answer_script ();
	test_every_lesson ();
	test_control ();
	test_progress ();
	test_panel ();
}
