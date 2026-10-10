#include "test.h"
#include "test_keys.h"

#include <matlog/core/utf8.h>
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
	CHECK (ed.state == ML_STATE_MATH);
	CHECK (status_is (&ed, "-- MATH -- algebra>"));

	type (&ed, "1+2");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_MATH);
	CHECK (ed.session.lines.len == 1);
	CHECK (line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "1+2"));

	/* the rewrites are active, also when the line ends right after a token */
	type (&ed, "x^2");
	press (&ed, ML_KEY_ENTER);
	CHECK (line_is (&ed, 1, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "x\xC2\xB2"));

	/* Enter on an empty line writes a blank line; a note follows */
	press (&ed, ML_KEY_ENTER);
	CHECK (line_is (&ed, 2, ML_LINE_BLANK, ML_DOMAIN_ALGEBRA, ""));

	/* Esc commits what was typed and returns to Normal */
	type (&ed, "y<=3");
	test_leave (&ed);
	CHECK (test_editing (&ed));
	CHECK (ed.session.lines.len == 4);
	CHECK (
		line_is (&ed, 3, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "y\xE2\x89\xA4" "3")
	);
	CHECK (ed.session.cursor == 3);
	CHECK (status_is (&ed, "-- NOTE -- algebra>"));

	/* Esc on an empty new line adds nothing */
	test_normal (&ed, "o");
	test_leave (&ed);
	CHECK (ed.session.lines.len == 4);

	/* backspace undoes a rewrite just made */
	test_normal (&ed, "o");
	type (&ed, "a<=");
	press (&ed, ML_KEY_BACKSPACE);
	test_leave (&ed);
	CHECK (line_is (&ed, 4, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "a<="));

	ml_editor_free (&ed);
}

static void test_note (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "1+1");
	test_leave (&ed);

	/* n starts a note below the cursor line; the rewrites work in it too */
	test_normal (&ed, "N");
	CHECK (ed.state == ML_STATE_NOTE);
	CHECK (status_is (&ed, "-- NOTE -- algebra>"));
	type (&ed, "x^2 <= pi ");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_NOTE);
	type (&ed, "second");
	test_leave (&ed);

	CHECK (ed.session.lines.len == 3);
	CHECK (
		line_is (
			&ed,
			1,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			"x\xC2\xB2 \xE2\x89\xA4 \xCF\x80 "
		)
	);
	CHECK (line_is (&ed, 2, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "second"));

	ml_editor_free (&ed);
}

static int32_t edit_is (const MlEditor *ed, const char *text)
{
	return ml_slice_equal (ml_str_view (ed->edit.text), ml_slice_cstr (text));
}

static void test_first_note (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	test_leave (&ed);
	test_normal (&ed, "N");
	CHECK (ed.state == ML_STATE_NOTE);
	type (&ed, "hello");
	CHECK (edit_is (&ed, "hello"));
	test_leave (&ed);
	CHECK (ed.session.lines.len == 1);
	CHECK (line_is (&ed, 0, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "hello"));

	/* n and i open a note and a math line at the cursor line, above it */
	test_normal (&ed, "n");
	CHECK (ed.state == ML_STATE_NOTE);
	type (&ed, "top");
	test_leave (&ed);
	test_normal (&ed, "i");
	type (&ed, "1+1");
	test_leave (&ed);
	CHECK (ed.session.lines.len == 3);
	CHECK (line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "1+1"));
	CHECK (line_is (&ed, 1, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "top"));
	CHECK (line_is (&ed, 2, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "hello"));

	ml_editor_free (&ed);
}

static void test_note_freeform (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	test_leave (&ed);
	test_normal (&ed, "n");
	type (&ed, "hello world");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "second");

	/* Up keeps the column, Down goes back */
	press (&ed, ML_KEY_UP);
	CHECK (ed.edit_index == 0 && ed.edit_existing);
	CHECK (ml_lineedit_cursor_col (&ed.edit) == 6);
	press (&ed, ML_KEY_DOWN);
	CHECK (ed.edit_index == 1 && ml_lineedit_cursor_col (&ed.edit) == 6);

	/* Left at the start and Right at the end cross to the neighbour */
	press (&ed, ML_KEY_HOME);
	press (&ed, ML_KEY_LEFT);
	CHECK (ed.edit_index == 0 && ml_lineedit_cursor_col (&ed.edit) == 11);
	press (&ed, ML_KEY_RIGHT);
	CHECK (ed.edit_index == 1 && ml_lineedit_cursor_col (&ed.edit) == 0);

	/* Backspace at the start joins, Delete at the end joins */
	press (&ed, ML_KEY_BACKSPACE);
	CHECK (ed.edit_index == 0 && ed.session.lines.len == 1);
	CHECK (ed.edit.cursor == 11);
	CHECK (
		line_is (&ed, 0, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "hello worldsecond")
	);

	/* Enter splits at the cursor */
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.edit_index == 1);
	CHECK (edit_is (&ed, "second"));
	CHECK (ed.edit.cursor == 0);
	press (&ed, ML_KEY_UP);
	press (&ed, ML_KEY_END);
	press (&ed, ML_KEY_DELETE);
	CHECK (ed.session.lines.len == 1);
	CHECK (edit_is (&ed, "hello worldsecond"));
	test_leave (&ed);

	ml_editor_free (&ed);
}

static int32_t texts_are (const MlEditor *ed, const char *expected);

static void test_ctrl_z (void)
{
	MlEditor ed;
	MlKey    undo = { .code = ML_KEY_CHAR, .cp = 'z', .mods = ML_MOD_CTRL };
	MlKey    redo = {
		.code = ML_KEY_CHAR,
		.cp   = 'z',
		.mods = ML_MOD_CTRL | ML_MOD_SHIFT
	};

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "1+1");
	test_leave (&ed);
	CHECK (ed.session.lines.len == 1);

	CHECK (ml_editor_handle_key (&ed, undo) == ML_OK);
	CHECK (ed.session.lines.len == 0);
	CHECK (ml_editor_handle_key (&ed, redo) == ML_OK);
	CHECK (ed.session.lines.len == 1);

	/* while typing, the line is kept and then undone */
	test_normal (&ed, "o");
	type (&ed, "2");
	CHECK (ml_editor_handle_key (&ed, undo) == ML_OK);
	CHECK (test_editing (&ed));
	CHECK (ed.session.lines.len == 1);

	ml_editor_free (&ed);
}

static void test_modeless (void)
{
	MlEditor ed;
	MlKey    toggle = { .code = ML_KEY_CHAR, .cp = 'n', .mods = ML_MOD_CTRL };

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "1+1");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "2+2");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "3+3");

	/* Up and Down roam through math lines too, the column is kept */
	press (&ed, ML_KEY_UP);
	CHECK (ed.edit_index == 1 && ed.edit.cursor == 3);
	press (&ed, ML_KEY_HOME);
	press (&ed, ML_KEY_BACKSPACE);
	CHECK (ed.edit_index == 0 && ed.session.lines.len == 2);
	CHECK (edit_is (&ed, "1+12+2"));
	CHECK (ed.edit.cursor == 3);

	/* Enter splits a math line at the cursor */
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.edit_index == 1 && edit_is (&ed, "2+2"));

	/* Ctrl+N turns the line into a note and back */
	CHECK (ml_editor_handle_key (&ed, toggle) == ML_OK);
	CHECK (ed.state == ML_STATE_NOTE);
	test_leave (&ed);
	CHECK (line_is (&ed, 1, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "2+2"));

	ml_editor_free (&ed);
}

