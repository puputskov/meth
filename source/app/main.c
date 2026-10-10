#include <matlog/cli/cli.h>
#include <matlog/platform/config.h>
#include <matlog/platform/console.h>
#include <matlog/platform/fs.h>
#include <matlog/platform/path.h>
#include <matlog/session/folds.h>
#include <matlog/session/mlog.h>
#include <matlog/session/settings.h>
#include <matlog/ui/editor.h>
#include <matlog/ui/hint.h>
#include <matlog/ui/screen.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SESSION "scratch.mlog"

/*
 * The alternate screen, and the "disambiguate" level of the kitty keyboard
 * protocol, which lets a terminal tell Ctrl+Shift+Z from Ctrl+Z. Terminals
 * that do not know it ignore the request.
 */
#define ALT_SCREEN_ON  "\x1b[?1049h\x1b[>1u"
#define ALT_SCREEN_OFF "\x1b[0m\x1b[<u\x1b[?1049l"

/* Opens the session file, or starts a new session that will be saved there. */
static MlResult open_session (MlEditor *ed, MlSlice path)
{
	MlResult r = ml_mlog_load (&ed->session, path);

	if (r == ML_ERR_IO)
	{
		r = ml_session_set_path (&ed->session, path);
	}

	if (r == ML_OK)
	{
		ml_session_set_domain (&ed->session, ed->session.domain);
		r = ml_editor_begin_note (ed);
	}

	return r;
}

/* Draws the editor if anything changed. */
static MlResult draw (
	MlConsole      *con,
	MlScreen       *screen,
	MlStr          *frame,
	const MlEditor *ed
)
{
	MlResult r = ml_screen_render (screen, frame, ed);

	if (r == ML_OK)
	{
		r = ml_console_write (con, ml_str_view (*frame));
	}

	ml_str_clear (frame);
	return r;
}

/*
 * Keeps the folds of the session in the settings file: a session that was just
 * opened (or switched to with :open) gets its folds back, any later change of
 * the folds is written. path and folds are what was seen the last time.
 */
static MlResult sync_folds (MlEditor *ed, MlStr *path, MlStr *folds)
{
	MlResult r       = ML_OK;
	MlStr    current;
	int32_t  changed = !ml_slice_equal (
		ml_str_view (*path),
		ml_str_view (ed->session.path)
	);

	ml_str_init (&current);

	if (changed)
	{
		ml_str_clear (path);
		r = ml_str_append (path, ml_str_view (ed->session.path));

		if (r == ML_OK)
		{
			r = ml_folds_load (&ed->session);
		}
	}

	if (r == ML_OK)
	{
		r = ml_folds_format (&current, &ed->session);
	}

	if (
		   r == ML_OK
		&& !changed
		&& !ml_slice_equal (ml_str_view (current), ml_str_view (*folds))
	)
	{
		/* a settings file that cannot be written is not worth an exit */
		ml_folds_save (&ed->session);
	}

	if (r == ML_OK)
	{
		ml_str_clear (folds);
		r = ml_str_append (folds, ml_str_view (current));
	}

	ml_str_free (&current);
	return r;
}

/* The section and keys of the look in the settings file. */
#define SETTINGS_DISPLAY "display"
#define SETTINGS_THEME   "theme"
#define SETTINGS_COLORS  "colors"

/*
 * Keeps the look in the settings file: the theme is written when it is not
 * the one seen the last time, the colour level likewise, each on its own so
 * that a level that was only detected is not written down. look holds
 * "theme colors" as seen last (empty before the first call). A file that
 * cannot be written is not worth an exit.
 */
