#include "test.h"
#include "test_keys.h"

#include <string.h>

void test_key (MlEditor *ed, MlKeyCode code, uint32_t cp, uint32_t mods)
{
	MlKey k =
	{
		.code = code,
		.cp   = cp,
		.mods = mods
	};

	CHECK (ml_editor_handle_key (ed, k) == ML_OK);
}

void test_leave (MlEditor *ed)
{
	/* the line is written into the session, as Esc did */
	CHECK (ml_editor_commit (ed) == ML_OK);

	if (
		   (ed->state == ML_STATE_MATH || ed->state == ML_STATE_NOTE)
		&& !ed->edit_existing
		&& ed->edit.text.len == 0
		&& ed->session.lines.len > 0
	)
	{
		test_key (ed, ML_KEY_UP, 0, 0);
	}
}

int32_t test_editing (const MlEditor *ed)
{
	return ed->state == ML_STATE_MATH || ed->state == ML_STATE_NOTE;
}

void test_text (MlEditor *ed, const char *text)
{
	for (uint64_t i = 0; text [i] != '\0'; i++)
	{
		test_key (ed, ML_KEY_CHAR, (uint8_t)text [i], 0);
	}
}

static void ctrl (MlEditor *ed, uint32_t letter)
{
	test_key (ed, ML_KEY_CHAR, letter, ML_MOD_CTRL);
}

static void again (MlEditor *ed, MlKeyCode code, uint32_t mods, uint64_t n)
{
	for (uint64_t i = 0; i < n; i++)
	{
		test_key (ed, code, 0, mods);
	}
}

/* Goes to the line number n (1 based), or the first one for 0. */
static void go_line (MlEditor *ed, uint64_t n)
{
	test_key (ed, ML_KEY_HOME, 0, ML_MOD_CTRL);
	again (ed, ML_KEY_DOWN, 0, ml_choose (n > 0, n - 1, 0));
}

static const char *domain_name_of (char key)
{
	const char *name = "algebra";

	name = ml_choose (key == 'e', "equations", name);
	name = ml_choose (key == 'm', "matrix", name);
	name = ml_choose (key == 'v', "vector", name);
	name = ml_choose (key == 'l', "lambda", name);
	name = ml_choose (key == 'b', "boolean", name);
	name = ml_choose (key == '2', "pga2", name);
	name = ml_choose (key == '3', "pga3", name);
	return name;
}

static void command (MlEditor *ed, const char *text)
{
	ctrl (ed, 'p');
	test_text (ed, text);
	test_key (ed, ML_KEY_ENTER, 0, 0);
}

static int32_t in_note (const MlEditor *ed)
{
	return ed->state == ML_STATE_NOTE;
}

void test_normal (MlEditor *ed, const char *keys)
{
	uint64_t n         = 0;
	int32_t  selecting = 0;
	uint64_t len       = strlen (keys);
	uint64_t i         = 0;

	while (i < len)
	{
		char     c    = keys [i];
		char     next = ml_choose (i + 1 < len, keys [i + 1], '\0');
		uint64_t reps = ml_choose (n > 0, n, 1);
		uint64_t used = 1;
		uint32_t shift;

		selecting = ed->state == ML_STATE_SELECT;
		shift     = ml_choose (selecting, ML_MOD_SHIFT, 0);

		if (c >= '1' && c <= '9')
		{
			n = n * 10 + (uint64_t)(c - '0');
		}

		else if (c == '0' && n > 0)
		{
			n = n * 10;
		}

		else
		{
			if (c == 'j')
			{
				again (ed, ML_KEY_DOWN, shift, reps);
			}

			else if (c == 'k')
			{
				again (ed, ML_KEY_UP, shift, reps);
			}

			else if (c == 'G' && n > 0)
			{
				go_line (ed, n);
			}

			else if (c == 'G')
			{
				test_key (ed, ML_KEY_END, 0, ML_MOD_CTRL);
			}

			else if (c == 'g' && next == 'g')
			{
				go_line (ed, n);
				used = 2;
			}

			else if (c == 'g')
			{
				ctrl (ed, 'p');
				test_text (ed, "mode ");
				test_text (ed, domain_name_of (next));
				test_key (ed, ML_KEY_ENTER, 0, 0);
				used = 2;
			}

			else if (c == 'd' && next == 'd')
			{
				if (reps > 1)
				{
					ctrl (ed, 'l');
					again (ed, ML_KEY_DOWN, ML_MOD_SHIFT, reps - 1);
				}

				ctrl (ed, 'x');
				used = 2;
			}

			else if (c == 'y' && next == 'y')
			{
				if (reps > 1)
				{
					ctrl (ed, 'l');
					again (ed, ML_KEY_DOWN, ML_MOD_SHIFT, reps - 1);
				}

				ctrl (ed, 'c');

				if (reps > 1)
				{
					test_key (ed, ML_KEY_ESCAPE, 0, 0);
				}

				used = 2;
			}

			else if (c == 'v' && selecting)
			{
				test_key (ed, ML_KEY_ESCAPE, 0, 0);
			}

			else if (c == 'v')
			{
				ctrl (ed, 'l');
			}

			else if (selecting && (c == 'd' || c == 'x'))
			{
				ctrl (ed, 'x');
			}

			else if (selecting && c == 'y')
			{
				ctrl (ed, 'c');
				test_key (ed, ML_KEY_ESCAPE, 0, 0);
			}

			else if (c == 'p')
			{
				ctrl (ed, 'v');
			}

			else if (c == 'u')
			{
				ctrl (ed, 'z');
			}

			else if (c == 'o')
			{
				test_key (ed, ML_KEY_END, 0, 0);
				test_key (ed, ML_KEY_ENTER, 0, ML_MOD_CTRL);
				test_key (ed, ML_KEY_ENTER, 0, 0);
			}

			else if (c == 'N' || c == 'i' || c == 'n')
			{
				int32_t fresh = !ed->edit_existing && ed->edit.text.len == 0;

				/* a new note below, or a line above */
				if (!fresh)
				{
					test_key (
						ed,
						ml_choose (c == 'N', ML_KEY_END, ML_KEY_HOME),
						0,
						0
					);
					test_key (ed, ML_KEY_ENTER, 0, 0);

					if (c != 'N')
					{
						test_key (ed, ML_KEY_UP, 0, 0);
					}
				}

				if (in_note (ed) != (c != 'i'))
				{
					ctrl (ed, 'n');
				}
			}

			else if (c == 'z' && next == 'a')
			{
				test_key (ed, ML_KEY_ENTER, 0, ML_MOD_ALT);
				used = 2;
			}

			else if (c == 'z' && (next == 'c' || next == 'o'))
			{
				command (ed, ml_choose (next == 'c', "fold", "unfold"));
				used = 2;
			}

			else if (c == 'z' && next == 'R')
			{
				command (ed, "unfoldall");
				used = 2;
			}

			else if (c == 'z' && next == 'M')
			{
				command (ed, "foldall");
				used = 2;
			}

			else if (c == ']' && next == ']')
			{
				test_key (ed, ML_KEY_DOWN, 0, ML_MOD_CTRL);
				used = 2;
			}

			else if (c == '[' && next == '[')
			{
				test_key (ed, ML_KEY_UP, 0, ML_MOD_CTRL);
				used = 2;
			}

			else if (c == 's')
			{
				test_key (ed, ML_KEY_F3, 0, 0);
			}

			else if (c == ':')
			{
				ctrl (ed, 'p');
				test_text (ed, keys + i + 1);
				used = len - i;
			}

			n = 0;
		}

		i += used;
	}
}