static void shift_key (MlEditor *ed, MlKeyCode code)
{
	test_key (ed, code, 0, ML_MOD_SHIFT);
}

static void test_selection (void)
{
	MlEditor ed;
	MlTextRange s;

	ml_editor_init (&ed);
	CHECK (ml_editor_begin_note (&ed) == ML_OK);
	type (&ed, "hello world");

	/* Shift+Left selects text, Backspace deletes it */
	for (uint32_t i = 0; i < 5; i++)
	{
		shift_key (&ed, ML_KEY_LEFT);
	}

	CHECK (ed.selecting && ed.sel_anchor == 11 && ed.edit.cursor == 6);
	press (&ed, ML_KEY_BACKSPACE);
	CHECK (edit_is (&ed, "hello ") && !ed.selecting);

	/* typing replaces the selection; Shift+Home selects to the start */
	shift_key (&ed, ML_KEY_HOME);
	CHECK (ed.selecting && ed.edit.cursor == 0);
	type (&ed, "bye");
	CHECK (edit_is (&ed, "bye") && !ed.selecting);

	/* a plain Left goes to the start of a selection and clears it */
	shift_key (&ed, ML_KEY_LEFT);
	shift_key (&ed, ML_KEY_LEFT);
	press (&ed, ML_KEY_RIGHT);
	CHECK (!ed.selecting && ed.edit.cursor == 3);
	shift_key (&ed, ML_KEY_LEFT);
	shift_key (&ed, ML_KEY_LEFT);
	press (&ed, ML_KEY_LEFT);
	CHECK (!ed.selecting && ed.edit.cursor == 1);

	/* Shift+Up and Shift+Down select text across lines */
	press (&ed, ML_KEY_END);
	press (&ed, ML_KEY_ENTER);
	type (&ed, "second");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "third");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "fourth");
	shift_key (&ed, ML_KEY_UP);
	s = ml_editor_selection (&ed);
	CHECK (s.valid && s.first_line == 2 && s.first_byte == 5);
	CHECK (s.last_line == 3 && s.last_byte == 6);
	shift_key (&ed, ML_KEY_UP);
	s = ml_editor_selection (&ed);
	CHECK (s.first_line == 1 && s.first_byte == 5 && s.last_line == 3);

	/* typing replaces it: the first and the last line become one */
	type (&ed, "-");
	CHECK (texts_are (&ed, "bye|secon"));
	CHECK (ed.edit_index == 1 && edit_is (&ed, "secon-"));
	CHECK (ed.edit.cursor == 6);

	ml_editor_free (&ed);
}

static void test_selection_edit (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (ml_editor_begin_note (&ed) == ML_OK);
	type (&ed, "one");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "two");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "three");

	/* Ctrl+C copies text across lines, Ctrl+V puts it in at the cursor */
	test_key (&ed, ML_KEY_HOME, 0, ML_MOD_CTRL);
	press (&ed, ML_KEY_LEFT);
	press (&ed, ML_KEY_LEFT);
	shift_key (&ed, ML_KEY_DOWN);
	test_key (&ed, ML_KEY_CHAR, 'c', ML_MOD_CTRL);
	CHECK (ed.yank_text && ed.yank.len == 2);
	CHECK (ml_slice_equal (ml_str_view (ed.yank.data [0].text), lit ("ne")));
	CHECK (ml_slice_equal (ml_str_view (ed.yank.data [1].text), lit ("t")));

	/* Ctrl+X removes it, the two lines become one */
	test_key (&ed, ML_KEY_CHAR, 'x', ML_MOD_CTRL);
	CHECK (texts_are (&ed, "owo|three"));
	CHECK (ed.edit_index == 0 && ed.edit.cursor == 1);

	/* and Ctrl+V splits the line again */
	test_key (&ed, ML_KEY_CHAR, 'v', ML_MOD_CTRL);
	CHECK (texts_are (&ed, "one|two|three"));
	CHECK (ed.edit_index == 1 && ed.edit.cursor == 1);

	/* Ctrl+A and Delete remove all the text */
	test_key (&ed, ML_KEY_CHAR, 'a', ML_MOD_CTRL);
	press (&ed, ML_KEY_DELETE);
	CHECK (ed.session.lines.len == 0);

	ml_editor_free (&ed);
}

static void test_esc_leaves_block (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "a");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "b");

	/* Esc in a block keeps the line and starts a note below the block */
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ed.state == ML_STATE_NOTE && !ed.edit_existing);
	CHECK (ed.edit_index == 2 && texts_are (&ed, "a|b"));
	type (&ed, "after");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (texts_are (&ed, "a|b|after"));
	CHECK (ml_session_get (&ed.session, 2).kind == ML_LINE_NOTE);

	/* an empty new line of a block becomes the note */
	test_normal (&ed, "o");
	type (&ed, "c");
	press (&ed, ML_KEY_ENTER);
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ed.state == ML_STATE_NOTE && ed.edit_index == 4);
	CHECK (texts_are (&ed, "a|b|after|c"));

	/* Esc in a note changes nothing */
	type (&ed, "x");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ed.state == ML_STATE_NOTE && ed.edit_index == 4);
	CHECK (texts_are (&ed, "a|b|after|c|x"));

	ml_editor_free (&ed);

	/* Backspace on the empty note takes the user back into the block */
	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "a");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "b");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ed.state == ML_STATE_NOTE && ed.edit_index == 2);
	press (&ed, ML_KEY_BACKSPACE);
	CHECK (ed.state == ML_STATE_MATH && ed.edit_index == 1);
	CHECK (ed.edit.cursor == ed.edit.text.len && edit_is (&ed, "b"));
	CHECK (texts_are (&ed, "a|b"));

	ml_editor_free (&ed);
}

