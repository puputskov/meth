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

int main (int argc, char **argv)
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
