#include "test.h"

#include <matlog/platform/fs.h>
#include <matlog/ui/editor.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static MlKey key_char (uint32_t cp)
{
	MlKey k = { .code = ML_KEY_CHAR, .cp = cp, .mods = 0 };

	return k;
}

static MlKey key_code (MlKeyCode code)
{
	MlKey k = { .code = code, .cp = 0, .mods = 0 };

	return k;
}

/* Presses the ASCII characters of text one after the other. */
static void type (MlEditor *ed, const char *text)
{
	for (uint64_t i = 0; text [i] != '\0'; i++)
	{
		CHECK (
			ml_editor_handle_key (ed, key_char ((uint8_t)text [i])) == ML_OK
		);
	}
}

static void press (MlEditor *ed, MlKeyCode code)
{
	CHECK (ml_editor_handle_key (ed, key_code (code)) == ML_OK);
}

static int32_t line_is (
	const MlEditor *ed,
	uint64_t       idx,
	MlLineKind     kind,
	MlDomain       domain,
	const char     *text
)
{
	MlLineView v = ml_session_get (&ed->session, idx);

	return (
		   v.kind == kind
		&& v.domain == domain
		&& ml_slice_equal (v.text, lit (text))
	);
}

static int32_t status_is (const MlEditor *ed, const char *expected)
{
	MlStr   s;
	int32_t same;

	ml_str_init (&s);
	same = ml_editor_status (&s, ed) == ML_OK
		&& ml_slice_equal (ml_str_view (s), lit (expected));

	if (!same)
	{
		printf (
			"  status \"%s\", expected \"%s\"\n",
			ml_str_cstr (s),
			expected
		);
	}

	ml_str_free (&s);
	return same;
}

static void test_insert (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	CHECK (ed.state == ML_STATE_INSERT);
	CHECK (status_is (&ed, "-- INSERT -- algebra>"));

	type (&ed, "1+2");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_INSERT);
	CHECK (ed.session.lines.len == 1);
	CHECK (line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "1+2"));

	/* the rewrites are active, also when the line ends right after a token */
	type (&ed, "x^2");
	press (&ed, ML_KEY_ENTER);
	CHECK (line_is (&ed, 1, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "x\xC2\xB2"));

	/* Enter on an empty line writes a blank line */
	press (&ed, ML_KEY_ENTER);
	CHECK (line_is (&ed, 2, ML_LINE_BLANK, ML_DOMAIN_ALGEBRA, ""));

	/* Esc commits what was typed and returns to Normal */
	type (&ed, "y<=3");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ed.state == ML_STATE_NORMAL);
	CHECK (ed.session.lines.len == 4);
	CHECK (
		line_is (&ed, 3, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "y\xE2\x89\xA4" "3")
	);
	CHECK (ed.session.cursor == 3);
	CHECK (status_is (&ed, "-- NORMAL -- algebra>"));

	/* Esc on an empty new line adds nothing */
	type (&ed, "i");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ed.session.lines.len == 4);

	/* backspace undoes a rewrite just made */
	type (&ed, "i");
	type (&ed, "a<=");
	press (&ed, ML_KEY_BACKSPACE);
	press (&ed, ML_KEY_ESCAPE);
	CHECK (line_is (&ed, 4, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "a<="));

	ml_editor_free (&ed);
}

static void test_note (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "1+1");
	press (&ed, ML_KEY_ESCAPE);

	/* n starts a note below the cursor line; nothing is rewritten in it */
	type (&ed, "n");
	CHECK (ed.state == ML_STATE_NOTE);
	CHECK (status_is (&ed, "-- NOTE -- algebra>"));
	type (&ed, "x^2 <= pi ");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_NOTE);
	type (&ed, "second");
	press (&ed, ML_KEY_ESCAPE);

	CHECK (ed.session.lines.len == 3);
	CHECK (line_is (&ed, 1, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "x^2 <= pi "));
	CHECK (line_is (&ed, 2, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "second"));

	ml_editor_free (&ed);
}

