#include <matlog/cli/cli.h>
#include <matlog/platform/console.h>
#include <matlog/session/mlog.h>
#include <matlog/ui/editor.h>
#include <matlog/ui/screen.h>

#include <stdio.h>

#define DEFAULT_SESSION "scratch.mlog"

#define ALT_SCREEN_ON  "\x1b[?1049h"
#define ALT_SCREEN_OFF "\x1b[?1049l"

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
		r = ml_editor_begin (ed);
	}

	return r;
}

static int32_t is_ctrl_c (MlKey key)
{
	return (
		   key.code == ML_KEY_CHAR
		&& key.cp == 'c'
		&& (key.mods & ML_MOD_CTRL)
	);
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

/* Reads keys and edits until the editor asks to quit. */
static MlResult run (
	MlConsole *con,
	MlScreen  *screen,
	MlEditor  *ed
)
{
	MlResult r = ML_OK;
	MlStr    frame;

	ml_str_init (&frame);

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

		r = draw (con, screen, &frame, ed);

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

		else if (r == ML_OK && is_ctrl_c (k.key))
		{
			ed->quit = 1;
		}

		else if (r == ML_OK)
		{
			r = ml_editor_handle_key (ed, k.key);
		}

		/* every committed change is written to the file right away */
		if (r == ML_OK && ed->session.dirty && ed->session.path.len > 0)
		{
			r = ml_mlog_save (&ed->session);
		}
	}

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
static int32_t run_command_line (int argc, char **argv)
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

	for (int i = 1; i < argc && r == ML_OK; i++)
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

/* The interactive editor. */
static int run_interactive (int argc, char **argv)
{
	MlResult      r = ML_OK;
	MlConsole     con;
	MlConsoleSize size;
	MlScreen      screen;
	MlEditor      ed;

	ml_editor_init (&ed);
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
			ml_slice_cstr (ml_choose (argc > 1, argv [1], DEFAULT_SESSION))
		);
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