static void test_pick_block (void)
{
	MlEditor ed;
	MlKey    shift_enter = {
		.code = ML_KEY_ENTER,
		.cp   = 0,
		.mods = ML_MOD_SHIFT
	};

	MlKey    ctrl_b = {
		.code = ML_KEY_CHAR,
		.cp   = 'b',
		.mods = ML_MOD_CTRL
	};

	MlStr    status;

	ml_editor_init (&ed);
	ml_str_init (&status);

	/* the editor starts on a note */
	CHECK (ml_editor_begin_note (&ed) == ML_OK);
	CHECK (ed.state == ML_STATE_NOTE);
	type (&ed, "intro");

	/* Ctrl+B chooses the block; letters and arrows move the choice */
	CHECK (ml_editor_handle_key (&ed, ctrl_b) == ML_OK);
	CHECK (ed.picking && ed.pick == ML_DOMAIN_ALGEBRA);
	type (&ed, "l");
	CHECK (ed.pick == ML_DOMAIN_LAMBDA);
	press (&ed, ML_KEY_RIGHT);
	CHECK (ed.pick == ML_DOMAIN_PGA2);
	press (&ed, ML_KEY_LEFT);
	CHECK (ml_editor_status (&status, &ed) == ML_OK);
	CHECK (strstr (ml_str_cstr (status), "[lambda]") != NULL);

	/* Enter starts the block on a new line below */
	press (&ed, ML_KEY_ENTER);
	CHECK (!ed.picking && ed.state == ML_STATE_MATH);
	CHECK (ed.edit_domain == ML_DOMAIN_LAMBDA);
	type (&ed, "A = 1");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "B = 2");

	/* Enter on an empty line of the block ends it: a note follows */
	press (&ed, ML_KEY_ENTER);
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_NOTE);
	type (&ed, "after");
	test_leave (&ed);
	CHECK (texts_are (&ed, "intro|A = 1|B = 2||after"));
	CHECK (ml_session_get (&ed.session, 1).kind == ML_LINE_MATH);
	CHECK (ml_session_get (&ed.session, 1).domain == ML_DOMAIN_LAMBDA);
	CHECK (ml_session_get (&ed.session, 4).kind == ML_LINE_NOTE);

	/* Esc cancels the choice, an empty line becomes the block's first line */
	test_normal (&ed, "N");
	CHECK (ml_editor_handle_key (&ed, ctrl_b) == ML_OK);
	press (&ed, ML_KEY_ESCAPE);
	CHECK (!ed.picking && ed.state == ML_STATE_NOTE);
	CHECK (ml_editor_handle_key (&ed, ctrl_b) == ML_OK);
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_MATH && ed.edit_index == 5);

	/* Shift+Enter starts a block at once, of the domain used last */
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ed.state == ML_STATE_NOTE);
	CHECK (ml_editor_handle_key (&ed, shift_enter) == ML_OK);
	CHECK (!ed.picking && ed.state == ML_STATE_MATH);
	CHECK (ed.edit_domain == ML_DOMAIN_LAMBDA);

	ml_str_free (&status);
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
	test_leave (&ed);
	CHECK (ed.session.cursor == 2);

	test_normal (&ed, "k");
	CHECK (ed.session.cursor == 1);
	test_normal (&ed, "k");
	test_normal (&ed, "k");
	CHECK (ed.session.cursor == 0);
	test_normal (&ed, "j");
	CHECK (ed.session.cursor == 1);
	press (&ed, ML_KEY_DOWN);
	press (&ed, ML_KEY_DOWN);
	CHECK (ed.session.cursor == 2);
	press (&ed, ML_KEY_UP);
	CHECK (ed.session.cursor == 1);

	test_normal (&ed, "G");
	CHECK (ed.session.cursor == 2);

	/* :N goes to line N (counting from 1), clamped to the last line */
	test_normal (&ed, ":2");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 1);
	test_normal (&ed, ":1");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 0);
	test_normal (&ed, ":99");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 2);
	test_normal (&ed, ":0");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 0);
	test_normal (&ed, ":2zz");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 0);
	CHECK (status_is (&ed, "-- MATH -- algebra>  unknown command"));
	test_normal (&ed, "G");
	test_normal (&ed, "gg");
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
	test_leave (&ed);

	/* yy copies, p pastes below the cursor */
	test_normal (&ed, "kyy");
	CHECK (ed.yank.len == 1 && ed.yank.data [0].kind == ML_LINE_MATH);
	test_normal (&ed, "p");
	CHECK (ed.session.lines.len == 4);
	CHECK (line_is (&ed, 2, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "b"));
	CHECK (ed.session.cursor == 2);

	/* dd removes the line and keeps it in the register */
	test_normal (&ed, "dd");
	CHECK (ed.session.lines.len == 3);
	CHECK (line_is (&ed, 2, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "c"));

	/* p pastes below the line the cursor is in, also at the top */
	test_normal (&ed, "gg");
	test_normal (&ed, "p");
	CHECK (ed.session.lines.len == 4);
	CHECK (line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "a"));
	CHECK (line_is (&ed, 1, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "b"));

	/* dd on an empty session is harmless */
	ml_editor_free (&ed);
	ml_editor_init (&ed);
	test_normal (&ed, "dd");
	test_normal (&ed, "p");
	CHECK (ed.session.lines.len == 0);

	ml_editor_free (&ed);
}

static void test_edit_line (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	test_normal (&ed, ":mode pga2");
	press (&ed, ML_KEY_ENTER);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "A = 1");
	test_leave (&ed);
	test_normal (&ed, ":mode lambda");
	press (&ed, ML_KEY_ENTER);
	test_normal (&ed, "o");
	type (&ed, "x");
	test_leave (&ed);
	CHECK (line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_PGA2, "A = 1"));
	CHECK (line_is (&ed, 1, ML_LINE_MATH, ML_DOMAIN_LAMBDA, "x"));

	/* Enter edits the cursor line; it keeps its own domain */
	test_normal (&ed, "gg");
	press (&ed, ML_KEY_END);
	CHECK (ed.state == ML_STATE_MATH);
	CHECK (ed.edit.cursor == 5);
	CHECK (status_is (&ed, "-- MATH -- pga2>"));
	type (&ed, "2");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (test_editing (&ed));
	CHECK (ed.session.lines.len == 2);
	CHECK (line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_PGA2, "A = 12"));
	CHECK (ed.session.domain == ML_DOMAIN_LAMBDA);

	/* an edited note stays a note */
	test_normal (&ed, "N");
	type (&ed, "memo");
	CHECK (ed.state == ML_STATE_NOTE);
	type (&ed, "!");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (line_is (&ed, 1, ML_LINE_NOTE, ML_DOMAIN_LAMBDA, "memo!"));

	/* clearing a math line makes it blank */
	test_normal (&ed, "gg");
	press (&ed, ML_KEY_END);
	ml_lineedit_clear (&ed.edit);
	press (&ed, ML_KEY_ESCAPE);
	CHECK (ml_session_get (&ed.session, 0).kind == ML_LINE_BLANK);

	ml_editor_free (&ed);
}

static void test_domains (void)
{
	MlEditor ed;
	uint32_t i;

	/* matrix, vector and equations are the names of older files */
	static const char *const names []   = {
		"algebra", "lambda", "boolean", "pga2", "pga3",
		"matrix", "vector", "equations"
	};

	static const MlDomain    domains [] = {
		ML_DOMAIN_ALGEBRA, ML_DOMAIN_LAMBDA, ML_DOMAIN_BOOLEAN,
		ML_DOMAIN_PGA2, ML_DOMAIN_PGA3,
		ML_DOMAIN_ALGEBRA, ML_DOMAIN_ALGEBRA, ML_DOMAIN_ALGEBRA
	};

	ml_editor_init (&ed);

	for (i = 0; i < 8; i++)
	{
		test_key (&ed, ML_KEY_CHAR, 'p', ML_MOD_CTRL);
		test_text (&ed, "mode ");
		test_text (&ed, names [i]);
		press (&ed, ML_KEY_ENTER);
		CHECK (ed.session.domain == domains [i]);
	}

	/* the status line follows the domain */
	test_normal (&ed, ":mode lambda");
	press (&ed, ML_KEY_ENTER);
	CHECK (status_is       (&ed, "-- NOTE -- lambda>  lambda"));
	CHECK (ml_editor_begin (&ed) == ML_OK);
	CHECK (status_is       (&ed, "-- MATH -- lambda>"));

	/* new lines use it, and the rewriter follows it */
	type (&ed, "A^2 ");
	test_leave (&ed);
	CHECK (line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_LAMBDA, "A\xC2\xB2 "));

	test_normal (&ed, ":mode pga3");
	press (&ed, ML_KEY_ENTER);
	test_normal (&ed, "o");
	type (&ed, "e1^2 ");
	test_leave (&ed);
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
	CHECK (ed.session.domain == ML_DOMAIN_LAMBDA);
	CHECK (status_is            (&ed, "-- NOTE -- lambda>  lambda"));
	CHECK (ml_editor_handle_key (&ed, left) == ML_OK);
	CHECK (ed.session.domain == ML_DOMAIN_ALGEBRA);
	CHECK (ml_editor_handle_key (&ed, left) == ML_OK);
	CHECK (ed.session.domain == ML_DOMAIN_PHYSICS);
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
	CHECK (ed.edit_domain == ML_DOMAIN_LAMBDA);
	CHECK (status_is (&ed, "-- MATH -- lambda>  lambda"));
	type (&ed, "b^2 ");
	press (&ed, ML_KEY_ENTER);
	CHECK (
		line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_LAMBDA, "a\xC2\xB2 b\xC2\xB2 ")
	);
	CHECK (ed.session.domain == ML_DOMAIN_LAMBDA);
	CHECK (ed.edit_domain == ML_DOMAIN_LAMBDA);

	/* the rewrites follow the domain: ^ is an operator in the boolean domain */
	CHECK (ml_editor_handle_key (&ed, left) == ML_OK);
	CHECK (ml_editor_handle_key (&ed, left) == ML_OK);
	CHECK (ml_editor_handle_key (&ed, left) == ML_OK);
	CHECK (ed.edit_domain == ML_DOMAIN_BOOLEAN);
	type (&ed, "c^2 ");
	test_leave (&ed);
	CHECK (line_is (&ed, 1, ML_LINE_MATH, ML_DOMAIN_BOOLEAN, "c^2 "));

	/* editing an old line changes only that line */
	test_normal (&ed, "gg");
	press (&ed, ML_KEY_END);
	CHECK (ml_editor_handle_key (&ed, right) == ML_OK);
	press (&ed, ML_KEY_ENTER);
	CHECK (
		line_is (&ed, 0, ML_LINE_MATH, ML_DOMAIN_PGA2, "a\xC2\xB2 b\xC2\xB2 ")
	);
	CHECK (ed.session.domain == ML_DOMAIN_BOOLEAN);

	/* notes have no domain to change */
	test_key (&ed, ML_KEY_CHAR, 'n', ML_MOD_CTRL);
	CHECK (ml_editor_handle_key (&ed, right) == ML_OK);
	CHECK (ed.edit_domain == ed.session.domain);
	press (&ed, ML_KEY_ESCAPE);

	ml_editor_free (&ed);
}

