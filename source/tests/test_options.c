#include "test.h"
#include "test_keys.h"

#include <meth/cli/cli.h>
#include <meth/platform/fs.h>
#include <meth/session/options.h>

#include <stdlib.h>
#include <string.h>

#define CONFIG_DIR  "meth_test_config/options"
#define CONFIG_FILE "meth_test_config/options/settings.mlcfg"

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static void use_test_config (void)
{
#ifdef _WIN32
	_putenv_s ("METH_CONFIG", CONFIG_DIR);
#else
	setenv ("METH_CONFIG", CONFIG_DIR, 1);
#endif
	ml_fs_remove (lit (CONFIG_FILE));
}

/* The status line message of the editor, "" when none. */
static const char *message_of (const MlEditor *ed)
{
	return ml_str_cstr (ed->message);
}

static void command (MlEditor *ed, const char *text)
{
	test_normal (ed, text);
	test_key (ed, ML_KEY_ENTER, 0, 0);
}

static void test_table (void)
{
	MlStr out;

	ml_str_init (&out);
	CHECK (ml_option_count () >= 4);
	CHECK (ml_option_find (lit ("ghost")) < ml_option_count ());
	CHECK (ml_option_find (lit ("nonsense")) == ml_option_count ());

	for (uint64_t i = 0; i < ml_option_count (); i++)
	{
		MlOption o = ml_option_at (i);

		CHECK (ml_option_find (lit (o.name)) == i);
		CHECK (strlen (o.summary) > 0);
	}

	/* bools take several spellings, and are written on or off */
	{
		uint64_t ghost = ml_option_find (lit ("ghost"));

		CHECK (ml_option_check (ghost, lit ("yes"), &out) == ML_OK);
		CHECK (ml_slice_equal (ml_str_view (out), lit ("on")));
		ml_str_clear (&out);
		CHECK (ml_option_check (ghost, lit ("0"), &out) == ML_OK);
		CHECK (ml_slice_equal (ml_str_view (out), lit ("off")));
		ml_str_clear (&out);
		CHECK (ml_option_check (ghost, lit ("maybe"), &out) == ML_ERR_PARSE);
		CHECK (out.len == 0);
	}

	/* a choice takes its words only, text takes any word */
	{
		uint64_t colors = ml_option_find (lit ("colors"));
		uint64_t theme  = ml_option_find (lit ("theme"));

		CHECK (ml_option_check (colors, lit ("256"), &out) == ML_OK);
		ml_str_clear (&out);
		CHECK (ml_option_check (colors, lit ("8"), &out) == ML_ERR_PARSE);
		CHECK (ml_option_check (theme, lit ("mine"), &out) == ML_OK);
		CHECK (ml_option_check (theme, lit (""), &out) == ML_ERR_PARSE);
	}

	ml_str_free (&out);
}

static void test_set_command (void)
{
	MlEditor ed;
	MlStr    text;

	use_test_config ();
	ml_editor_init (&ed);
	ml_str_init (&text);
	CHECK (ed.ghost);

	command (&ed, ":set");
	CHECK (strstr (message_of (&ed), "ghost=on") != NULL);
	CHECK (strstr (message_of (&ed), "highlight=off") != NULL);

	command (&ed, ":set ghost off");
	CHECK (!ed.ghost);
	CHECK (strcmp (message_of (&ed), "") == 0);
	CHECK (ml_fs_read_file (&text, lit (CONFIG_FILE)) == ML_OK);
	CHECK (strstr (ml_str_cstr (text), "[display]\nghost = off\n") != NULL);

	command (&ed, ":set ghost");
	CHECK (strcmp (message_of (&ed), "ghost=off") == 0);

	command (&ed, ":set ghost maybe");
	CHECK (!ed.ghost);
	CHECK (strstr (message_of (&ed), "ghost: not a value") != NULL);

	command (&ed, ":set nothing 1");
	CHECK (strstr (message_of (&ed), "no such setting: nothing") != NULL);

	command (&ed, ":set highlight on");
	CHECK (ed.highlight);
	command (&ed, ":set colors 256");
	CHECK (ed.colors == ML_COLORS_256);
	command (&ed, ":set colors 5");
	CHECK (ed.colors == ML_COLORS_256);
	command (&ed, ":set theme light");
	CHECK (strcmp (ed.theme.name, "light") == 0);

	/* what is in force is what is kept: a missing theme is not written */
	command (&ed, ":set theme no-such-theme");
	CHECK (strcmp (ed.theme.name, "light") == 0);
	ml_str_clear (&text);
	CHECK (ml_fs_read_file (&text, lit (CONFIG_FILE)) == ML_OK);
	CHECK (strstr (ml_str_cstr (text), "theme = light\n") != NULL);
	CHECK (strstr (ml_str_cstr (text), "no-such-theme") == NULL);

	/* a new editor takes the file */
	{
		MlEditor again;

		ml_editor_init (&again);
		CHECK (ml_editor_load_settings (&again) == ML_OK);
		CHECK (!again.ghost);
		CHECK (again.highlight);
		CHECK (again.colors == ML_COLORS_256);
		CHECK (strcmp (again.theme.name, "light") == 0);

		/* :config reload does the same after the file changed */
		CHECK (
			ml_fs_write_file_atomic (
				lit (CONFIG_FILE),
				lit ("[display]\nghost = on\ncolors = nonsense\n")
			) == ML_OK
		);
		command (&again, ":config reload");
		CHECK (again.ghost);
		CHECK (again.colors == ML_COLORS_256);
		CHECK (strcmp (message_of (&again), "settings read") == 0);
		ml_editor_free (&again);
	}

	command (&ed, ":config path");
	CHECK (strstr (message_of (&ed), "settings.mlcfg") != NULL);

	command (&ed, ":config");
	CHECK (ed.state == ML_STATE_HELP);

	ml_str_free (&text);
	ml_editor_free (&ed);
	ml_fs_remove (lit (CONFIG_FILE));
}

