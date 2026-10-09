#include "test.h"

#include <matlog/ui/lineedit.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static MlKey key (MlKeyCode code, uint32_t cp, uint32_t mods)
{
	MlKey k =
	{
		.code = code,
		.cp   = cp,
		.mods = mods
	};

	return k;
}

static int32_t is_text (const MlLineEdit *e, const char *expected)
{
	return ml_slice_equal (ml_str_view (e->text), lit (expected));
}

static void test_basic_edit (void)
{
	MlLineEdit e;

	ml_lineedit_init (&e);
	CHECK (is_text (&e, "") && e.cursor == 0);

	CHECK (ml_lineedit_insert (&e, lit ("abc")) == ML_OK);
	CHECK (is_text            (&e, "abc") && e.cursor == 3);

	ml_lineedit_left (&e);
	CHECK (e.cursor == 2);
	CHECK (ml_lineedit_insert_cp (&e, 'X') == ML_OK);
	CHECK (is_text               (&e, "abXc") && e.cursor == 3);

	ml_lineedit_home (&e);
	CHECK (e.cursor == 0);
	ml_lineedit_left (&e);
	CHECK (e.cursor == 0);
	ml_lineedit_backspace (&e);
	CHECK (is_text (&e, "abXc"));

	ml_lineedit_delete (&e);
	CHECK (is_text (&e, "bXc") && e.cursor == 0);

	ml_lineedit_end (&e);
	CHECK (e.cursor == 3);
	ml_lineedit_right (&e);
	CHECK (e.cursor == 3);
	ml_lineedit_delete (&e);
	CHECK (is_text (&e, "bXc"));
	ml_lineedit_backspace (&e);
	CHECK (is_text (&e, "bX") && e.cursor == 2);

	CHECK (ml_lineedit_insert_cp (&e, 0xD800) == ML_ERR_UTF8);

	ml_lineedit_clear (&e);
	CHECK (is_text (&e, "") && e.cursor == 0);

	CHECK (ml_lineedit_set (&e, lit ("hello")) == ML_OK);
	CHECK (is_text         (&e, "hello") && e.cursor == 5);

	ml_lineedit_free (&e);
}

static void test_utf8_edit (void)
{
	MlLineEdit e;

	ml_lineedit_init (&e);

	/* pi, square root, x: 2 + 3 + 1 bytes */
	CHECK (ml_lineedit_set (&e, lit ("\xCF\x80\xE2\x88\x9A" "x")) == ML_OK);
	CHECK (e.cursor == 6);

	ml_lineedit_left (&e);
	CHECK (e.cursor == 5);
	ml_lineedit_left (&e);
	CHECK (e.cursor == 2);
	ml_lineedit_left (&e);
	CHECK (e.cursor == 0);

	ml_lineedit_right (&e);
	CHECK (e.cursor == 2);
	ml_lineedit_right (&e);
	CHECK (e.cursor == 5);

	/* backspace removes the whole code point */
	ml_lineedit_backspace (&e);
	CHECK (is_text (&e, "\xCF\x80x") && e.cursor == 2);
	ml_lineedit_home (&e);
	ml_lineedit_delete (&e);
	CHECK (is_text (&e, "x") && e.cursor == 0);

	/* wide characters and combining marks */
	CHECK (ml_lineedit_set (&e, lit ("a\xE4\xB8\xAD" "e\xCC\x81")) == ML_OK);
	CHECK (ml_lineedit_width      (&e) == 4);
	CHECK (ml_lineedit_cursor_col (&e) == 4);
	ml_lineedit_home (&e);
	CHECK (ml_lineedit_cursor_col (&e) == 0);
	ml_lineedit_right (&e);
	ml_lineedit_right (&e);
	CHECK (ml_lineedit_cursor_col (&e) == 3);

	ml_lineedit_free (&e);
}

