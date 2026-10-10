#include "test.h"

#include <meth/core/err.h>
#include <meth/core/slice.h>
#include <meth/core/str.h>
#include <meth/platform/console.h>
#include <meth/platform/fs.h>
#include <meth/platform/key.h>
#ifdef _WIN32
#include <meth/platform/win32/key.h>
#endif
#include <meth/platform/path.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

#ifdef _WIN32

/* Win32 values used to build raw events */
#define VK_ESC   0x1Bu
#define VK_LEFT  0x25u
#define VK_RET   0x0Du
#define VK_F5    0x74u
#define VK_A     0x41u
#define VK_SHIFT 0x10u
#define CTL_SHIFT      0x10u
#define CTL_LEFT_CTRL  0x08u
#define CTL_RIGHT_ALT  0x01u
#define CTL_LEFT_ALT   0x02u

static MlKey decode (uint16_t vk, uint16_t unicode, uint32_t control)
{
	MlRawKey raw = {
		.vk      = vk,
		.unicode = unicode,
		.control = control
	};

	return ml_key_decode_win32 (raw, 0).key;
}

static void test_key_decode (void)
{
	MlKey          k;
	MlDecodeResult d;
	MlRawKey       hi = {
		.vk      = 0,
		.unicode = 0xD83D,
		.control = 0
	};

	MlRawKey lo = {
		.vk      = 0,
		.unicode = 0xDE00,
		.control = 0
	};

	/* Esc is its own key, distinct from the arrows */
	k = decode (VK_ESC, 0x1B, 0);
	CHECK (k.code == ML_KEY_ESCAPE && k.mods == 0);
	k = decode (VK_LEFT, 0, 0x100);
	CHECK (k.code == ML_KEY_LEFT);
	CHECK (decode (VK_LEFT, 0, 0).code != decode (VK_ESC, 0, 0).code);

	k = decode (VK_RET, 0x0D, 0);
	CHECK (k.code == ML_KEY_ENTER);

	/* function keys */
	k = decode (VK_F5, 0, 0);
	CHECK (k.code == ML_KEY_F5);

	/* plain and shifted characters; shift is dropped, it is in the character */
	k = decode (VK_A, 'a', 0);
	CHECK (k.code == ML_KEY_CHAR && k.cp == 'a' && k.mods == 0);
	k = decode (VK_A, 'A', CTL_SHIFT);
	CHECK (k.code == ML_KEY_CHAR && k.cp == 'A' && k.mods == 0);

	/* Ctrl+letter arrives as a control code */
	k = decode (VK_A, 0x01, CTL_LEFT_CTRL);
	CHECK (k.code == ML_KEY_CHAR && k.cp == 'a' && k.mods == ML_MOD_CTRL);
	k = decode ('C', 0x03, CTL_LEFT_CTRL);
	CHECK (k.code == ML_KEY_CHAR && k.cp == 'c' && k.mods == ML_MOD_CTRL);

	/* Alt+letter keeps both */
	k = decode (VK_A, 'a', CTL_LEFT_ALT);
	CHECK (k.code == ML_KEY_CHAR && k.cp == 'a' && k.mods == ML_MOD_ALT);

	/* AltGr (Ctrl+Alt) with a printable character is just the character */
	k = decode ('Q', '@', CTL_LEFT_CTRL | CTL_RIGHT_ALT);
	CHECK (k.code == ML_KEY_CHAR && k.cp == '@' && k.mods == 0);

	/* unicode text such as pi */
	k = decode (0, 0x03C0, 0);
	CHECK (k.code == ML_KEY_CHAR && k.cp == 0x03C0);

	/* space, also with Ctrl */
	k = decode (0x20, 0, 0);
	CHECK (k.code == ML_KEY_CHAR && k.cp == ' ');

	/* modifier alone produces nothing */
	k = decode (VK_SHIFT, 0, CTL_SHIFT);
	CHECK (k.code == ML_KEY_NONE);

	/* surrogate pairs: U+1F600 is D83D DE00 */
	d = ml_key_decode_win32 (hi, 0);
	CHECK (d.key.code == ML_KEY_NONE && d.pending_high == 0xD83D);
	d = ml_key_decode_win32 (lo, d.pending_high);
	CHECK (
		d.key.code == ML_KEY_CHAR && d.key.cp == 0x1F600 && d.pending_high == 0
	);
	d = ml_key_decode_win32 (lo, 0);
	CHECK (d.key.code == ML_KEY_NONE);
}

#endif /* _WIN32 */