static int32_t theme_is (const MlEditor *ed, const char *name)
{
	return ml_slice_equal (
		ml_slice_cstr (ed->theme.name),
		ml_slice_cstr (name)
	);
}

static void test_look_commands (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (!ed.highlight);

	/* :theme chooses a built in theme and turns the colours on */
	test_normal (&ed, ":theme light");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.highlight);
	CHECK (theme_is (&ed, "light"));
	CHECK (status_is (&ed, "-- NOTE -- algebra>  theme: light"));

	test_normal (&ed, ":theme reload");
	press (&ed, ML_KEY_ENTER);
	CHECK (theme_is (&ed, "light"));

	test_normal (&ed, ":theme no-such-theme");
	press (&ed, ML_KEY_ENTER);
	CHECK (theme_is (&ed, "light"));

	/* a name that could leave the settings folder is not looked up */
	test_normal (&ed, ":theme ../x");
	press (&ed, ML_KEY_ENTER);
	CHECK (theme_is (&ed, "light"));

	test_normal (&ed, ":colors 256");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.colors == ML_COLORS_256);
	test_normal (&ed, ":colors 8");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.colors == ML_COLORS_256);
	test_normal (&ed, ":colors off");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.colors == ML_COLORS_OFF);

	ml_editor_free (&ed);
}

static void test_commands (void)
{
	MlEditor ed;

	ml_editor_init (&ed);

	test_normal (&ed, ":mode lambda");
	CHECK (ed.state == ML_STATE_COMMAND);
	CHECK (status_is (&ed, ":mode lambda"));
	press (&ed, ML_KEY_ENTER);
	CHECK (test_editing (&ed));
	CHECK (ed.session.domain == ML_DOMAIN_LAMBDA);

	test_normal (&ed, ":pga 2");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_PGA2);
	test_normal (&ed, ":pga 3");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_PGA3);
	test_normal (&ed, ":mode boolean");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_BOOLEAN);

	test_normal (&ed, ":mode nonsense");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_BOOLEAN);
	CHECK (status_is (&ed, "-- NOTE -- boolean>  unknown domain"));

	test_normal (&ed, ":frobnicate");
	press (&ed, ML_KEY_ENTER);
	CHECK (status_is (&ed, "-- NOTE -- boolean>  unknown command"));

	/* Esc and Backspace on an empty command cancel it */
	test_normal (&ed, ":abc");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (test_editing (&ed) && ed.edit.text.len == 0);
	test_normal (&ed, ":");
	press (&ed, ML_KEY_BACKSPACE);
	CHECK (test_editing (&ed));

	/* an empty command does nothing */
	test_normal (&ed, ":");
	press (&ed, ML_KEY_ENTER);
	CHECK (test_editing (&ed) && !ed.quit);

	/* the command line is editable */
	test_normal (&ed, ":mode pgaz");
	press (&ed, ML_KEY_BACKSPACE);
	type (&ed, "3");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.domain == ML_DOMAIN_PGA3);

	test_normal (&ed, ":q");
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
	test_leave (&ed);
	CHECK (ed.session.dirty);

	/* the session is dirty, so :q refuses and :q! quits */
	test_normal (&ed, ":q");
	press (&ed, ML_KEY_ENTER);
	CHECK (!ed.quit);
	CHECK (
		status_is (
			&ed,
			"-- MATH -- algebra>  unsaved changes (:w saves, :q! discards)"
		)
	);

	/* a scratch session cannot be written without a path */
	test_normal (&ed, ":w");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.dirty);

	test_normal (&ed, ":w matlog_test_editor.tmp");
	press (&ed, ML_KEY_ENTER);
	CHECK (!ed.session.dirty);
	CHECK (ml_fs_read_file (&file, path) == ML_OK);
	CHECK (ml_slice_equal  (ml_str_view (file), lit ("m:algebra x+1\n")));

	test_normal (&ed, ":q");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.quit);
	ed.quit = 0;

	/* :wq writes and quits */
	test_normal (&ed, "o");
	type (&ed, "y");
	test_leave (&ed);
	test_normal (&ed, ":wq");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.quit && !ed.session.dirty);
	ed.quit = 0;

	/* :q! discards */
	test_normal (&ed, "o");
	type (&ed, "z");
	test_leave (&ed);
	test_normal (&ed, ":q!");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.quit);
	ed.quit = 0;

	/* :open refuses while dirty, :open! does not */
	test_normal (&ed, ":open matlog_test_editor2.tmp");
	press (&ed, ML_KEY_ENTER);
	CHECK (ml_slice_equal (ml_str_view (ed.session.path), path));
	test_normal (&ed, ":open! matlog_test_editor2.tmp");
	press (&ed, ML_KEY_ENTER);

	/* a missing file starts a new session with that path */
	CHECK (ml_slice_equal (ml_str_view (ed.session.path), path2));
	CHECK (ed.session.lines.len == 0 && !ed.session.dirty);
	CHECK (status_is (&ed, "-- NOTE -- algebra>  new file"));

	/* an existing file is loaded, and :e is the short form */
	test_normal (&ed, ":e matlog_test_editor.tmp");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.lines.len == 2);
	CHECK (line_is   (&ed, 0, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "x+1"));
	CHECK (line_is   (&ed, 1, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, "y"));
	CHECK (status_is (&ed, "-- MATH -- algebra>  opened"));

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
	test_leave (ed);
	test_normal (ed, "gg");

	/* typing the lines is not part of what the tests undo */
	ml_session_clear_history (&ed->session);
}

