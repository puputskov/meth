#include "test.h"

#include <matlog/session/settings.h>

static MlSlice lit (const char *text)
{
	return ml_slice_cstr (text);
}

/* The text with the entry set, as a string compared to expect. */
static int32_t set_gives (
	const char *old,
	const char *section,
	const char *key,
	const char *value,
	const char *expect
)
{
	MlStr   out;
	int32_t same;

	ml_str_init (&out);
	same = (
		   ml_settings_text_set (
			   &out,
			   lit (old),
			   lit (section),
			   lit (key),
			   lit (value)
		   ) == ML_OK
		&& ml_slice_equal (ml_str_view (out), lit (expect))
	);

	if (!same)
	{
		printf (
			"  got \"%.*s\"\n",
			(int)out.len,
			out.data
		);
	}

	ml_str_free (&out);
	return same;
}

static int32_t get_is (
	const char *text,
	const char *section,
	const char *key,
	const char *expect
)
{
	MlSetting s = ml_settings_text_get (lit (text), lit (section), lit (key));

	return ml_choose (
		expect == NULL,
		!s.found,
		s.found && ml_slice_equal (s.value, lit (expect))
	);
}

static void test_get (void)
{
	const char *text =
		"# comment\n"
		"[folds]\n"
		"a.mlog = 1 2\n"
		"\n"
		"[display]\n"
		"theme = light\n"
		"colors=256\n";

	CHECK (get_is (text, "display", "theme", "light"));
	CHECK (get_is (text, "display", "colors", "256"));
	CHECK (get_is (text, "folds", "a.mlog", "1 2"));

	/* the section counts, and a missing key is not found */
	CHECK (get_is (text, "folds", "theme", NULL));
	CHECK (get_is (text, "display", "nothing", NULL));
	CHECK (get_is (text, "other", "theme", NULL));
	CHECK (get_is ("", "display", "theme", NULL));
	CHECK (get_is ("theme = x\n", "display", "theme", NULL));
}

static void test_set (void)
{
	/* an empty file gets the section */
	CHECK (set_gives (
		"",
		"display",
		"theme",
		"dark",
		"[display]\ntheme = dark\n"
	));

	/* a section is added after the others, with a blank line between */
	CHECK (set_gives (
		"[folds]\na = 1\n",
		"display",
		"theme",
		"dark",
		"[folds]\na = 1\n\n[display]\ntheme = dark\n"
	));

	/* an entry is replaced where it is */
	CHECK (set_gives (
		"[display]\ntheme = light\ncolors = 16\n",
		"display",
		"theme",
		"dark",
		"[display]\ntheme = dark\ncolors = 16\n"
	));

	/* a new entry goes to the end of its section, before the next one */
	CHECK (set_gives (
		"[display]\ntheme = light\n[folds]\na = 1\n",
		"display",
		"colors",
		"16",
		"[display]\ntheme = light\ncolors = 16\n[folds]\na = 1\n"
	));
	CHECK (set_gives (
		"[display]\ntheme = light\n",
		"display",
		"colors",
		"16",
		"[display]\ntheme = light\ncolors = 16\n"
	));

	/* comments and unknown entries stay */
	CHECK (set_gives (
		"# mine\n[display]\nfuture = 1\ntheme = x\n",
		"display",
		"theme",
		"y",
		"# mine\n[display]\nfuture = 1\ntheme = y\n"
	));

	/* the same entry in another section is not touched */
	CHECK (set_gives (
		"[folds]\ntheme = a\n[display]\ntheme = b\n",
		"display",
		"theme",
		"c",
		"[folds]\ntheme = a\n[display]\ntheme = c\n"
	));
}

void run_settings_tests (void)
{
	test_get ();
	test_set ();
}
