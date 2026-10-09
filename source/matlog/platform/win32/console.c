#include <matlog/platform/console.h>
#include <matlog/platform/win32/key.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MODE_IN_RAW  (ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS)
#define MODE_OUT_ADD (ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)

static HANDLE to_handle (uint64_t h)
{
	return (HANDLE)(uintptr_t)h;
}

static uint64_t from_handle (HANDLE h)
{
	return (uint64_t)(uintptr_t)h;
}

MlResult ml_console_open (MlConsole *c)
{
	MlResult r        = ML_OK;
	HANDLE   in       = GetStdHandle (STD_INPUT_HANDLE);
	HANDLE   out      = GetStdHandle (STD_OUTPUT_HANDLE);
	DWORD    in_mode  = 0;
	DWORD    out_mode = 0;

	c->active       = 0;
	c->pending_high = 0;

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

MlKeyResult ml_console_read_key (MlConsole *c)
{
	MlKeyResult res =
	{
		.result = ML_OK,
		.key =
		{
			.code = ML_KEY_NONE,
			.cp   = 0,
			.mods = 0
		}
	};

	while (res.result == ML_OK && res.key.code == ML_KEY_NONE)
	{
		INPUT_RECORD rec;
		DWORD        count = 0;

		if (!ReadConsoleInputW (to_handle (c->in_handle), &rec, 1, &count) || count == 0)
		{
			res.result = ML_ERR_IO;
		}

		else if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT)
		{
			res.key.code = ML_KEY_RESIZE;
		}

		else if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
		{
			MlRawKey raw =
			{
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

MlConsoleSize ml_console_size (const MlConsole *c)
{
	MlConsoleSize size =
	{
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

		if (WriteFile (to_handle (c->out_handle), text.data + pos, chunk, &written, NULL) && written > 0)
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