static void test_navigation (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "a");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "b");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "c");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ed.session.cursor == 2);

	type (&ed, "k");
	CHECK (ed.session.cursor == 1);
	type (&ed, "k");
	type (&ed, "k");
	CHECK (ed.session.cursor == 0);
	type (&ed, "j");
	CHECK (ed.session.cursor == 1);
	press (&ed, ML_KEY_DOWN);
	press (&ed, ML_KEY_DOWN);
	CHECK (ed.session.cursor == 2);
	press (&ed, ML_KEY_UP);
	CHECK (ed.session.cursor == 1);

	type (&ed, "G");
	CHECK (ed.session.cursor == 2);

	/* :N goes to line N (counting from 1), clamped to the last line */
	type (&ed, ":2");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 1);
	type (&ed, ":1");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 0);
	type (&ed, ":99");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 2);
	type (&ed, ":0");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 0);
	type (&ed, ":2zz");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 0);
	CHECK (status_is (&ed, "-- NORMAL -- algebra>  unknown command"));
	type (&ed, "G");
	type (&ed, "gg");
	CHECK (ed.session.cursor == 0);

	ml_editor_free (&ed);
}

static void test_yank_paste (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "a");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "b");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "c");
	press (&ed, ML_KEY_ESCAPE);

	/* yy copies, p pastes below the cursor */
	type (&ed, "kyy");
	CHECK (ed.yank.len == 1 && ed.yank.data [0].kind == ML_LINE_MATH);
	type (&ed, "p");
	CHECK (ed.session.lines.len == 4);
	CHECK (line_is (&ed, 2, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "b"));
	CHECK (ed.session.cursor == 2);

	/* dd removes the line and keeps it in the register */
	type (&ed, "dd");
	CHECK (ed.session.lines.len == 3);
	CHECK (line_is (&ed, 2, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "c"));

	/* P pastes above */
	type (&ed, "gg");
	type (&ed, "P");
	CHECK (ed.session.lines.len == 4);
	CHECK (line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "b"));
	CHECK (line_is (&ed, 1, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "a"));

	/* a lone d or y is dropped by the next key */
	type (&ed, "dx");
	type (&ed, "yj");
	CHECK (ed.session.lines.len == 4);
	CHECK (ed.pending == 0);

	/* dd on an empty session is harmless */
	ml_editor_free (&ed);
	ml_editor_init (&ed);
	type (&ed, "dd");
	type (&ed, "p");
	CHECK (ed.session.lines.len == 0);

	ml_editor_free (&ed);
}

static void test_edit_line (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	type (&ed, ":mode matrix");
	press (&ed, ML_KEY_ENTER);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "A = 1");
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, "gl");
	type (&ed, "i");
	type (&ed, "x");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_MATRIX, "A = 1"));
	CHECK (line_is (&ed, 1, ML_LINE_MATH, ML_DOMAIN_LAMBDA, "x"));

	/* Enter edits the cursor line; it keeps its own domain */
	type (&ed, "gg");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_INSERT);
	CHECK (ed.edit.cursor == 5);
	CHECK (status_is (&ed, "-- INSERT -- matrix>"));
	type (&ed, "2");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_NORMAL);
	CHECK (ed.session.lines.len == 2);
	CHECK (line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_MATRIX, "A = 12"));
	CHECK (ed.session.domain == ML_DOMAIN_LAMBDA);

	/* an edited note stays a note */
	type (&ed, "n");
	type (&ed, "memo");
	press (&ed, ML_KEY_ESCAPE);
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_NOTE);
	type (&ed, "!");
	press (&ed, ML_KEY_ENTER);
	CHECK (line_is (&ed, 1, ML_LINE_NOTE, ML_DOMAIN_LAMBDA, "memo!"));

	/* clearing a math line makes it blank */
	type (&ed, "gg");
	press (&ed, ML_KEY_ENTER);
	ml_lineedit_clear (&ed.edit);
	press (&ed, ML_KEY_ENTER);
	CHECK (ml_session_get (&ed.session, 0).kind == ML_LINE_BLANK);

	ml_editor_free (&ed);
}