static MlResult sync_look (const MlEditor *ed, MlStr *look)
{
	MlResult r = ML_OK;
	MlStr    now;
	MlSlice  was   = ml_str_view (*look);
	uint64_t split = 0;
	MlSlice  theme = ml_slice_cstr (ed->theme.name);
	MlSlice  level = ml_slice_cstr (ml_color_level_name (ed->colors));

	ml_str_init (&now);

	while (split < was.len && was.data [split] != ' ')
	{
		split++;
	}

	if (look->len > 0)
	{
		if (!ml_slice_equal (ml_slice_sub (was, 0, split), theme))
		{
			ml_settings_set (
				ml_slice_cstr (SETTINGS_DISPLAY),
				ml_slice_cstr (SETTINGS_THEME),
				theme
			);
		}

		if (
			!ml_slice_equal (
				ml_slice_sub (was, split + 1, was.len - split - 1),
				level
			)
		)
		{
			ml_settings_set (
				ml_slice_cstr (SETTINGS_DISPLAY),
				ml_slice_cstr (SETTINGS_COLORS),
				level
			);
		}
	}

	r = ml_str_appendf (
		&now,
		"%s %s",
		ed->theme.name,
		ml_color_level_name (ed->colors)
	);

	if (r == ML_OK)
	{
		ml_str_clear (look);
		r = ml_str_append (look, ml_str_view (now));
	}

	ml_str_free (&now);
	return r;
}

/*
 * The theme file being used: the stamp of name.theme in the settings folder
 * when the theme is not a built in one (0 otherwise, and 0 for a file that is
 * gone), kept to notice that the file was edited.
 */
typedef struct ThemeWatch
{
	char     name [ML_THEME_NAME_MAX];
	uint64_t stamp;
} ThemeWatch;

static uint64_t theme_file_stamp (const MlEditor *ed)
{
	uint64_t stamp = 0;
	MlTheme  built;
	MlStr    dir;
	MlStr    file;
	MlStr    path;

	ml_str_init (&dir);
	ml_str_init (&file);
	ml_str_init (&path);

	if (
		   ml_theme_builtin (&built, ml_slice_cstr (ed->theme.name)) != ML_OK
		&& ml_config_dir (&dir) == ML_OK
		&& ml_str_appendf (&file, "%s.theme", ed->theme.name) == ML_OK
		&& ml_path_join (&path, ml_str_view (dir), ml_str_view (file)) == ML_OK
	)
	{
		stamp = ml_fs_stamp (ml_str_view (path));
	}

	ml_str_free (&path);
	ml_str_free (&file);
	ml_str_free (&dir);
	return stamp;
}

/* 1 while the theme in use is a file that can change under us. */
static int32_t theme_is_file (const ThemeWatch *w)
{
	return w->stamp != 0;
}

/*
 * Reads the theme file again when it was edited since the last look. A theme
 * that was just chosen is only remembered, it was read a moment ago.
 */
static MlResult watch_theme (MlEditor *ed, ThemeWatch *w)
{
	MlResult r     = ML_OK;
	uint64_t stamp = theme_file_stamp (ed);

	if (strcmp (w->name, ed->theme.name) != 0)
	{
		strncpy (w->name, ed->theme.name, ML_THEME_NAME_MAX - 1);
		w->name [ML_THEME_NAME_MAX - 1] = 0;
		w->stamp = stamp;
	}

	else if (stamp != 0 && stamp != w->stamp)
	{
		w->stamp = stamp;
		r        = ml_editor_apply_theme (ed, ml_slice_cstr (ed->theme.name));
	}

	return r;
}