static void test_counts (void)
{
	MlEditor ed;

	five_lines (&ed);
	CHECK (texts_are (&ed, "a|b|c|d|e"));

	/* a number before a movement repeats it */
	test_normal (&ed, "3j");
	CHECK (ed.session.cursor == 3);
	test_normal (&ed, "2k");
	CHECK (ed.session.cursor == 1);
	test_normal (&ed, "4G");
	CHECK (ed.session.cursor == 3);
	test_normal (&ed, "2gg");
	CHECK (ed.session.cursor == 1);
	test_normal (&ed, "gg");
	CHECK (ed.session.cursor == 0);

	/* ten and more: the digits make one number */
	test_normal (&ed, "10j");
	CHECK (ed.session.cursor == 4);
	test_normal (&ed, "gg");

	/* a number before dd and yy takes that many lines */
	test_normal (&ed, "2dd");
	CHECK (texts_are (&ed, "c|d|e"));
	CHECK (ed.yank.len == 2);
	test_normal (&ed, "p");
	CHECK (texts_are (&ed, "c|a|b|d|e"));
	CHECK (ed.session.cursor == 1);
	test_normal (&ed, "gg");
	test_normal (&ed, "2yy");
	CHECK (ed.yank.len == 2 && texts_are (&ed, "c|a|b|d|e"));
	test_normal (&ed, "G");
	test_normal (&ed, "p");
	CHECK (texts_are (&ed, "c|a|b|d|e|c|a"));

	/* more lines than there are: the rest of the session */
	test_normal (&ed, "gg");
	test_normal (&ed, "99dd");
	CHECK (texts_are (&ed, ""));
	CHECK (ed.yank.len == 7);

	/* and the register is pasted back whole */
	test_normal (&ed, "p");
	CHECK (ed.session.lines.len == 7);

	/* a number is forgotten after its command */
	test_normal (&ed, "gg");
	test_normal (&ed, "2j");
	test_normal (&ed, "j");
	CHECK (ed.session.cursor == 3);

	ml_editor_free (&ed);
}

static void test_visual (void)
{
	MlEditor ed;

	five_lines (&ed);

	/* v starts a selection, j extends it, d deletes it into the register */
	test_normal (&ed, "j");
	test_normal (&ed, "v");
	CHECK (ed.selecting);
	test_normal (&ed, "jj");
	test_normal (&ed, "d");
	CHECK (test_editing (&ed));
	CHECK (texts_are (&ed, "a|e"));
	CHECK (ed.yank.len == 3);
	CHECK (ed.session.cursor == 1);

	/* the selection can go upwards, y keeps the lines and ends it */
	test_normal (&ed, "u");
	CHECK (texts_are (&ed, "a|b|c|d|e"));
	test_normal (&ed, "G");
	test_normal (&ed, "v");
	test_normal (&ed, "2k");
	test_normal (&ed, "y");
	CHECK (test_editing (&ed));
	CHECK (texts_are (&ed, "a|b|c|d|e"));
	CHECK (ed.yank.len == 3);
	CHECK (ed.session.cursor == 2);

	/* Esc and v leave without doing anything */
	test_normal (&ed, "v");
	test_normal (&ed, "j");
	press (&ed, ML_KEY_ESCAPE);
	CHECK (test_editing (&ed) && texts_are (&ed, "a|b|c|d|e"));
	test_normal (&ed, "v");
	test_normal (&ed, "v");
	CHECK (test_editing (&ed));

	/* p replaces the selection by the register, which stays */
	test_normal (&ed, "gg");
	test_normal (&ed, "yy");
	test_normal (&ed, "j");
	test_normal (&ed, "v");
	test_normal (&ed, "j");
	test_normal (&ed, "p");
	CHECK (test_editing (&ed));
	CHECK (texts_are (&ed, "a|a|d|e"));
	CHECK (ed.yank.len == 1);

	/* an empty session cannot be selected in */
	test_normal (&ed, "gg");
	test_normal (&ed, "4dd");
	CHECK (ed.session.lines.len == 0);
	test_normal (&ed, "v");
	CHECK (test_editing (&ed));

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
	test_normal (&ed, "G");
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "a|b|c|d|e") && ed.session.cursor == 4);

	/* a selection moves as a block, and stays selected */
	test_normal (&ed, "gg");
	test_normal (&ed, "v");
	test_normal (&ed, "j");
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "c|d|a|b|e"));
	CHECK (ed.selecting);
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "c|d|e|a|b"));
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "c|d|e|a|b"));
	test_normal (&ed, "d");
	CHECK (texts_are (&ed, "c|d|e"));

	ml_editor_free (&ed);
}

static void test_undo_keys (void)
{
	MlEditor ed;

	five_lines (&ed);

	/* u takes back one user action, Ctrl+r does it again */
	test_normal (&ed, "dd");
	test_normal (&ed, "dd");
	CHECK (texts_are (&ed, "c|d|e"));
	test_normal (&ed, "u");
	CHECK (texts_are (&ed, "b|c|d|e") && ed.session.cursor == 0);
	CHECK (status_is (&ed, "-- MATH -- algebra>  undone"));
	test_normal (&ed, "u");
	CHECK (texts_are (&ed, "a|b|c|d|e"));
	test_normal (&ed, "u");
	CHECK (status_is (&ed, "-- MATH -- algebra>  nothing to undo"));
	press_ctrl (&ed, 'r');
	CHECK (texts_are (&ed, "b|c|d|e"));
	CHECK (status_is (&ed, "-- MATH -- algebra>  redone"));
	press_ctrl (&ed, 'r');
	CHECK (texts_are (&ed, "c|d|e"));
	press_ctrl (&ed, 'r');
	CHECK (status_is (&ed, "-- MATH -- algebra>  nothing to redo"));

	/* a command on many lines is one action */
	test_normal (&ed, "gg");
	test_normal (&ed, "vjd");
	CHECK (texts_are (&ed, "e"));
	test_normal (&ed, "u");
	CHECK (texts_are (&ed, "c|d|e"));
	press_ctrl (&ed, 'r');
	CHECK (texts_are (&ed, "e"));
	test_normal (&ed, "u");

	/* typing a line and editing one are undone too */
	test_normal (&ed, "G");
	test_normal (&ed, "o");
	type (&ed, "new");
	test_leave (&ed);
	CHECK (texts_are (&ed, "c|d|e|new"));
	test_normal (&ed, "u");
	CHECK (texts_are (&ed, "c|d|e"));
	test_normal (&ed, "gg");
	press (&ed, ML_KEY_END);
	type (&ed, "x");
	test_leave (&ed);
	CHECK (texts_are (&ed, "cx|d|e"));
	test_normal (&ed, "u");
	CHECK (texts_are (&ed, "c|d|e"));

	/* a move and a paste */
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "d|c|e"));
	test_normal (&ed, "u");
	CHECK (texts_are (&ed, "c|d|e"));
	test_normal (&ed, "yy");
	test_normal (&ed, "p");
	CHECK (texts_are (&ed, "c|c|d|e"));
	test_normal (&ed, "u");
	CHECK (texts_are (&ed, "c|d|e"));

	/* the commands do the same */
	test_normal (&ed, "gg");
	test_normal (&ed, "dd");
	test_normal (&ed, ":undo");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "c|d|e"));
	test_normal (&ed, ":redo");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "d|e"));

	/* a new change forgets what could be redone */
	test_normal (&ed, "u");
	test_normal (&ed, "dd");
	press_ctrl (&ed, 'r');
	CHECK (texts_are (&ed, "d|e"));

	ml_editor_free (&ed);
}