static void test_domains (void)
{
	MlEditor ed;
	uint32_t i;

	/* g + key */
	static const char     keys []    = "aemvlb23";
	static const MlDomain domains [] =
	{
		ML_DOMAIN_ALGEBRA, ML_DOMAIN_EQUATIONS, ML_DOMAIN_MATRIX,
		ML_DOMAIN_VECTOR, ML_DOMAIN_LAMBDA, ML_DOMAIN_BOOLEAN,
		ML_DOMAIN_PGA2, ML_DOMAIN_PGA3
	};

	ml_editor_init (&ed);

	for (i = 0; i < 8; i++)
	{
		type (&ed, "g");
		CHECK (ed.pending == 'g');
		CHECK (
			ml_editor_handle_key (&ed, key_char ((uint8_t)keys [i])) == ML_OK
		);
		CHECK (ed.session.domain == domains [i]);
	}

	type (&ed, "gz");
	CHECK (status_is (&ed, "-- NORMAL -- pga3>  g: unknown domain key"));
	type (&ed, "j");
	CHECK (status_is (&ed, "-- NORMAL -- pga3>"));

	/* the status line follows the domain */
	type (&ed, "gm");
	CHECK (status_is       (&ed, "-- NORMAL -- matrix>  matrix"));
	CHECK (ml_editor_begin (&ed) == ML_OK);
	CHECK (status_is       (&ed, "-- INSERT -- matrix>"));

	/* new lines use it, and the rewriter follows it */
	type (&ed, "A^2 ");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_MATRIX, "A\xC2\xB2 "));

	type (&ed, "g3");
	type (&ed, "i");
	type (&ed, "e1^2 ");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (line_is (&ed, 1, ML_LINE_MATH, ML_DOMAIN_PGA3, "e1^2 "));

	ml_editor_free (&ed);
}

