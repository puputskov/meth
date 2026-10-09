#include "test.h"

#ifndef _WIN32

#include <matlog/core/utf8.h>
#include <matlog/platform/posix/key.h>

static MlVtDecode dec (const char *bytes, uint64_t len, int32_t final)
{
	return ml_key_decode_vt (ml_slice (bytes, len), final);
}

/* Decodes a whole sequence given as text and checks the key and its size. */
static int32_t is_key (
	const char *seq,
	uint64_t   len,
	MlKeyCode  code,
	uint32_t   cp,
	uint32_t   mods
)
{
	MlVtDecode d    = dec (seq, len, 1);
	int32_t    same = (
		   !d.incomplete
		&& d.used == len
		&& d.key.code == code
		&& d.key.cp   == cp
		&& d.key.mods == mods
	);

	if (!same)
	{
		printf (
			"  sequence of %d bytes: code %d cp %u mods %u used %d "
			"incomplete %d\n",
			(int32_t)len,
			(int32_t)d.key.code,
			d.key.cp,
			d.key.mods,
			(int32_t)d.used,
			d.incomplete
		);
	}

	return same;
}

static void test_plain (void)
{
	MlVtDecode d;

	CHECK (is_key ("a", 1, ML_KEY_CHAR, 'a', 0));
	CHECK (is_key ("Z", 1, ML_KEY_CHAR, 'Z', 0));
	CHECK (is_key (" ", 1, ML_KEY_CHAR, ' ', 0));
	CHECK (is_key ("\r", 1, ML_KEY_ENTER, 0, 0));
	CHECK (is_key ("\n", 1, ML_KEY_ENTER, 0, 0));
	CHECK (is_key ("\t", 1, ML_KEY_TAB, 0, 0));
	CHECK (is_key ("\x7F", 1, ML_KEY_BACKSPACE, 0, 0));
	CHECK (is_key ("\x08", 1, ML_KEY_BACKSPACE, 0, 0));

	/* Ctrl+letter, Ctrl+C is how the app quits */
	CHECK (is_key ("\x01", 1, ML_KEY_CHAR, 'a', ML_MOD_CTRL));
	CHECK (is_key ("\x03", 1, ML_KEY_CHAR, 'c', ML_MOD_CTRL));
	CHECK (is_key ("\x1A", 1, ML_KEY_CHAR, 'z', ML_MOD_CTRL));

	/* UTF-8: pi, the euro sign, and an emoji */
	CHECK (is_key ("\xCF\x80", 2, ML_KEY_CHAR, 0x03C0, 0));
	CHECK (is_key ("\xE2\x82\xAC", 3, ML_KEY_CHAR, 0x20AC, 0));
	CHECK (is_key ("\xF0\x9F\x98\x80", 4, ML_KEY_CHAR, 0x1F600, 0));

	/* half a character waits for the rest, unless nothing more can come */
	d = dec ("\xE2\x82", 2, 0);
	CHECK (d.incomplete && d.used == 0);
	d = dec ("\xE2\x82", 2, 1);
	CHECK (!d.incomplete && d.used >= 1 && d.key.cp == ML_CP_REPLACEMENT);

	/* only the first key of the bytes is used */
	d = dec ("ab", 2, 0);
	CHECK (d.used == 1 && d.key.cp == 'a');

	/* nothing at all is incomplete */
	d = dec ("", 0, 0);
	CHECK (d.incomplete && d.used == 0);
}

