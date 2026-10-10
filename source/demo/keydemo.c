#include <meth/core/str.h>
#include <meth/platform/console.h>

/*
 * Prints every key event as it is decoded. Esc and the arrow keys arrive as
 * different codes. Press Ctrl+C (or q) to quit.
 */

static MlResult print_line (const MlConsole *c, MlStr *line)
{
	MlResult r = ml_str_append (line, ml_slice_cstr ("\r\n"));

	if (r == ML_OK)
	{
		r = ml_console_write (c, ml_str_view (*line));
	}

	ml_str_clear (line);
	return r;
}

int main (void)
{
	MlConsole c;
	MlStr     line;
	MlResult  r    = ML_OK;
	int32_t   quit = 0;

	ml_str_init (&line);
	r = ml_console_open (&c);

	if (r == ML_OK)
	{
		r = ml_str_append (
			&line,
			ml_slice_cstr ("Key demo: press keys, Ctrl+C or q quits.")
		);
	}

	if (r == ML_OK)
	{
		r = print_line (&c, &line);
	}

	while (r == ML_OK && !quit)
	{
		MlKeyResult k = ml_console_read_key (&c);

		r = k.result;

		if (r == ML_OK)
		{
			MlConsoleSize size = ml_console_size (&c);

			r = ml_key_format (&line, k.key);

			if (r == ML_OK && k.key.code == ML_KEY_RESIZE)
			{
				r = ml_str_appendf (&line, " %ux%u", size.cols, size.rows);
			}

			if (r == ML_OK)
			{
				r = print_line (&c, &line);
			}

			quit = (
				   k.key.code == ML_KEY_CHAR
				&& (
					   k.key.cp == 'q'
					|| (k.key.cp == 'c' && (k.key.mods & ML_MOD_CTRL))
				)
			);
		}
	}

	ml_console_close (&c);
	ml_str_free (&line);
	return ml_choose (
		r == ML_OK,
		0,
		1
	);
}