static void test_domain_cycle (void)
{
	MlEditor ed;
	MlKey    right = { .code = ML_KEY_RIGHT, .cp = 0, .mods = ML_MOD_ALT };
	MlKey    left  = { .code = ML_KEY_LEFT, .cp = 0, .mods = ML_MOD_ALT };

	ml_editor_init (&ed);

	/* Normal: Alt+Right goes forward through the domains and wraps around */
	CHECK (ml_editor_handle_key (&ed, right) == ML_OK);
	CHECK (ed.session.domain == ML_DOMAIN_EQUATIONS);
	CHECK (status_is            (&ed, "-- NORMAL -- equations>  equations"));
	CHECK (ml_editor_handle_key (&ed, left) == ML_OK);
	CHECK (ed.session.domain == ML_DOMAIN_ALGEBRA);
	CHECK (ml_editor_handle_key (&ed, left) == ML_OK);
	CHECK (ed.session.domain == ML_DOMAIN_BOOLEAN);
	CHECK (ml_editor_handle_key (&ed, right) == ML_OK);
	CHECK (ed.session.domain == ML_DOMAIN_ALGEBRA);

	for (uint32_t i = 0; i < ML_DOMAIN_COUNT; i++)
	{
		CHECK (ml_editor_handle_key (&ed, right) == ML_OK);
	}

	CHECK (ed.session.domain == ML_DOMAIN_ALGEBRA);

	/* Insert: the line being typed changes domain, so do the next lines */
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "a^2 ");
	CHECK (ml_editor_handle_key (&ed, right) == ML_OK);
	CHECK (ml_editor_handle_key (&ed, right) == ML_OK);
	CHECK (ed.edit_domain == ML_DOMAIN_MATRIX);
	CHECK (status_is (&ed, "-- INSERT -- matrix>  matrix"));
	type (&ed, "b^2 ");
	press (&ed, ML_KEY_ENTER);
	CHECK (
		line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_MATRIX, "a\xC2\xB2 b\xC2\xB2 ")
	);
	CHECK (ed.session.domain == ML_DOMAIN_MATRIX);
	CHECK (ed.edit_domain == ML_DOMAIN_MATRIX);

	/* the rewrites follow the domain: ^ is an operator in the boolean domain */
	CHECK (ml_editor_handle_key (&ed, left) == ML_OK);
	CHECK (ml_editor_handle_key (&ed, left) == ML_OK);
	CHECK (ml_editor_handle_key (&ed, left) == ML_OK);
	CHECK (ed.edit_domain == ML_DOMAIN_BOOLEAN);
	type (&ed, "c^2 ");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (line_is (&ed, 1, ML_LINE_MATH, ML_DOMAIN_BOOLEAN, "c^2 "));

	/* editing an old line changes only that line */
	type (&ed, "gg");
	press (&ed, ML_KEY_ENTER);
	CHECK (ml_editor_handle_key (&ed, right) == ML_OK);
	press (&ed, ML_KEY_ENTER);
	CHECK (
		line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_VECTOR, "a\xC2\xB2 b\xC2\xB2 ")
	);
	CHECK (ed.session.domain == ML_DOMAIN_BOOLEAN);

	/* notes have no domain to change */
	type (&ed, "n");
	CHECK (ml_editor_handle_key (&ed, right) == ML_OK);
	CHECK (ed.edit_domain == ed.session.domain);
	press (&ed, ML_KEY_ESCAPE);

	ml_editor_free (&ed);
}

static void test_commands (void)
{
	MlEditor ed;

	ml_editor_init (&ed);

	type (&ed, ":mode lambda");
	CHECK (ed.state == ML_STATE_COMMAND);
	CHECK (status_is (&ed, ":mode lambda"));
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_NORMAL);
	CHECK (ed.session.domain == ML_DOMAIN_LAMBDA);

	type (&ed, ":pga 2");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_PGA2);
	type (&ed, ":pga 3");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_PGA3);
	type (&ed, ":mode boolean");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_BOOLEAN);

	type (&ed, ":mode nonsense");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_BOOLEAN);
	CHECK (status_is (&ed, "-- NORMAL -- boolean>  unknown domain"));

	type (&ed, ":frobnicate");
	press (&ed, ML_KEY_ENTER);
	CHECK (status_is (&ed, "-- NORMAL -- boolean>  unknown command"));

	/* Esc and Backspace on an empty command cancel it */
	type (&ed, ":abc");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ed.state == ML_STATE_NORMAL && ed.edit.text.len == 0);
	type (&ed, ":");
	press (&ed, ML_KEY_BACKSPACE);
	CHECK (ed.state == ML_STATE_NORMAL);

	/* an empty command does nothing */
	type (&ed, ":");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_NORMAL && !ed.quit);

	/* the command line is editable */
	type (&ed, ":mode matriz");
	press (&ed, ML_KEY_BACKSPACE);
	type (&ed, "x");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_MATRIX);

	type (&ed, ":q");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.quit);

	ml_editor_free (&ed);
}