static void test_escape (void)
{
	MlVtDecode d;

	/* Esc is told apart from the arrow keys by waiting */
	d = dec ("\x1b", 1, 0);
	CHECK (d.incomplete && d.used == 0);
	CHECK (is_key ("\x1b", 1, ML_KEY_ESCAPE, 0, 0));
	d = dec ("\x1b\x1b", 2, 0);
	CHECK (!d.incomplete && d.used == 1 && d.key.code == ML_KEY_ESCAPE);

	CHECK (is_key ("\x1b[A", 3, ML_KEY_UP, 0, 0));
	CHECK (is_key ("\x1b[B", 3, ML_KEY_DOWN, 0, 0));
	CHECK (is_key ("\x1b[C", 3, ML_KEY_RIGHT, 0, 0));
	CHECK (is_key ("\x1b[D", 3, ML_KEY_LEFT, 0, 0));
	CHECK (is_key ("\x1bOA", 3, ML_KEY_UP, 0, 0));
	CHECK (is_key ("\x1bOD", 3, ML_KEY_LEFT, 0, 0));
	CHECK (is_key ("\x1b[H", 3, ML_KEY_HOME, 0, 0));
	CHECK (is_key ("\x1b[F", 3, ML_KEY_END, 0, 0));
	CHECK (is_key ("\x1bOH", 3, ML_KEY_HOME, 0, 0));
	CHECK (is_key ("\x1b[1~", 4, ML_KEY_HOME, 0, 0));
	CHECK (is_key ("\x1b[2~", 4, ML_KEY_INSERT, 0, 0));
	CHECK (is_key ("\x1b[3~", 4, ML_KEY_DELETE, 0, 0));
	CHECK (is_key ("\x1b[4~", 4, ML_KEY_END, 0, 0));
	CHECK (is_key ("\x1b[5~", 4, ML_KEY_PAGE_UP, 0, 0));
	CHECK (is_key ("\x1b[6~", 4, ML_KEY_PAGE_DOWN, 0, 0));
	CHECK (is_key ("\x1bOP", 3, ML_KEY_F1, 0, 0));
	CHECK (is_key ("\x1bOS", 3, ML_KEY_F4, 0, 0));
	CHECK (is_key ("\x1b[15~", 5, ML_KEY_F5, 0, 0));
	CHECK (is_key ("\x1b[17~", 5, ML_KEY_F6, 0, 0));
	CHECK (is_key ("\x1b[21~", 5, ML_KEY_F10, 0, 0));
	CHECK (is_key ("\x1b[23~", 5, ML_KEY_F11, 0, 0));
	CHECK (is_key ("\x1b[24~", 5, ML_KEY_F12, 0, 0));

	/* the modifier parameter of xterm */
	CHECK (is_key ("\x1b[1;2A", 6, ML_KEY_UP, 0, ML_MOD_SHIFT));
	CHECK (is_key ("\x1b[1;3D", 6, ML_KEY_LEFT, 0, ML_MOD_ALT));
	CHECK (is_key ("\x1b[1;3C", 6, ML_KEY_RIGHT, 0, ML_MOD_ALT));
	CHECK (is_key ("\x1b[1;5C", 6, ML_KEY_RIGHT, 0, ML_MOD_CTRL));
	CHECK (
		is_key (
			"\x1b[1;7C",
			6,
			ML_KEY_RIGHT,
			0,
			ML_MOD_ALT | ML_MOD_CTRL
		)
	);
	CHECK (is_key ("\x1b[3;5~", 6, ML_KEY_DELETE, 0, ML_MOD_CTRL));
	CHECK (is_key ("\x1b[1;5P", 6, ML_KEY_F1, 0, ML_MOD_CTRL));
	CHECK (is_key ("\x1b[Z", 3, ML_KEY_TAB, 0, ML_MOD_SHIFT));

	/* Alt and a key */
	CHECK (is_key ("\x1b" "a", 2, ML_KEY_CHAR, 'a', ML_MOD_ALT));
	CHECK (is_key ("\x1b\xCF\x80", 3, ML_KEY_CHAR, 0x03C0, ML_MOD_ALT));
	CHECK (is_key ("\x1b\x7F", 2, ML_KEY_BACKSPACE, 0, ML_MOD_ALT));

	/* a sequence that is not finished yet waits, and is dropped at the end */
	d = dec ("\x1b[1;5", 5, 0);
	CHECK (d.incomplete && d.used == 0);
	d = dec ("\x1b[1;5", 5, 1);
	CHECK (!d.incomplete && d.used == 5 && d.key.code == ML_KEY_NONE);
	d = dec ("\x1bO", 2, 0);
	CHECK (d.incomplete);

	/* an unknown sequence is skipped as a whole, not shown as characters */
	d = dec ("\x1b[99X", 5, 0);
	CHECK (!d.incomplete && d.used == 5 && d.key.code == ML_KEY_NONE);
	d = dec ("\x1b[200~", 6, 0);
	CHECK (!d.incomplete && d.used == 6 && d.key.code == ML_KEY_NONE);

	/* garbage that never ends is cut off */
	{
		char junk [64];

		junk [0] = 0x1b;
		junk [1] = '[';

		for (uint64_t i = 2; i < sizeof junk; i++)
		{
			junk [i] = '1';
		}

		d = dec (junk, sizeof junk, 0);
		CHECK (!d.incomplete && d.used > 0 && d.used <= sizeof junk);
	}
}

static void test_csi_u (void)
{
	/* Ctrl+z and Ctrl+Shift+z of the kitty protocol */
	CHECK (is_key ("\x1b[122;5u", 8, ML_KEY_CHAR, 'z', ML_MOD_CTRL));
	CHECK (
		is_key (
			"\x1b[122;6u",
			8,
			ML_KEY_CHAR,
			'z',
			ML_MOD_CTRL | ML_MOD_SHIFT
		)
	);
	CHECK (is_key ("\x1b[27u", 5, ML_KEY_ESCAPE, 0, 0));
	CHECK (is_key ("\x1b[97u", 5, ML_KEY_CHAR, 'a', 0));
}

static void test_mouse (void)
{
	MlVtDecode d = dec ("[<0;12;3M", sizeof "[<0;12;3M" - 1, 1);

	CHECK (d.used == 10 && d.key.code == ML_KEY_MOUSE);
	CHECK (d.key.cp == ML_MOUSE_PRESS && d.key.col == 12 && d.key.row == 3);
	d = dec ("[<32;5;6M", sizeof "[<32;5;6M" - 1, 1);
	CHECK (d.key.code == ML_KEY_MOUSE && d.key.cp == ML_MOUSE_DRAG);
	CHECK (d.key.col == 5 && d.key.row == 6);
	d = dec ("[<0;5;6m", sizeof "[<0;5;6m" - 1, 1);
	CHECK (d.key.code == ML_KEY_MOUSE && d.key.cp == ML_MOUSE_RELEASE);
	d = dec ("[<64;5;6M", sizeof "[<64;5;6M" - 1, 1);
	CHECK (d.key.cp == ML_MOUSE_WHEEL_UP);
	d = dec ("[<65;5;6M", sizeof "[<65;5;6M" - 1, 1);
	CHECK (d.key.cp == ML_MOUSE_WHEEL_DOWN);
	d = dec ("[<2;5;6M", sizeof "[<2;5;6M" - 1, 1);
	CHECK (d.key.code == ML_KEY_NONE && d.used == 9);
	d = dec ("[<4;5;6M", sizeof "[<4;5;6M" - 1, 1);
	CHECK (d.key.mods == ML_MOD_SHIFT);
}

void run_posix_key_tests (void);

void run_posix_key_tests (void)
{
	test_mouse ();
	test_plain ();
	test_escape ();
	test_csi_u ();
}

#else

void run_posix_key_tests (void);

void run_posix_key_tests (void)
{
}

#endif
