#include <matlog/platform/console.h>
#include <matlog/platform/win32/key.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MODE_IN_RAW \
	(ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT)
#define MODE_OUT_ADD \
	(ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)

static HANDLE to_handle (uint64_t h)
{
	return (HANDLE)(uintptr_t)h;
}

static uint64_t from_handle (HANDLE h)
{
	return (uint64_t)(uintptr_t)h;
}

int32_t ml_console_truecolor (void)
{
	return 1;
}

MlResult ml_console_open (MlConsole *c)
{
	MlResult r        = ML_OK;
	HANDLE   in       = GetStdHandle (STD_INPUT_HANDLE);
	HANDLE   out      = GetStdHandle (STD_OUTPUT_HANDLE);
	DWORD    in_mode  = 0;
	DWORD    out_mode = 0;

	c->active        = 0;
	c->pending_high  = 0;
	c->mouse_buttons = 0;

	if (
		   in  == INVALID_HANDLE_VALUE
		|| out == INVALID_HANDLE_VALUE
		|| in  == NULL
		|| out == NULL
	)
	{
		r = ML_ERR_IO;
	}

	else if (!GetConsoleMode (in, &in_mode) || !GetConsoleMode (out, &out_mode))
	{
		r = ML_ERR_IO;
	}

	if (r == ML_OK)
	{
		c->in_handle      = from_handle (in);
		c->out_handle     = from_handle (out);
		c->saved_in_mode  = (uint32_t)in_mode;
		c->saved_out_mode = (uint32_t)out_mode;
		c->saved_out_cp   = (uint32_t)GetConsoleOutputCP ();

		if (!SetConsoleMode (in, MODE_IN_RAW))
		{
			r = ML_ERR_IO;
		}
	}

	if (r == ML_OK && !SetConsoleMode (out, out_mode | MODE_OUT_ADD))
	{
		SetConsoleMode (in, in_mode);
		r = ML_ERR_IO;
	}

	if (r == ML_OK)
	{
		SetConsoleOutputCP (CP_UTF8);
		c->active = 1;
	}

	return r;
}

void ml_console_close (MlConsole *c)
{
	if (c->active)
	{
		SetConsoleMode (to_handle (c->in_handle), c->saved_in_mode);
		SetConsoleMode (to_handle (c->out_handle), c->saved_out_mode);
		SetConsoleOutputCP (c->saved_out_cp);
		c->active = 0;
	}
}

/*
 * The key of a mouse event: left button press, drag with it down, release,
 * and the wheel. *buttons keeps the buttons of the event before, to tell a
 * press from a release.
 */