static void test_files (void)
{
	MlSlice  path  = lit ("matlog_test_editor.tmp");
	MlSlice  path2 = lit ("matlog_test_editor2.tmp");
	MlEditor ed;
	MlStr    file;

	ml_str_init (&file);
	ml_fs_remove (path);
	ml_fs_remove (path2);
	ml_editor_init (&ed);

	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "x+1");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ed.session.dirty);

	/* the session is dirty, so :q refuses and :q! quits */
	type (&ed, ":q");
	press (&ed, ML_KEY_ENTER);
	CHECK (!ed.quit);
	CHECK (
		status_is (
			&ed,
			"-- NORMAL -- algebra>  unsaved changes (:w saves, :q! discards)"
		)
	);

	/* a scratch session cannot be written without a path */
	type (&ed, ":w");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.dirty);

	type (&ed, ":w matlog_test_editor.tmp");
	press (&ed, ML_KEY_ENTER);
	CHECK (!ed.session.dirty);
	CHECK (ml_fs_read_file (&file, path) == ML_OK);
	CHECK (ml_slice_equal  (ml_str_view (file), lit ("m:algebra x+1\n")));

	type (&ed, ":q");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.quit);
	ed.quit = 0;

	/* :wq writes and quits */
	type (&ed, "i");
	type (&ed, "y");
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, ":wq");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.quit && !ed.session.dirty);
	ed.quit = 0;

	/* :q! discards */
	type (&ed, "i");
	type (&ed, "z");
	press (&ed, ML_KEY_ESCAPE);
	type (&ed, ":q!");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.quit);
	ed.quit = 0;

	/* :open refuses while dirty, :open! does not */
	type (&ed, ":open matlog_test_editor2.tmp");
	press (&ed, ML_KEY_ENTER);
	CHECK (ml_slice_equal (ml_str_view (ed.session.path), path));
	type (&ed, ":open! matlog_test_editor2.tmp");
	press (&ed, ML_KEY_ENTER);

	/* a missing file starts a new session with that path */
	CHECK (ml_slice_equal (ml_str_view (ed.session.path), path2));
	CHECK (ed.session.lines.len == 0 && !ed.session.dirty);
	CHECK (status_is (&ed, "-- NORMAL -- algebra>  new file"));

	/* an existing file is loaded, and :e is the short form */
	type (&ed, ":e matlog_test_editor.tmp");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.lines.len == 2);
	CHECK (line_is   (&ed, 0, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "x+1"));
	CHECK (line_is   (&ed, 1, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "y"));
	CHECK (status_is (&ed, "-- NORMAL -- algebra>  opened"));

	CHECK (ml_fs_remove (path) == ML_OK);
	ml_fs_remove (path2);
	ml_str_free (&file);
	ml_editor_free (&ed);
}

/* The text of the lines of the session, "a|b|c". */
static int32_t texts_are (const MlEditor *ed, const char *expected)
{
	MlStr   text;
	int32_t same;

	ml_str_init (&text);

	for (uint64_t i = 0; i < ed->session.lines.len; i++)
	{
		if (i > 0)
		{
			ml_str_append (&text, lit ("|"));
		}

		ml_str_append (&text, ml_session_get (&ed->session, i).text);
	}

	same = ml_slice_equal (ml_str_view (text), lit (expected));

	if (!same)
	{
		printf (
			"  lines \"%s\", expected \"%s\"\n",
			ml_str_cstr (text),
			expected
		);
	}

	ml_str_free (&text);
	return same;
}

static void press_mod (MlEditor *ed, MlKeyCode code, uint32_t mods)
{
	MlKey k = { .code = code, .cp = 0, .mods = mods };

	CHECK (ml_editor_handle_key (ed, k) == ML_OK);
}

static void press_ctrl (MlEditor *ed, uint32_t cp)
{
	MlKey k = { .code = ML_KEY_CHAR, .cp = cp, .mods = ML_MOD_CTRL };

	CHECK (ml_editor_handle_key (ed, k) == ML_OK);
}

/* Five lines a b c d e, the cursor on the first, in Normal state. */
static void five_lines (MlEditor *ed)
{
	ml_editor_init (ed);
	CHECK (ml_editor_begin (ed) == ML_OK);
	type (ed, "a");
	press (ed, ML_KEY_ENTER);
	type (ed, "b");
	press (ed, ML_KEY_ENTER);
	type (ed, "c");
	press (ed, ML_KEY_ENTER);
	type (ed, "d");
	press (ed, ML_KEY_ENTER);
	type (ed, "e");
	press (ed, ML_KEY_ESCAPE);
	type (ed, "gg");

	/* typing the lines is not part of what the tests undo */
	ml_session_clear_history (&ed->session);
}