/* Reads keys and edits until the editor asks to quit. */
static MlResult run (
	MlConsole *con,
	MlScreen  *screen,
	MlEditor  *ed
)
{
	MlResult r = ML_OK;
	MlStr    frame;
	MlStr    path;
	MlStr    folds;
	MlStr    look;
	ThemeWatch watch = { .name = "", .stamp = 0 };
	uint32_t   idle  = 0;
	MlKey      asked;

	ml_str_init (&frame);
	ml_str_init (&path);
	ml_str_init (&folds);
	ml_str_init (&look);
	r = sync_folds (ed, &path, &folds);

	if (r == ML_OK)
	{
		r = sync_look (ed, &look);
	}

	if (r == ML_OK)
	{
		r = watch_theme (ed, &watch);
	}

	while (r == ML_OK && !ed->quit)
	{
		MlKeyResult k =
		{
			.result = ML_OK,
			.key    =
			{
				.code = ML_KEY_NONE,
				.cp   = 0,
				.mods = 0
			}
		};

		/* a page is the text area, less one line of overlap */
		ml_editor_set_page (
			ed,
			ml_choose (screen->rows > 2, screen->rows - 2, 1)
		);
		r = draw (con, screen, &frame, ed);

		/*
		 * Waiting for a key is cut into seconds while something changes
		 * without one: the hint of an empty editor, a theme file that is
		 * edited in another window.
		 */
		idle = 0;

		while (
			   r == ML_OK
			&& (ml_screen_hint_active (ed) || theme_is_file (&watch))
			&& !ml_console_wait (con, 1000)
		)
		{
			idle++;

			if (ml_screen_hint_active (ed) && idle % ML_HINT_SECONDS == 0)
			{
				screen->hint_tick++;
			}

			r = watch_theme (ed, &watch);

			if (r == ML_OK)
			{
				r = draw (con, screen, &frame, ed);
			}
		}

		if (r == ML_OK)
		{
			k = ml_console_read_key (con);
			r = k.result;
		}

		if (r == ML_OK && k.key.code == ML_KEY_RESIZE)
		{
			MlConsoleSize size = ml_console_size (con);

			ml_screen_resize (screen, size.cols, size.rows);
		}

		else if (r == ML_OK && k.key.code == ML_KEY_MOUSE)
		{
			r = ml_editor_mouse (
				ed,
				k.key,
				ml_screen_hit (screen, ed, k.key.col, k.key.row)
			);
		}

		else if (r == ML_OK)
		{
			r = ml_editor_handle_key (ed, k.key);
		}

		/* keys the editor asks for itself (:tutorial show types an answer) */
		while (r == ML_OK && ml_editor_take_pending (ed, &asked))
		{
			r = ml_editor_handle_key (ed, asked);
		}

		if (r == ML_OK)
		{
			r = sync_folds (ed, &path, &folds);
		}

		if (r == ML_OK)
		{
			r = sync_look (ed, &look);
		}

		if (r == ML_OK)
		{
			r = watch_theme (ed, &watch);
		}

		/* every committed change is written to the file right away */
		if (r == ML_OK && ed->session.dirty && ed->session.path.len > 0)
		{
			r = ml_mlog_save (&ed->session);
		}
	}

	ml_str_free (&look);
	ml_str_free (&folds);
	ml_str_free (&path);
	ml_str_free (&frame);
	return r;
}

/* All of standard input, appended to out. */
static MlResult read_stdin (MlStr *out)
{
	MlResult r    = ML_OK;
	char     buf [4096];
	uint64_t got;

	do
	{
		got = fread (buf, 1, sizeof buf, stdin);
		r   = ml_str_append (out, ml_slice (buf, got));
	}

	while (r == ML_OK && got > 0);

	return r;
}

/* Runs a command line command (cli.h): no screen, results on stdout. */
static int32_t run_command_line (int32_t argc, char **argv)
{
	int32_t    code = ML_EXIT_IO;
	MlSliceVec args;
	MlStr      input;
	MlStr      out;
	MlStr      err;
	MlResult   r    = ML_OK;

	MlSliceVec_init (&args);
	ml_str_init (&input);
	ml_str_init (&out);
	ml_str_init (&err);

	for (int32_t i = 1; i < argc && r == ML_OK; i++)
	{
		r = MlSliceVec_push (&args, ml_slice_cstr (argv [i]));
	}

	if (r == ML_OK && ml_cli_needs_stdin (&args))
	{
		r = read_stdin (&input);
	}

	if (r == ML_OK)
	{
		code = ml_cli_run (&args, ml_str_view (input), &out, &err);
		fwrite (out.data, 1, out.len, stdout);
		fwrite (err.data, 1, err.len, stderr);
	}

	ml_str_free (&err);
	ml_str_free (&out);
	ml_str_free (&input);
	MlSliceVec_free (&args);
	return code;
}