static void test_key_format (void)
{
	MlStr s;
	MlKey esc = {
		.code = ML_KEY_ESCAPE,
		.cp   = 0,
		.mods = 0
	};

	MlKey left = {
		.code = ML_KEY_LEFT,
		.cp   = 0,
		.mods = ML_MOD_CTRL | ML_MOD_ALT
	};

	MlKey pi = {
		.code = ML_KEY_CHAR,
		.cp   = 0x03C0,
		.mods = 0
	};

	ml_str_init (&s);
	CHECK (ml_key_format (&s, esc) == ML_OK);
	CHECK (strcmp        (ml_str_cstr (s), "Esc") == 0);

	ml_str_clear (&s);
	CHECK (ml_key_format (&s, left) == ML_OK);
	CHECK (strcmp        (ml_str_cstr (s), "Ctrl+Alt+Left") == 0);

	ml_str_clear (&s);
	CHECK (ml_key_format (&s, pi) == ML_OK);
	CHECK (strcmp        (ml_str_cstr (s), "Char '\xCF\x80' U+03C0") == 0);

	CHECK (strcmp (ml_key_code_name (ML_KEY_RESIZE), "Resize") == 0);
	CHECK (strcmp (ml_key_code_name ((MlKeyCode)999), "Unknown") == 0);
	ml_str_free (&s);
}

static void test_path (void)
{
	MlStr s;

	ml_str_init (&s);

	CHECK (ml_slice_equal (ml_path_name (lit ("a/b/c.mlog")), lit ("c.mlog")));
	CHECK (
		ml_slice_equal (ml_path_name (lit ("a\\b\\c.mlog")), lit ("c.mlog"))
	);
	CHECK (ml_slice_equal (ml_path_name (lit ("c.mlog")), lit ("c.mlog")));
	CHECK (ml_path_name   (lit ("dir/")).len == 0);

	CHECK (ml_slice_equal (ml_path_dir (lit ("a/b/c.mlog")), lit ("a/b")));
	CHECK (ml_slice_equal (ml_path_dir (lit ("a\\c.mlog")), lit ("a")));
	CHECK (ml_path_dir    (lit ("c.mlog")).len == 0);
	CHECK (ml_slice_equal (ml_path_dir (lit ("/c.mlog")), lit ("/")));
	CHECK (ml_slice_equal (ml_path_dir (lit ("dir/")), lit ("dir")));

	CHECK (ml_path_is_absolute  (lit ("/x")));
	CHECK (ml_path_is_absolute  (lit ("\\\\server\\share")));
	CHECK (ml_path_is_absolute  (lit ("C:\\x")));
	CHECK (!ml_path_is_absolute (lit ("x/y")));
	CHECK (!ml_path_is_absolute (lit ("")));

	CHECK (ml_path_join (&s, lit ("a/b"), lit ("c.mlog")) == ML_OK);
	CHECK (strcmp       (ml_str_cstr (s), "a/b/c.mlog") == 0);

	ml_str_clear (&s);
	CHECK (ml_path_join (&s, lit ("a/b/"), lit ("c")) == ML_OK);
	CHECK (strcmp       (ml_str_cstr (s), "a/b/c") == 0);

	ml_str_clear (&s);
	CHECK (ml_path_join (&s, lit (""), lit ("c")) == ML_OK);
	CHECK (strcmp       (ml_str_cstr (s), "c") == 0);

	ml_str_clear (&s);
	CHECK (ml_path_join (&s, lit ("a"), lit ("C:\\abs")) == ML_OK);
	CHECK (strcmp       (ml_str_cstr (s), "C:\\abs") == 0);

	ml_str_free (&s);
}