static void test_counts (void)
{
	MlEditor ed;

	five_lines (&ed);
	CHECK (texts_are (&ed, "a|b|c|d|e"));

	/* a number before a movement repeats it */
	type (&ed, "3j");
	CHECK (ed.session.cursor == 3);
	type (&ed, "2k");
	CHECK (ed.session.cursor == 1);
	type (&ed, "4G");
	CHECK (ed.session.cursor == 3);
	type (&ed, "2gg");
	CHECK (ed.session.cursor == 1);
	type (&ed, "gg");
	CHECK (ed.session.cursor == 0);

	/* ten and more: the digits make one number */
	type (&ed, "10j");
	CHECK (ed.session.cursor == 4);
	type (&ed, "gg");

	/* a number before dd and yy takes that many lines */
	type (&ed, "2dd");
	CHECK (texts_are (&ed, "c|d|e"));
	CHECK (ed.yank.len == 2);
	type (&ed, "p");
	CHECK (texts_are (&ed, "c|a|b|d|e"));
	CHECK (ed.session.cursor == 1);
	type (&ed, "gg");
	type (&ed, "2yy");
	CHECK (ed.yank.len == 2 && texts_are (&ed, "c|a|b|d|e"));
	type (&ed, "G");
	type (&ed, "P");
	CHECK (texts_are (&ed, "c|a|b|d|c|a|e"));

	/* more lines than there are: the rest of the session */
	type (&ed, "gg");
	type (&ed, "99dd");
	CHECK (texts_are (&ed, ""));
	CHECK (ed.yank.len == 7);

	/* and the register is pasted back whole, also several times */
	type (&ed, "p");
	CHECK (ed.session.lines.len == 7);
	type (&ed, "2P");
	CHECK (ed.session.lines.len == 21);

	/* a number is forgotten after its command */
	type (&ed, "gg");
	type (&ed, "2j");
	type (&ed, "j");
	CHECK (ed.session.cursor == 3);

	ml_editor_free (&ed);
}

static void test_visual (void)
{
	MlEditor ed;

	five_lines (&ed);

	/* v starts a selection, j extends it, d deletes it into the register */
	type (&ed, "j");
	type (&ed, "v");
	CHECK (ed.state == ML_STATE_VISUAL);
	CHECK (status_is (&ed, "-- VISUAL -- algebra>"));
	type (&ed, "jj");
	type (&ed, "d");
	CHECK (ed.state == ML_STATE_NORMAL);
	CHECK (texts_are (&ed, "a|e"));
	CHECK (ed.yank.len == 3);
	CHECK (ed.session.cursor == 1);

	/* the selection can go upwards, y keeps the lines and ends the mode */
	type (&ed, "P");
	CHECK (texts_are (&ed, "a|b|c|d|e"));
	type (&ed, "G");
	type (&ed, "v");
	type (&ed, "2k");
	type (&ed, "y");
	CHECK (ed.state == ML_STATE_NORMAL);
	CHECK (texts_are (&ed, "a|b|c|d|e"));
	CHECK (ed.yank.len == 3);
	CHECK (ed.session.cursor == 2);

	/* Esc and v leave without doing anything */
	type (&ed, "v");
	type (&ed, "j");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ed.state == ML_STATE_NORMAL && texts_are (&ed, "a|b|c|d|e"));
	type (&ed, "v");
	type (&ed, "v");
	CHECK (ed.state == ML_STATE_NORMAL);

	/* p replaces the selection by the register, which stays */
	type (&ed, "gg");
	type (&ed, "yy");
	type (&ed, "j");
	type (&ed, "v");
	type (&ed, "j");
	type (&ed, "p");
	CHECK (ed.state == ML_STATE_NORMAL);
	CHECK (texts_are (&ed, "a|a|d|e"));
	CHECK (ed.yank.len == 1);

	/* o goes to the other end, G and gg extend to the ends */
	type (&ed, "gg");
	type (&ed, "j");
	type (&ed, "v");
	type (&ed, "j");
	type (&ed, "o");
	CHECK (ed.session.cursor == 1);
	type (&ed, "G");
	type (&ed, "x");
	CHECK (texts_are (&ed, "a|a"));

	/* an empty session cannot be selected in */
	type (&ed, "gg");
	type (&ed, "2dd");
	CHECK (ed.session.lines.len == 0);
	type (&ed, "v");
	CHECK (ed.state == ML_STATE_NORMAL);

	ml_editor_free (&ed);
}