static MlKey mouse_key (HANDLE out, MOUSE_EVENT_RECORD m, DWORD *buttons)
{
	MlKey key = {
		.code = ML_KEY_NONE,
		.cp   = 0,
		.mods = 0,
		.col  = 0,
		.row  = 0
	};

	CONSOLE_SCREEN_BUFFER_INFO info;
	int32_t down = (m.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
	int32_t was  = (*buttons & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
	int32_t left = 0;
	int32_t top  = 0;

	if (GetConsoleScreenBufferInfo (out, &info))
	{
		left = info.srWindow.Left;
		top  = info.srWindow.Top;
	}

	key.col  = (uint32_t)(m.dwMousePosition.X - left + 1);
	key.row  = (uint32_t)(m.dwMousePosition.Y - top + 1);
	key.mods |= ml_choose (
		(m.dwControlKeyState & SHIFT_PRESSED) != 0,
		ML_MOD_SHIFT,
		0
	);
	key.mods |= ml_choose (
		(m.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0,
		ML_MOD_CTRL,
		0
	);
	key.mods |= ml_choose (
		(m.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0,
		ML_MOD_ALT,
		0
	);

	if (m.dwEventFlags & MOUSE_WHEELED)
	{
		key.code = ML_KEY_MOUSE;
		key.cp   = ml_choose (
			(int16_t)HIWORD (m.dwButtonState) > 0,
			ML_MOUSE_WHEEL_UP,
			ML_MOUSE_WHEEL_DOWN
		);
	}

	else if (m.dwEventFlags & MOUSE_MOVED)
	{
		key.code = ml_choose (down, ML_KEY_MOUSE, ML_KEY_NONE);
		key.cp   = ML_MOUSE_DRAG;
	}

	else if (m.dwEventFlags == 0 && down != was)
	{
		key.code = ML_KEY_MOUSE;
		key.cp   = ml_choose (down, ML_MOUSE_PRESS, ML_MOUSE_RELEASE);
	}

	*buttons = m.dwButtonState;
	return key;
}

MlKeyResult ml_console_read_key (MlConsole *c)
{
	MlKeyResult res = {
		.result = ML_OK,
		.key = {
			.code = ML_KEY_NONE,
			.cp   = 0,
			.mods = 0
		}
	};

	while (res.result == ML_OK && res.key.code == ML_KEY_NONE)
	{
		INPUT_RECORD rec;
		DWORD        count = 0;

		if (
			   !ReadConsoleInputW (to_handle (c->in_handle), &rec, 1, &count)
			|| count == 0
		)
		{
			res.result = ML_ERR_IO;
		}

		else if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT)
		{
			res.key.code = ML_KEY_RESIZE;
		}

		else if (rec.EventType == MOUSE_EVENT)
		{
			DWORD buttons = c->mouse_buttons;

			res.key = mouse_key (
				to_handle (c->out_handle),
				rec.Event.MouseEvent,
				&buttons
			);
			c->mouse_buttons = buttons;
		}

		else if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
		{
			MlRawKey raw = {
				.vk      = rec.Event.KeyEvent.wVirtualKeyCode,
				.unicode = rec.Event.KeyEvent.uChar.UnicodeChar,
				.control = rec.Event.KeyEvent.dwControlKeyState
			};

			MlDecodeResult d = ml_key_decode_win32 (raw, c->pending_high);

			c->pending_high = d.pending_high;
			res.key         = d.key;
		}
	}

	return res;
}

/* Does the record make ml_console_read_key return something? */
static int32_t is_input (INPUT_RECORD rec)
{
	return (
		   rec.EventType == WINDOW_BUFFER_SIZE_EVENT
		|| (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
		|| (
			   rec.EventType == MOUSE_EVENT
			&& (
				   rec.Event.MouseEvent.dwEventFlags == 0
				|| (rec.Event.MouseEvent.dwEventFlags & MOUSE_WHEELED)
				|| (rec.Event.MouseEvent.dwButtonState != 0)
			)
		)
	);
}

int32_t ml_console_wait (MlConsole *c, uint32_t timeout_ms)
{
	HANDLE   in    = to_handle (c->in_handle);
	ULONGLONG end  = GetTickCount64 () + timeout_ms;
	int32_t  ready = 0;
	int32_t  more  = 1;

	/* events that read_key would skip are dropped, they are not input */
	while (more && !ready)
	{
		ULONGLONG now  = GetTickCount64 ();
		DWORD     left = (DWORD)ml_choose (now < end, end - now, 0);

		if (WaitForSingleObject (in, left) == WAIT_OBJECT_0)
		{
			INPUT_RECORD rec;
			DWORD        count = 0;

			if (PeekConsoleInputW (in, &rec, 1, &count) && count == 1)
			{
				ready = is_input (rec);

				if (!ready)
				{
					ReadConsoleInputW (in, &rec, 1, &count);
				}
			}
		}

		else
		{
			more = 0;
		}
	}

	return ready;
}

MlConsoleSize ml_console_size (const MlConsole *c)
{
	MlConsoleSize size = {
		.cols = 0,
		.rows = 0
	};

	CONSOLE_SCREEN_BUFFER_INFO info;

	if (GetConsoleScreenBufferInfo (to_handle (c->out_handle), &info))
	{
		size.cols = (uint32_t)(info.srWindow.Right - info.srWindow.Left + 1);
		size.rows = (uint32_t)(info.srWindow.Bottom - info.srWindow.Top + 1);
	}

	return size;
}

MlResult ml_console_write (const MlConsole *c, MlSlice text)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	while (r == ML_OK && pos < text.len)
	{
		DWORD written = 0;
		DWORD chunk   = (DWORD)ml_choose (
			text.len - pos > 0x10000,
			0x10000,
			text.len - pos
		);

		if (
			   WriteFile (
					to_handle (c->out_handle),
					text.data + pos,
					chunk,
					&written,
					NULL
				)
			&& written > 0
		)
		{
			pos += written;
		}

		else
		{
			r = ML_ERR_IO;
		}
	}

	return r;
}