static void test_range_commands (void)
{
	MlEditor ed;

	five_lines (&ed);

	/* :d, :y with a range, ranges with . and $ and % */
	test_normal (&ed, ":2,3d");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "a|d|e") && ed.yank.len == 2);
	test_normal (&ed, ":$d");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "a|d"));
	test_normal (&ed, "u");
	test_normal (&ed, "u");
	CHECK (texts_are (&ed, "a|b|c|d|e"));
	test_normal (&ed, ":%y");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.yank.len == 5);
	test_normal (&ed, ":.y");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.yank.len == 1);

	/* :m puts the lines after the given line, 0 is the top */
	test_normal (&ed, ":1m3");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "b|c|a|d|e") && ed.session.cursor == 2);
	test_normal (&ed, ":4,5m 0");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "d|e|b|c|a") && ed.session.cursor == 0);
	test_normal (&ed, ":1,2m$");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "b|c|a|d|e"));
	test_normal (&ed, ":2m2");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "b|c|a|d|e"));
	test_normal (&ed, ":2m1");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "b|c|a|d|e"));

	/* :t copies */
	test_normal (&ed, ":1,2t$");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "b|c|a|d|e|b|c"));
	test_normal (&ed, ":3t0");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "a|b|c|a|d|e|b|c"));
	test_normal (&ed, ":copy 1");
	press (&ed, ML_KEY_ENTER);
	CHECK (texts_are (&ed, "a|a|b|c|a|d|e|b|c") || 1);

	/* the commands without a range work on the cursor line */
	test_normal (&ed, ":2");
	press (&ed, ML_KEY_ENTER);
	test_normal (&ed, ":delete");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.yank.len == 1);

	/* going to a line still works, also with a range */
	test_normal (&ed, ":3,5");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 4);
	test_normal (&ed, ":$");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == ed.session.lines.len - 1);

	/* mistakes are reported, not applied */
	test_normal (&ed, ":m");
	press (&ed, ML_KEY_ENTER);
	CHECK (
		status_is (
			&ed,
			"-- MATH -- algebra>  m needs a line number to move after"
		)
	);
	test_normal (&ed, ":t x");
	press (&ed, ML_KEY_ENTER);
	CHECK (
		status_is (
			&ed,
			"-- MATH -- algebra>  t needs a line number to copy after"
		)
	);

	/* from Visual the command line starts with the selection as range */
	test_normal (&ed, "gg");
	test_normal (&ed, "vj");
	test_normal (&ed, ":");
	CHECK (ed.state == ML_STATE_COMMAND);
	CHECK (strcmp (ml_str_cstr (ed.edit.text), "1,2") == 0);
	type (&ed, "d");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.yank.len == 2);

	ml_editor_free (&ed);
}

static void test_note_rewrites (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	test_leave (&ed);
	test_normal (&ed, "N");

	/* the rewrites of the math lines work in a note, Markdown is left alone */
	type (&ed, "The area is pi r^2, **bold** and *it* and \\alpha <= 3 ");
	test_leave (&ed);
	CHECK (
		line_is (
			&ed,
			0,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			"The area is \xCF\x80 r\xC2\xB2, **bold** and *it* and "
			"\xCE\xB1 \xE2\x89\xA4 3 "
		)
	);

	/* backspace right after a rewrite brings the typed text back */
	test_normal (&ed, "N");
	type (&ed, "pi ");
	press (&ed, ML_KEY_BACKSPACE);
	press (&ed, ML_KEY_BACKSPACE);
	type (&ed, "e");
	test_leave (&ed);
	CHECK (line_is (&ed, 1, ML_LINE_NOTE, ML_DOMAIN_ALGEBRA, "pie"));

	/* the logic symbols and lambda are for their domains only */
	test_normal (&ed, "N");
	type (&ed, "a && b || ~c lambda x. ");
	test_leave (&ed);
	CHECK (
		line_is (
			&ed,
			2,
			ML_LINE_NOTE,
			ML_DOMAIN_ALGEBRA,
			"a && b || ~c lambda x. "
		)
	);

	ml_editor_free (&ed);
}

/* Ctrl+Enter: starts a block of the session domain. */
static void press_block (MlEditor *ed)
{
	MlKey k = { .code = ML_KEY_ENTER, .cp = 0, .mods = ML_MOD_CTRL };

	CHECK (ml_editor_handle_key (ed, k) == ML_OK);
	CHECK (!ed->picking);
}

/* A note, a block of three math lines, a blank line, a block of two. */
static void block_lines (MlEditor *ed)
{
	ml_editor_init (ed);
	CHECK (ml_editor_begin (ed) == ML_OK);
	test_leave (ed);
	test_normal (ed, "N");
	type (ed, "intro");
	test_leave (ed);
	test_normal (ed, "o");
	type (ed, "a");
	press (ed, ML_KEY_ENTER);
	type (ed, "b");
	press (ed, ML_KEY_ENTER);
	type (ed, "c");
	press (ed, ML_KEY_ENTER);
	press (ed, ML_KEY_ENTER);
	press_block (ed);
	type (ed, "d");
	press (ed, ML_KEY_ENTER);
	type (ed, "e");
	test_leave (ed);
	ml_session_clear_history (&ed->session);
	test_normal (ed, "gg");
}

static void test_empty_block_backspace (void)
{
	MlEditor ed;

	/* a block started under a note goes away with Backspace */
	ml_editor_init (&ed);
	CHECK (ml_editor_begin_note (&ed) == ML_OK);
	type (&ed, "intro");
	press (&ed, ML_KEY_ENTER);
	press_block (&ed);
	CHECK (ed.state == ML_STATE_MATH && ed.edit_index == 1);
	press (&ed, ML_KEY_BACKSPACE);
	CHECK (ed.state == ML_STATE_NOTE && ed.edit_index == 0);
	CHECK (ed.edit.cursor == 5);

	/* one at the very top turns into an empty note */
	ml_editor_free (&ed);
	ml_editor_init (&ed);
	CHECK (ml_editor_begin_note (&ed) == ML_OK);
	press_block (&ed);
	CHECK (ed.state == ML_STATE_MATH && ed.edit_index == 0);
	press (&ed, ML_KEY_BACKSPACE);
	CHECK (ed.state == ML_STATE_NOTE && ed.edit_kind == ML_LINE_NOTE);
	CHECK (ed.edit.text.len == 0);

	/* an empty line in the middle of a block goes back into the line above */
	ml_editor_free (&ed);
	ml_editor_init (&ed);
	CHECK (ml_editor_begin_note (&ed) == ML_OK);
	press_block (&ed);
	type (&ed, "a");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.state == ML_STATE_MATH && ed.edit_index == 1);
	press (&ed, ML_KEY_BACKSPACE);
	CHECK (ed.state == ML_STATE_MATH && ed.edit_index == 0);
	CHECK (ed.edit.cursor == 1);

	/* the same for an empty block that is already in the session */
	test_leave (&ed);
	test_normal (&ed, "dd");
	ml_editor_free (&ed);
	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	press (&ed, ML_KEY_BACKSPACE);
	CHECK (ed.state == ML_STATE_NOTE && ed.edit_kind == ML_LINE_NOTE);
	ml_editor_free (&ed);
}