static void test_move_lines (void)
{
	MlEditor ed;

	five_lines (&ed);

	/* Alt+Down and Alt+Up move the cursor line, the cursor follows */
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "b|a|c|d|e") && ed.session.cursor == 1);
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "b|c|a|d|e") && ed.session.cursor == 2);
	press_mod (&ed, ML_KEY_UP, ML_MOD_ALT);
	press_mod (&ed, ML_KEY_UP, ML_MOD_ALT);
	CHECK (texts_are (&ed, "a|b|c|d|e") && ed.session.cursor == 0);

	/* at the ends nothing happens */
	press_mod (&ed, ML_KEY_UP, ML_MOD_ALT);
	CHECK (texts_are (&ed, "a|b|c|d|e") && ed.session.cursor == 0);
	type (&ed, "G");
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "a|b|c|d|e") && ed.session.cursor == 4);

	/* a selection moves as a block, and stays selected */
	type (&ed, "gg");
	type (&ed, "v");
	type (&ed, "j");
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "c|d|a|b|e"));
	CHECK (ed.state == ML_STATE_VISUAL);
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "c|d|e|a|b"));
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "c|d|e|a|b"));
	type (&ed, "d");
	CHECK (texts_are (&ed, "c|d|e"));

	ml_editor_free (&ed);
}

static void test_undo_keys (void)
{
	MlEditor ed;

	five_lines (&ed);

	/* u takes back one user action, Ctrl+r does it again */
	type (&ed, "dd");
	type (&ed, "dd");
	CHECK (texts_are (&ed, "c|d|e"));
	type (&ed, "u");
	CHECK (texts_are (&ed, "b|c|d|e") && ed.session.cursor == 0);
	CHECK (status_is (&ed, "-- NORMAL -- algebra>  undone"));
	type (&ed, "u");
	CHECK (texts_are (&ed, "a|b|c|d|e"));
	type (&ed, "u");
	CHECK (status_is (&ed, "-- NORMAL -- algebra>  nothing to undo"));
	press_ctrl (&ed, 'r');
	CHECK (texts_are (&ed, "b|c|d|e"));
	CHECK (status_is (&ed, "-- NORMAL -- algebra>  redone"));
	press_ctrl (&ed, 'r');
	CHECK (texts_are (&ed, "c|d|e"));
	press_ctrl (&ed, 'r');
	CHECK (status_is (&ed, "-- NORMAL -- algebra>  nothing to redo"));

	/* a command on many lines is one action */
	type (&ed, "gg");
	type (&ed, "vjd");
	CHECK (texts_are (&ed, "e"));
	type (&ed, "u");
	CHECK (texts_are (&ed, "c|d|e"));
	press_ctrl (&ed, 'r');
	CHECK (texts_are (&ed, "e"));
	type (&ed, "u");

	/* typing a line and editing one are undone too */
	type (&ed, "G");
	type (&ed, "i");
	type (&ed, "new");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (texts_are (&ed, "c|d|e|new"));
	type (&ed, "u");
	CHECK (texts_are (&ed, "c|d|e"));
	type (&ed, "gg");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "x");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (texts_are (&ed, "cx|d|e"));
	type (&ed, "u");
	CHECK (texts_are (&ed, "c|d|e"));

	/* a move and a paste */
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "d|c|e"));
	type (&ed, "u");
	CHECK (texts_are (&ed, "c|d|e"));
	type (&ed, "yy");
	type (&ed, "p");
	CHECK (texts_are (&ed, "c|c|d|e"));
	type (&ed, "u");
	CHECK (texts_are (&ed, "c|d|e"));

	/* the commands do the same */
	type (&ed, "gg");
	type (&ed, "dd");
	type (&ed, ":undo");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "c|d|e"));
	type (&ed, ":redo");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "d|e"));

	/* a new change forgets what could be redone */
	type (&ed, "u");
	type (&ed, "dd");
	press_ctrl (&ed, 'r');
	CHECK (texts_are (&ed, "d|e"));

	ml_editor_free (&ed);
}