typedef struct Run
{
	int32_t code;
	MlStr   out;
	MlStr   err;
} Run;

static Run run (const char *a0, const char *a1, const char *a2, const char *a3)
{
	Run        r;
	MlSliceVec args;
	const char *list [4] = { a0, a1, a2, a3 };

	MlSliceVec_init (&args);
	ml_str_init (&r.out);
	ml_str_init (&r.err);

	for (uint64_t i = 0; i < 4 && list [i] != NULL; i++)
	{
		CHECK (MlSliceVec_push (&args, lit (list [i])) == ML_OK);
	}

	r.code = ml_cli_run (&args, lit (""), &r.out, &r.err);
	MlSliceVec_free (&args);
	return r;
}

static void run_free (Run *r)
{
	ml_str_free (&r->out);
	ml_str_free (&r->err);
}

static void test_cli (void)
{
	Run r;

	use_test_config ();

	r = run ("config", "list", NULL, NULL);
	CHECK (r.code == ML_EXIT_OK);
	CHECK (strstr (ml_str_cstr (r.out), "theme = auto\n") != NULL);
	CHECK (strstr (ml_str_cstr (r.out), "ghost = on\n") != NULL);
	run_free (&r);

	r = run ("config", "set", "ghost", "no");
	CHECK (r.code == ML_EXIT_OK);
	CHECK (r.out.len == 0);
	run_free (&r);

	r = run ("config", "get", "ghost", NULL);
	CHECK (r.code == ML_EXIT_OK);
	CHECK (strcmp (ml_str_cstr (r.out), "ghost = off\n") == 0);
	run_free (&r);

	r = run ("config", "get", "--format=json", "ghost");
	CHECK (
		strcmp (
			ml_str_cstr (r.out),
			"{\"name\":\"ghost\",\"section\":\"display\",\"value\":\"off\","
			"\"default\":\"on\",\"source\":\"file\"}\n"
		) == 0
	);
	run_free (&r);

	r = run ("config", "path", NULL, NULL);
	CHECK (strstr (ml_str_cstr (r.out), "settings.mlcfg\n") != NULL);
	run_free (&r);

	/* usage errors */
	r = run ("config", "set", "ghost", "maybe");
	CHECK (r.code == ML_EXIT_USAGE);
	run_free (&r);
	r = run ("config", "get", "nothing", NULL);
	CHECK (r.code == ML_EXIT_USAGE);
	run_free (&r);
	r = run ("config", NULL, NULL, NULL);
	CHECK (r.code == ML_EXIT_USAGE);
	run_free (&r);

	/* the help of the command */
	r = run ("help", "config", NULL, NULL);
	CHECK (r.code == ML_EXIT_OK);
	CHECK (
		strstr (ml_str_cstr (r.out), "meth config set NAME VALUE") != NULL
	);
	run_free (&r);

	ml_fs_remove (lit (CONFIG_FILE));
}

void run_options_tests (void);

void run_options_tests (void)
{
	test_table ();
	test_set_command ();
	test_cli ();
}