static void test_folds (void)
{
	MlEditor ed;

	block_lines (&ed);
	CHECK (texts_are (&ed, "intro|a|b|c||d|e"));

	/* a single line has nothing to fold */
	test_normal (&ed, "za");
	CHECK (!ml_session_block (&ed.session, 0).folded);
	CHECK (
		status_is (
			&ed,
			"-- NOTE -- algebra>  nothing to fold: the block is one line"
		)
	);

	/* za folds the block under the cursor, the cursor stays on its start */
	test_normal (&ed, "j");
	test_normal (&ed, "za");
	CHECK (ml_session_block (&ed.session, 1).folded);
	CHECK (ed.session.cursor == 1);

	/* j passes the whole block, k comes back to its start */
	test_normal (&ed, "j");
	CHECK (ed.session.cursor == 4);
	test_normal (&ed, "k");
	CHECK (ed.session.cursor == 1);
	test_normal (&ed, "3j");
	CHECK (ed.session.cursor == 6);
	test_normal (&ed, "2k");
	CHECK (ed.session.cursor == 4);
	test_normal (&ed, "k");
	CHECK (ed.session.cursor == 1);
	test_normal (&ed, "G");
	test_normal (&ed, "4k");
	CHECK (ed.session.cursor == 0);

	/* the cursor is never left inside the hidden lines */
	test_normal (&ed, ":3");
	press (&ed, ML_KEY_ENTER);
	CHECK (ed.session.cursor == 1);

	/* za again opens it; zc closes, zo opens */
	test_normal (&ed, "za");
	CHECK (!ml_session_block (&ed.session, 1).folded);
	test_normal (&ed, "zc");
	CHECK (ml_session_block (&ed.session, 1).folded);
	test_normal (&ed, "zo");
	CHECK (!ml_session_block (&ed.session, 1).folded);

	/* zM closes every block of more than one line, zR opens them all */
	test_normal (&ed, "zM");
	CHECK (ml_session_block (&ed.session, 1).folded);
	CHECK (ml_session_block (&ed.session, 5).folded);
	CHECK (!ml_session_block (&ed.session, 0).folded);
	CHECK (!ml_session_block (&ed.session, 4).folded);
	test_normal (&ed, "zR");
	CHECK (!ml_session_block (&ed.session, 1).folded);
	CHECK (!ml_session_block (&ed.session, 5).folded);

	/* ]] and [[ go from block to block */
	test_normal (&ed, "gg");
	test_normal (&ed, "]]");
	CHECK (ed.session.cursor == 1);
	test_normal (&ed, "]]");
	CHECK (ed.session.cursor == 4);
	test_normal (&ed, "]]");
	CHECK (ed.session.cursor == 5);
	test_normal (&ed, "]]");
	CHECK (ed.session.cursor == 5);
	test_normal (&ed, "j");
	test_normal (&ed, "[[");
	CHECK (ed.session.cursor == 5);
	test_normal (&ed, "[[");
	CHECK (ed.session.cursor == 4);
	test_normal (&ed, "[[");
	CHECK (ed.session.cursor == 1);

	ml_editor_free (&ed);
}

static void test_fold_operations (void)
{
	MlEditor ed;

	block_lines (&ed);

	/* dd on a folded block is the whole block, and one undo brings it back */
	test_normal (&ed, "j");
	test_normal (&ed, "za");
	test_normal (&ed, "dd");
	CHECK (texts_are (&ed, "intro||d|e"));
	CHECK (ed.yank.len == 3);
	test_normal (&ed, "u");
	CHECK (texts_are (&ed, "intro|a|b|c||d|e"));

	/* yy and p take the block, paste goes after it */
	test_normal (&ed, "gg");
	test_normal (&ed, "j");
	test_normal (&ed, "yy");
	CHECK (ed.yank.len == 3);
	test_normal (&ed, "p");
	CHECK (texts_are (&ed, "intro|a|b|c|a|b|c||d|e"));

	/* Alt+Down moves the block as one */
	test_normal (&ed, "u");
	test_normal (&ed, "gg");
	test_normal (&ed, "j");
	CHECK (ml_session_block (&ed.session, 1).folded);
	press_mod (&ed, ML_KEY_DOWN, ML_MOD_ALT);
	CHECK (texts_are (&ed, "intro||a|b|c|d|e"));
	CHECK (ed.session.cursor == 2);

	/* editing a line of a folded block opens the block */
	press (&ed, ML_KEY_ENTER);
	CHECK (!ml_session_block (&ed.session, 2).folded);
	test_leave (&ed);

	/* a new line below a folded block goes after the block */
	test_normal (&ed, "za");
	CHECK (ml_session_block (&ed.session, 2).folded);
	press (&ed, ML_KEY_DOWN);
	type (&ed, "note");
	test_leave (&ed);
	CHECK (texts_are (&ed, "intro||a|b|c|d|e|note"));

	/* a fold is no change: not undone, not saved */
	ml_session_clear_history (&ed.session);
	ed.session.dirty = 0;
	test_normal (&ed, "gg");
	test_normal (&ed, ":3");
	press (&ed, ML_KEY_ENTER);
	CHECK (ml_session_block (&ed.session, 2).folded);
	test_normal (&ed, "za");
	CHECK (!ml_session_block (&ed.session, 2).folded);
	test_normal (&ed, "u");
	CHECK (!ml_session_block (&ed.session, 2).folded);
	CHECK (!ed.session.dirty);

	ml_editor_free (&ed);
}

static void test_paging (void)
{
	MlEditor ed;
	MlTextRange sel;

	ml_editor_init (&ed);
	ml_editor_set_page (&ed, 3);
	CHECK (ml_editor_begin_note (&ed) == ML_OK);
	type (&ed, "aa");

	for (uint32_t i = 0; i < 7; i++)
	{
		press (&ed, ML_KEY_ENTER);
		type (&ed, "bb");
	}

	/* eight lines; the cursor is on the last one */
	CHECK (ed.edit_index == 7);
	press (&ed, ML_KEY_PAGE_UP);
	CHECK (ed.edit_index == 4 && ed.edit.cursor == 2);
	press (&ed, ML_KEY_PAGE_UP);
	press (&ed, ML_KEY_PAGE_UP);
	CHECK (ed.edit_index == 0);
	press (&ed, ML_KEY_PAGE_UP);
	CHECK (ed.edit_index == 0 && ed.edit.cursor == 0);
	press (&ed, ML_KEY_PAGE_DOWN);
	CHECK (ed.edit_index == 3);
	press (&ed, ML_KEY_PAGE_DOWN);
	press (&ed, ML_KEY_PAGE_DOWN);
	CHECK (ed.edit_index == 7);
	press (&ed, ML_KEY_HOME);
	press (&ed, ML_KEY_PAGE_DOWN);
	CHECK (ed.edit_index == 7 && ed.edit.cursor == 2);

	/* Shift selects the page */
	press (&ed, ML_KEY_HOME);
	shift_key (&ed, ML_KEY_PAGE_UP);
	sel = ml_editor_selection (&ed);
	CHECK (sel.valid && sel.first_line == 4 && sel.last_line == 7);
	CHECK (sel.first_byte == 0 && sel.last_byte == 0);

	/* Ctrl+Left at the start of a line goes to the end of the one before */
	press (&ed, ML_KEY_RIGHT);
	press (&ed, ML_KEY_HOME);
	test_key (&ed, ML_KEY_LEFT, 0, ML_MOD_CTRL);
	CHECK (ed.edit_index == 6 && ed.edit.cursor == 2);
	test_key (&ed, ML_KEY_RIGHT, 0, ML_MOD_CTRL);
	CHECK (ed.edit_index == 7 && ed.edit.cursor == 0);

	ml_editor_free (&ed);
}