/* The text of an environment variable, empty when it is not set. */
static MlSlice env_text (const char *name)
{
	const char *value = getenv (name);

	return ml_slice_cstr (ml_choose (value != NULL, value, ""));
}


/*
 * Colours and theme: the terminal says what it can show (NO_COLOR picks the
 * mono theme), then what the user chose before, kept in the settings file,
 * replaces that.
 */
static void setup_look (MlEditor *ed)
{
	MlStr   value;
	int32_t found = 0;

	ed->colors = ml_colors_detect (
		env_text ("COLORTERM"),
		env_text ("TERM"),
		getenv ("NO_COLOR") != NULL,
		ml_console_truecolor ()
	);
	ed->highlight = 1;

	if (ed->colors == ML_COLORS_OFF)
	{
		ml_theme_builtin (&ed->theme, ml_slice_cstr ("mono"));
	}

	else
	{
		/* a light terminal gets the light theme */
		ml_theme_builtin (
			&ed->theme,
			ml_slice_cstr (ml_theme_for_background (env_text ("COLORFGBG")))
		);
	}

	ml_str_init (&value);

	if (
		   ml_settings_get (
			   &value,
			   ml_slice_cstr (SETTINGS_DISPLAY),
			   ml_slice_cstr (SETTINGS_COLORS),
			   &found
		   ) == ML_OK
		&& found
		&& ml_color_level_from_name (ml_str_view (value)) != ML_COLORS_COUNT
	)
	{
		ed->colors = ml_color_level_from_name (ml_str_view (value));
	}

	ml_str_clear (&value);

	if (
		   ml_settings_get (
			   &value,
			   ml_slice_cstr (SETTINGS_DISPLAY),
			   ml_slice_cstr (SETTINGS_THEME),
			   &found
		   ) == ML_OK
		&& found
	)
	{
		ml_editor_apply_theme (ed, ml_str_view (value));
		ml_str_clear (&ed->message);
	}

	ml_str_free (&value);
}


/* The interactive editor. */
static int32_t run_interactive (int32_t argc, char **argv)
{
	MlResult      r = ML_OK;
	MlConsole     con;
	MlConsoleSize size;
	MlScreen      screen;
	MlEditor      ed;

	ml_editor_init (&ed);
	setup_look (&ed);
	ml_editor_load_settings (&ed);
	ml_editor_load_keys (&ed);
	r = ml_console_open (&con);

	if (r == ML_OK)
	{
		size = ml_console_size (&con);
		ml_screen_init (&screen, size.cols, size.rows);
		r = ml_console_write (&con, ml_slice_cstr (ALT_SCREEN_ON));
	}

	if (r == ML_OK)
	{
		r = open_session (
			&ed,
			ml_slice_cstr (
				ml_choose (
					argc > 1 && strcmp (argv [1], "--tutorial") != 0,
					argv [1],
					DEFAULT_SESSION
				)
			)
		);
	}

	if (r == ML_OK && argc > 1 && strcmp (argv [1], "--tutorial") == 0)
	{
		r = ml_editor_start_tutorial (&ed, ml_slice ("", 0));
	}

	if (r == ML_OK)
	{
		r = run (&con, &screen, &ed);
	}

	/* single cleanup stage: give the terminal back before printing anything */
	if (con.active)
	{
		ml_console_write (&con, ml_slice_cstr (ALT_SCREEN_OFF));
		ml_screen_free (&screen);
	}

	ml_console_close (&con);
	ml_editor_free (&ed);

	if (r != ML_OK)
	{
		fprintf (
			stderr,
			"matlog: %s%s\n",
			ml_result_str (r),
			ml_choose (
				r == ML_ERR_IO && !con.active,
				" (run matlog in a console window)",
				""
			)
		);
	}

	return ml_choose (
		r == ML_OK,
		0,
		1
	);
}

int main (int argc, char **argv)
{
	return ml_choose (
		argc > 1 && ml_cli_is_command (ml_slice_cstr (argv [1])),
		run_command_line (argc, argv),
		run_interactive (argc, argv)
	);
}