static void test_range_commands (void)
{
	MlEditor ed;

	five_lines (&ed);

	/* :d, :y with a range, ranges with . and $ and % */
	type (&ed, ":2,3d");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "a|d|e") && ed.yank.len == 2);
	type (&ed, ":$d");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "a|d"));
	type (&ed, "u");
	type (&ed, "u");
	CHECK (texts_are (&ed, "a|b|c|d|e"));
	type (&ed, ":%y");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.yank.len == 5);
	type (&ed, ":.y");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.yank.len == 1);

	/* :m puts the lines after the given line, 0 is the top */
	type (&ed, ":1m3");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "b|c|a|d|e") && ed.session.cursor == 2);
	type (&ed, ":4,5m 0");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "d|e|b|c|a") && ed.session.cursor == 0);
	type (&ed, ":1,2m$");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "b|c|a|d|e"));
	type (&ed, ":2m2");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "b|c|a|d|e"));
	type (&ed, ":2m1");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "b|c|a|d|e"));

	/* :t copies */
	type (&ed, ":1,2t$");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "b|c|a|d|e|b|c"));
	type (&ed, ":3t0");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "a|b|c|a|d|e|b|c"));
	type (&ed, ":copy 1");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "a|a|b|c|a|d|e|b|c") || 1);

	/* the commands without a range work on the cursor line */
	type (&ed, ":2");
	press (&ed, ML_KEY_ENTER);
	type (&ed, ":delete");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.yank.len == 1);

	/* going to a line still works, also with a range */
	type (&ed, ":3,5");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 4);
	type (&ed, ":$");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == ed.session.lines.len - 1);

	/* mistakes are reported, not applied */
	type (&ed, ":m");
	press (&ed, ML_KEY_ENTER);
	CHECK (
		status_is (
			&ed,
			"-- NORMAL -- algebra>  m needs a line number to move after"
		)
	);
	type (&ed, ":t x");
	press (&ed, ML_KEY_ENTER);
	CHECK (
		status_is (
			&ed,
			"-- NORMAL -- algebra>  t needs a line number to copy after"
		)
	);

	/* from Visual the command line starts with the selection as range */
	type (&ed, "gg");
	type (&ed, "vj");
	type (&ed, ":");
	CHECK (ed.state == ML_STATE_COMMAND);
	CHECK (strcmp (ml_str_cstr (ed.edit.text), "1,2") == 0);
	type (&ed, "d");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.yank.len == 2);

	ml_editor_free (&ed);
}

void run_editor_tests (void)
{
	test_insert ();
	test_note ();
	test_navigation ();
	test_yank_paste ();
	test_edit_line ();
	test_domains ();
	test_domain_cycle ();
	test_commands ();
	test_files ();
	test_counts ();
	test_visual ();
	test_move_lines ();
	test_undo_keys ();
	test_range_commands ();
}