static void test_fs (void)
{
	MlSlice path = lit ("meth_test_file.tmp");
	MlSlice tmp  = lit ("meth_test_file.tmp.tmp");
	MlStr   s;
	MlStr   big;

	ml_str_init (&s);
	ml_str_init (&big);
	ml_fs_remove (path);

	CHECK (!ml_fs_exists   (path));
	CHECK (ml_fs_read_file (&s, path) == ML_ERR_IO);
	CHECK (s.len == 0);
	CHECK (ml_fs_remove (path) == ML_ERR_IO);

	CHECK (ml_fs_write_file_atomic (path, lit ("hello \xCF\x80\n")) == ML_OK);
	CHECK (ml_fs_exists            (path));
	CHECK (!ml_fs_exists           (tmp));
	CHECK (ml_fs_read_file         (&s, path) == ML_OK);
	CHECK (ml_slice_equal          (ml_str_view (s), lit ("hello \xCF\x80\n")));

	/* replacing an existing file */
	CHECK (ml_fs_write_file_atomic (path, lit ("second")) == ML_OK);
	ml_str_clear (&s);
	CHECK (ml_fs_read_file (&s, path) == ML_OK);
	CHECK (ml_slice_equal  (ml_str_view (s), lit ("second")));

	/* a file larger than the read buffer, and reading appends */
	for (int32_t i = 0; i < 5000; i++)
	{
		CHECK (ml_str_append (&big, lit ("0123456789")) == ML_OK);
	}

	CHECK (ml_fs_write_file_atomic (path, ml_str_view (big)) == ML_OK);
	ml_str_clear (&s);
	ml_str_append (&s, lit ("prefix:"));
	CHECK (ml_fs_read_file (&s, path) == ML_OK);
	CHECK (s.len == 7 + 50000);
	CHECK (strncmp (ml_str_cstr (s), "prefix:0123456789", 17) == 0);

	/* empty file */
	CHECK (ml_fs_write_file_atomic (path, lit ("")) == ML_OK);
	ml_str_clear (&s);
	CHECK (ml_fs_read_file (&s, path) == ML_OK && s.len == 0);

	/* invalid UTF-8 path */
	CHECK (ml_fs_read_file (&s, ml_slice ("\xFF\xFE", 2)) == ML_ERR_UTF8);

	CHECK (ml_fs_remove  (path) == ML_OK);
	CHECK (!ml_fs_exists (path));

	ml_str_free (&big);
	ml_str_free (&s);
}

static void test_fs_list (void)
{
	MlSlice dir = lit ("meth_test_dir.tmp");
	MlStr   names;
	MlStr   a;
	MlStr   b;
	MlStr   c;
	uint64_t stamp;

	ml_str_init (&names);
	ml_str_init (&a);
	ml_str_init (&b);
	ml_str_init (&c);
	CHECK (ml_fs_make_dirs (dir) == ML_OK);
	ml_str_append (&a, dir);
	ml_str_append (&a, lit ("/a.theme"));
	ml_str_append (&b, dir);
	ml_str_append (&b, lit ("/b.theme"));
	ml_str_append (&c, dir);
	ml_str_append (&c, lit ("/c.txt"));
	CHECK (ml_fs_write_file_atomic (ml_str_view (a), lit ("1")) == ML_OK);
	CHECK (ml_fs_write_file_atomic (ml_str_view (b), lit ("22")) == ML_OK);
	CHECK (ml_fs_write_file_atomic (ml_str_view (c), lit ("3")) == ML_OK);

	/* only the files with the suffix, one per line, in any order */
	CHECK (ml_fs_list (&names, dir, lit (".theme")) == ML_OK);
	CHECK (names.len == 16);
	CHECK (strstr (ml_str_cstr (names), "a.theme\n") != NULL);
	CHECK (strstr (ml_str_cstr (names), "b.theme\n") != NULL);
	CHECK (strstr (ml_str_cstr (names), "c.txt") == NULL);

	/* a folder that is not there lists nothing */
	ml_str_clear (&names);
	CHECK (ml_fs_list (&names, lit ("no_such_dir.tmp"), lit ("")) == ML_OK);
	CHECK (names.len == 0);

	/* the stamp changes with the content, and is 0 without a file */
	stamp = ml_fs_stamp (ml_str_view (a));
	CHECK (stamp != 0);
	CHECK (ml_fs_stamp (ml_str_view (a)) == stamp);
	CHECK (
		ml_fs_write_file_atomic (ml_str_view (a), lit ("longer text")) == ML_OK
	);
	CHECK (ml_fs_stamp (ml_str_view (a)) != stamp);
	CHECK (ml_fs_stamp (lit ("no_such_file.tmp")) == 0);

	ml_fs_remove (ml_str_view (a));
	ml_fs_remove (ml_str_view (b));
	ml_fs_remove (ml_str_view (c));
	ml_str_free (&c);
	ml_str_free (&b);
	ml_str_free (&a);
	ml_str_free (&names);
}

static void test_console (void)
{
	MlConsole c;
	MlResult  r = ml_console_open (&c);

	/*
	 * Under a test runner stdin/stdout are usually not a console; both outcomes
	 * are valid.
	 */
	CHECK (r == ML_OK || r == ML_ERR_IO);

	if (r == ML_OK)
	{
		MlConsoleSize size = ml_console_size (&c);

		CHECK (c.active);
		CHECK (size.cols > 0 && size.rows > 0);
		ml_console_close (&c);
		CHECK (!c.active);
		ml_console_close (&c);
		CHECK (!c.active);
	}

	else
	{
		CHECK (!c.active);
		ml_console_close (&c);
	}
}

void run_platform_tests (void)
{
#ifdef _WIN32
	test_key_decode ();
#endif
	test_key_format ();
	test_path ();
	test_fs ();
	test_fs_list ();
	test_console ();
}