static void test_words (void)
{
	MlLineEdit e;

	ml_lineedit_init (&e);
	CHECK (ml_lineedit_set (&e, lit ("foo + bar_1*(\xCF\x80 x)")) == ML_OK);

	ml_lineedit_home (&e);
	ml_lineedit_word_right (&e);
	CHECK (e.cursor == 3);
	ml_lineedit_word_right (&e);
	CHECK (e.cursor == 11);
	ml_lineedit_word_right (&e);
	CHECK (e.cursor == 15);
	ml_lineedit_word_right (&e);
	CHECK (e.cursor == 17);
	ml_lineedit_word_right (&e);
	CHECK (e.cursor == e.text.len);
	ml_lineedit_word_right (&e);
	CHECK (e.cursor == e.text.len);

	ml_lineedit_word_left (&e);
	CHECK (e.cursor == 16);
	ml_lineedit_word_left (&e);
	CHECK (e.cursor == 13);
	ml_lineedit_word_left (&e);
	CHECK (e.cursor == 6);
	ml_lineedit_word_left (&e);
	CHECK (e.cursor == 0);
	ml_lineedit_word_left (&e);
	CHECK (e.cursor == 0);

	/* deleting words and ranges */
	CHECK (ml_lineedit_set (&e, lit ("one two three")) == ML_OK);
	ml_lineedit_delete_word_left (&e);
	CHECK (is_text (&e, "one two ") && e.cursor == 8);
	ml_lineedit_delete_word_left (&e);
	CHECK (is_text (&e, "one ") && e.cursor == 4);

	ml_lineedit_home (&e);
	ml_lineedit_delete_word_right (&e);
	CHECK (is_text (&e, " ") && e.cursor == 0);

	CHECK (ml_lineedit_set (&e, lit ("abcdef")) == ML_OK);
	ml_lineedit_left (&e);
	ml_lineedit_left (&e);
	ml_lineedit_kill_to_end (&e);
	CHECK (is_text (&e, "abcd") && e.cursor == 4);
	ml_lineedit_left (&e);
	ml_lineedit_kill_to_start (&e);
	CHECK (is_text (&e, "d") && e.cursor == 0);

	ml_lineedit_free (&e);
}

static void test_keys (void)
{
	MlLineEdit   e;
	MlEditResult r;

	ml_lineedit_init (&e);

	r = ml_lineedit_handle_key (&e, key (ML_KEY_CHAR, 'h', 0));
	CHECK (r.result == ML_OK && r.handled);
	r = ml_lineedit_handle_key (&e, key (ML_KEY_CHAR, 0x03C0, 0));
	CHECK (r.handled);
	r = ml_lineedit_handle_key (&e, key (ML_KEY_CHAR, 'i', 0));
	CHECK (r.handled && is_text (&e, "h\xCF\x80i"));

	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_LEFT, 0, 0)
		).handled && e.cursor == 3
	);
	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_HOME, 0, 0)
		).handled && e.cursor == 0
	);
	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_END, 0, 0)
		).handled && e.cursor == 4
	);
	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_CHAR, 'a', ML_MOD_CTRL)
		).handled && e.cursor == 0
	);
	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_CHAR, 'e', ML_MOD_CTRL)
		).handled && e.cursor == 4
	);
	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_BACKSPACE, 0, 0)
		).handled && is_text (&e, "h\xCF\x80")
	);
	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_BACKSPACE, 0, ML_MOD_CTRL)
		).handled && is_text (&e, "")
	);

	CHECK (ml_lineedit_set (&e, lit ("ab cd")) == ML_OK);
	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_LEFT, 0, ML_MOD_CTRL)
		).handled && e.cursor == 3
	);
	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_DELETE, 0, 0)
		).handled && is_text (&e, "ab d")
	);
	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_CHAR, 'k', ML_MOD_CTRL)
		).handled && is_text (&e, "ab ")
	);
	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_CHAR, 'w', ML_MOD_CTRL)
		).handled && is_text (&e, "")
	);
	CHECK (ml_lineedit_set (&e, lit ("xyz")) == ML_OK);
	CHECK (
		ml_lineedit_handle_key (
			&e,
			key (ML_KEY_CHAR, 'u', ML_MOD_CTRL)
		).handled && is_text (&e, "")
	);

	/* Alt+arrows belong to the editor (domain cycling): not handled here */
	CHECK (ml_lineedit_set (&e, lit ("ab cd")) == ML_OK);
	CHECK (
		!ml_lineedit_handle_key (&e, key (ML_KEY_LEFT, 0, ML_MOD_ALT)).handled
	);
	CHECK (
		!ml_lineedit_handle_key (&e, key (ML_KEY_RIGHT, 0, ML_MOD_ALT)).handled
	);
	CHECK (e.cursor == 5);
	CHECK (
		ml_lineedit_handle_key (&e, key (ML_KEY_LEFT, 0, ML_MOD_CTRL)).handled
	);
	CHECK (e.cursor == 3);
	CHECK (
		ml_lineedit_handle_key (&e, key (ML_KEY_RIGHT, 0, ML_MOD_CTRL)).handled
	);
	CHECK (e.cursor == 5);
	CHECK (ml_lineedit_set (&e, lit ("")) == ML_OK);

	/* keys that are not editing keys */
	CHECK (!ml_lineedit_handle_key (&e, key (ML_KEY_ENTER, 0, 0)).handled);
	CHECK (!ml_lineedit_handle_key (&e, key (ML_KEY_ESCAPE, 0, 0)).handled);
	CHECK (!ml_lineedit_handle_key (&e, key (ML_KEY_UP, 0, 0)).handled);
	CHECK (!ml_lineedit_handle_key (&e, key (ML_KEY_F1, 0, 0)).handled);
	CHECK (
		!ml_lineedit_handle_key (&e, key (ML_KEY_CHAR, 'x', ML_MOD_ALT)).handled
	);
	CHECK (
		!ml_lineedit_handle_key (
			&e,
			key (ML_KEY_CHAR, 'x', ML_MOD_CTRL)
		).handled
	);
	CHECK (is_text (&e, ""));

	ml_lineedit_free (&e);
}