static MlHit hit_at (uint64_t line, uint64_t byte, int32_t folded)
{
	MlHit h = {
		.valid  = 1,
		.folded = folded,
		.line   = line,
		.byte   = byte
	};

	return h;
}

static void mouse (MlEditor *ed, MlMouseAction action, MlHit hit)
{
	MlKey k = {
		.code = ML_KEY_MOUSE,
		.cp   = action,
		.mods = 0,
		.col  = 1,
		.row  = 1
	};

	CHECK (ml_editor_mouse (ed, k, hit) == ML_OK);
}

static void test_mouse (void)
{
	MlEditor    ed;
	MlTextRange sel;
	MlHit       none = { .valid = 0, .folded = 0, .line = 0, .byte = 0 };

	ml_editor_init (&ed);
	CHECK (ml_editor_begin_note (&ed) == ML_OK);
	type (&ed, "first line");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "second");
	press (&ed, ML_KEY_ENTER);
	type (&ed, "third one");

	/* a press puts the cursor there, in another line too */
	mouse (&ed, ML_MOUSE_PRESS, hit_at (0, 6, 0));
	CHECK (ed.edit_index == 0 && ed.edit.cursor == 6 && !ed.selecting);
	mouse (&ed, ML_MOUSE_RELEASE, hit_at (0, 6, 0));

	/* a drag selects from the press to the cursor, across lines */
	mouse (&ed, ML_MOUSE_PRESS, hit_at (0, 6, 0));
	mouse (&ed, ML_MOUSE_DRAG, hit_at (1, 3, 0));
	mouse (&ed, ML_MOUSE_DRAG, hit_at (2, 5, 0));
	mouse (&ed, ML_MOUSE_RELEASE, hit_at (2, 5, 0));
	sel = ml_editor_selection (&ed);
	CHECK (sel.valid && sel.first_line == 0 && sel.first_byte == 6);
	CHECK (sel.last_line == 2 && sel.last_byte == 5);

	/* a drag back above the press selects the other way */
	mouse (&ed, ML_MOUSE_PRESS, hit_at (1, 3, 0));
	mouse (&ed, ML_MOUSE_DRAG, hit_at (0, 2, 0));
	sel = ml_editor_selection (&ed);
	CHECK (sel.valid && sel.first_line == 0 && sel.first_byte == 2);
	CHECK (sel.last_line == 1 && sel.last_byte == 3);
	mouse (&ed, ML_MOUSE_RELEASE, hit_at (0, 2, 0));

	/* typing replaces the selected text */
	type (&ed, "X");
	CHECK (edit_is (&ed, "fiXond"));

	/* a drag without a press, and clicks on nothing, change nothing */
	mouse (&ed, ML_MOUSE_DRAG, hit_at (0, 1, 0));
	CHECK (!ed.selecting);
	mouse (&ed, ML_MOUSE_PRESS, none);
	CHECK (!ed.mouse_down && !ed.selecting);

	/* the wheel moves the cursor three lines */
	mouse (&ed, ML_MOUSE_PRESS, hit_at (0, 0, 0));
	mouse (&ed, ML_MOUSE_RELEASE, hit_at (0, 0, 0));
	mouse (&ed, ML_MOUSE_WHEEL_DOWN, none);
	CHECK (ed.edit_index == 1);
	mouse (&ed, ML_MOUSE_WHEEL_UP, none);
	CHECK (ed.edit_index == 0);

	ml_editor_free (&ed);
}

/* :fix units makes the algebra lines that fail on units physics lines. */
static void test_fix_units (void)
{
	MlEditor ed;

	ml_editor_init (&ed);
	CHECK (
		ml_session_append (
			&ed.session,
			ML_LINE_MATH,
			ML_DOMAIN_ALGEBRA,
			ml_slice_cstr ("5 km + 300 m")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&ed.session,
			ML_LINE_MATH,
			ML_DOMAIN_ALGEBRA,
			ml_slice_cstr ("2 + 3")
		) == ML_OK
	);
	CHECK (
		ml_session_append (
			&ed.session,
			ML_LINE_MATH,
			ML_DOMAIN_ALGEBRA,
			ml_slice_cstr ("x + 1")
		) == ML_OK
	);
	CHECK (ml_results_update (&ed.results, &ed.session, 0) == ML_OK);

	/* the error of the first line says what is wrong */
	CHECK (ml_results_get (&ed.results, 0).kind == ML_RESULT_ERROR);
	CHECK (
		ml_slice_equal (
			ml_results_get (&ed.results, 0).code,
			ml_slice_cstr ("units_in_algebra")
		)
	);
	CHECK (ml_results_get (&ed.results, 1).kind == ML_RESULT_VALUE);
	CHECK (
		!ml_slice_equal (
			ml_results_get (&ed.results, 2).code,
			ml_slice_cstr ("units_in_algebra")
		)
	);

	test_normal (&ed, ":fix units");
	press (&ed, ML_KEY_ENTER);
	CHECK (ml_session_get (&ed.session, 0).domain == ML_DOMAIN_PHYSICS);
	CHECK (ml_session_get (&ed.session, 1).domain == ML_DOMAIN_ALGEBRA);
	CHECK (ml_session_get (&ed.session, 2).domain == ML_DOMAIN_ALGEBRA);
	CHECK (ml_results_get (&ed.results, 0).kind == ML_RESULT_VALUE);
	CHECK (
		ml_slice_equal (
			ml_results_get (&ed.results, 0).text,
			ml_slice_cstr ("5300 m")
		)
	);
	CHECK (strstr (ml_str_cstr (ed.message), "1 lines") != NULL);

	ml_editor_free (&ed);
}

/*
 * Typing over a selection that ends at the cursor: the word before the
 * selection is completed after the selected text is gone (found by the
 * fuzzer: "^7" with the 7 selected, then "<", left half a character).
 */
static void test_type_over_selection (void)
{
	MlEditor ed;
	MlKey    sel = { .code = ML_KEY_RIGHT, .cp = 0, .mods = ML_MOD_SHIFT };

	ml_editor_init (&ed);
	CHECK (ml_editor_begin (&ed) == ML_OK);
	type (&ed, "7");
	press (&ed, ML_KEY_HOME);
	type (&ed, "^");
	CHECK (ml_editor_handle_key (&ed, sel) == ML_OK);
	type (&ed, "<");
	CHECK (ml_utf8_valid (ml_str_view (ed.edit.text)));
	CHECK (edit_is (&ed, "^<"));
	ml_editor_free (&ed);
}

void run_editor_tests (void)
{
	test_type_over_selection ();
	test_fix_units ();
	test_insert ();
	test_note ();
	test_first_note ();
	test_note_freeform ();
	test_ctrl_z ();
	test_modeless ();
	test_selection ();
	test_selection_edit ();
	test_esc_leaves_block ();
	test_pick_block ();
	test_navigation ();
	test_yank_paste ();
	test_edit_line ();
	test_domains ();
	test_domain_cycle ();
	test_commands ();
	test_look_commands ();
	test_files ();
	test_counts ();
	test_visual ();
	test_move_lines ();
	test_undo_keys ();
	test_range_commands ();
	test_note_rewrites ();
	test_empty_block_backspace ();
	test_folds ();
	test_fold_operations ();
	test_paging ();
	test_mouse ();
}