/* Renders and returns the output as a C string for comparison. */
static MlResult render_to (
	MlStr            *out,
	const MlLineEdit *e,
	const char       *prompt,
	uint32_t         prompt_width,
	uint32_t         cols
)
{
	ml_str_clear (out);
	return ml_lineedit_render (out, e, lit (prompt), prompt_width, cols);
}

static void test_render (void)
{
	MlLineEdit e;
	MlStr      out;

	ml_lineedit_init (&e);
	ml_str_init (&out);

	/* fits: prompt, text, erase to end, cursor after the text */
	CHECK (ml_lineedit_set (&e, lit ("1+2")) == ML_OK);
	CHECK (render_to       (&out, &e, "> ", 2, 20) == ML_OK);
	CHECK (strcmp          (ml_str_cstr (out), "\r> 1+2\x1b[K\r\x1b[5C") == 0);

	/* cursor in the middle */
	ml_lineedit_left (&e);
	CHECK (render_to (&out, &e, "> ", 2, 20) == ML_OK);
	CHECK (strcmp    (ml_str_cstr (out), "\r> 1+2\x1b[K\r\x1b[4C") == 0);

	/* empty text and no prompt: no cursor movement needed */
	ml_lineedit_clear (&e);
	CHECK (render_to (&out, &e, "", 0, 20) == ML_OK);
	CHECK (strcmp    (ml_str_cstr (out), "\r\x1b[K\r") == 0);

	/* the prompt may carry escape codes, only its width counts */
	CHECK (ml_lineedit_set (&e, lit ("x")) == ML_OK);
	CHECK (render_to       (&out, &e, "\x1b[1mm>\x1b[0m ", 3, 20) == ML_OK);
	CHECK (
		strcmp (ml_str_cstr (out), "\r\x1b[1mm>\x1b[0m x\x1b[K\r\x1b[4C") == 0
	);

	/* unicode text: pi is one column */
	CHECK (ml_lineedit_set (&e, lit ("\xCF\x80+1")) == ML_OK);
	CHECK (render_to       (&out, &e, "> ", 2, 20) == ML_OK);
	CHECK (strcmp (ml_str_cstr (out), "\r> \xCF\x80+1\x1b[K\r\x1b[5C") == 0);

	/*
	 * too wide: 10 columns, prompt 2, so 7 text columns; cursor at the end
	 * scrolls
	 */
	CHECK (ml_lineedit_set (&e, lit ("0123456789")) == ML_OK);
	CHECK (render_to       (&out, &e, "> ", 2, 10) == ML_OK);
	CHECK (strcmp (ml_str_cstr (out), "\r> 456789\x1b[K\r\x1b[8C") == 0);

	/* cursor at the start shows the beginning */
	ml_lineedit_home (&e);
	CHECK (render_to (&out, &e, "> ", 2, 10) == ML_OK);
	CHECK (strcmp    (ml_str_cstr (out), "\r> 0123456\x1b[K\r\x1b[2C") == 0);

	/*
	 * wide characters never straddle the edge: 4 text columns of "a" + 3 wide
	 * chars
	 */
	CHECK (
		ml_lineedit_set (
			&e,
			lit ("a\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD")
		) == ML_OK
	);
	ml_lineedit_home (&e);
	CHECK (render_to (&out, &e, "", 0, 6) == ML_OK);
	CHECK (
		strcmp (ml_str_cstr (out), "\ra\xE4\xB8\xAD\xE4\xB8\xAD\x1b[K\r") == 0
	);

	/* combining marks stay with their character */
	CHECK (ml_lineedit_set (&e, lit ("e\xCC\x81" "x")) == ML_OK);
	CHECK (render_to       (&out, &e, "", 0, 20) == ML_OK);
	CHECK (strcmp (ml_str_cstr (out), "\re\xCC\x81x\x1b[K\r\x1b[2C") == 0);

	/* a tiny terminal does not crash or loop */
	CHECK (render_to (&out, &e, "> ", 2, 1) == ML_OK);
	CHECK (render_to (&out, &e, "> ", 2, 0) == ML_OK);

	ml_str_free (&out);
	ml_lineedit_free (&e);
}

void run_lineedit_tests (void)
{
	test_basic_edit ();
	test_utf8_edit ();
	test_words ();
	test_keys ();
	test_render ();
}
