#include <matlog/platform/posix/key.h>

#include <matlog/core/utf8.h>

/* Longest escape sequence that is still taken for a key. */
#define MAX_SEQUENCE 32

#define ESC 0x1B

static MlVtDecode make (
	MlKeyCode code,
	uint32_t  cp,
	uint32_t  mods,
	uint64_t  used
)
{
	MlVtDecode d =
	{
		.key =
		{
			.code = code,
			.cp   = cp,
			.mods = mods
		},
		.used       = used,
		.incomplete = 0
	};

	return d;
}

static MlVtDecode need_more (void)
{
	MlVtDecode d = make (ML_KEY_NONE, 0, 0, 0);

	d.incomplete = 1;
	return d;
}

/* Number of bytes of the UTF-8 sequence that starts with lead, 0 if none. */
static uint64_t utf8_length (uint8_t lead)
{
	uint64_t n = 0;

	n = ml_choose (lead >= 0xC2 && lead <= 0xDF, 2, n);
	n = ml_choose (lead >= 0xE0 && lead <= 0xEF, 3, n);
	n = ml_choose (lead >= 0xF0 && lead <= 0xF4, 4, n);
	return n;
}

/* A key that is not an escape sequence. */
static MlVtDecode decode_plain (MlSlice bytes, int32_t final)
{
	uint8_t    b = (uint8_t)bytes.data [0];
	MlVtDecode d = make (ML_KEY_NONE, 0, 0, 1);

	if (b == '\r' || b == '\n')
	{
		d = make (ML_KEY_ENTER, 0, 0, 1);
	}

	else if (b == '\t')
	{
		d = make (ML_KEY_TAB, 0, 0, 1);
	}

	else if (b == 0x7F || b == 0x08)
	{
		d = make (ML_KEY_BACKSPACE, 0, 0, 1);
	}

	else if (b >= 1 && b <= 26)
	{
		d = make (ML_KEY_CHAR, (uint32_t)('a' + b - 1), ML_MOD_CTRL, 1);
	}

	else if (b >= 0x20 && b < 0x7F)
	{
		d = make (ML_KEY_CHAR, b, 0, 1);
	}

	else if (b >= 0x80)
	{
		uint64_t need = utf8_length (b);

		if (need > bytes.len && !final)
		{
			d = need_more ();
		}

		else
		{
			MlDecoded c = ml_utf8_decode (bytes);

			d = make (ML_KEY_CHAR, c.cp, 0, c.size);
		}
	}

	return d;
}

/* The modifier parameter of xterm: 1 + shift 1 + alt 2 + ctrl 4 + meta 8. */
static uint32_t modifiers_from (uint32_t param)
{
	uint32_t bits = ml_choose (param > 0, param - 1, 0);
	uint32_t mods = 0;

	mods |= ml_choose ((bits & 1) != 0, ML_MOD_SHIFT, 0);
	mods |= ml_choose ((bits & 2) != 0, ML_MOD_ALT, 0);
	mods |= ml_choose ((bits & 4) != 0, ML_MOD_CTRL, 0);
	mods |= ml_choose ((bits & 8) != 0, ML_MOD_ALT, 0);
	return mods;
}

/* The key of ESC [ n ~ */
static MlKeyCode tilde_key (uint32_t n)
{
	MlKeyCode code = ML_KEY_NONE;

	code = ml_choose (n == 1 || n == 7, ML_KEY_HOME, code);
	code = ml_choose (n == 2, ML_KEY_INSERT, code);
	code = ml_choose (n == 3, ML_KEY_DELETE, code);
	code = ml_choose (n == 4 || n == 8, ML_KEY_END, code);
	code = ml_choose (n == 5, ML_KEY_PAGE_UP, code);
	code = ml_choose (n == 6, ML_KEY_PAGE_DOWN, code);
	code = ml_choose (n >= 11 && n <= 15, ML_KEY_F1 + (n - 11), code);
	code = ml_choose (n >= 17 && n <= 21, ML_KEY_F6 + (n - 17), code);
	code = ml_choose (n == 23 || n == 24, ML_KEY_F11 + (n - 23), code);
	return code;
}

/* The key of ESC [ letter and ESC O letter. */
static MlKeyCode letter_key (char letter)
{
	MlKeyCode code = ML_KEY_NONE;

	code = ml_choose (letter == 'A', ML_KEY_UP, code);
	code = ml_choose (letter == 'B', ML_KEY_DOWN, code);
	code = ml_choose (letter == 'C', ML_KEY_RIGHT, code);
	code = ml_choose (letter == 'D', ML_KEY_LEFT, code);
	code = ml_choose (letter == 'H', ML_KEY_HOME, code);
	code = ml_choose (letter == 'F', ML_KEY_END, code);
	code = ml_choose (letter == 'P', ML_KEY_F1, code);
	code = ml_choose (letter == 'Q', ML_KEY_F2, code);
	code = ml_choose (letter == 'R', ML_KEY_F3, code);
	code = ml_choose (letter == 'S', ML_KEY_F4, code);
	return code;
}

/* The key of ESC [ code ; modifiers u. */
static MlKey csi_u_key (uint32_t code, uint32_t mods)
{
	MlKey key =
	{
		.code = ML_KEY_CHAR,
		.cp   = ml_choose (code >= 'A' && code <= 'Z', code + 32, code),
		.mods = mods
	};

	if (code == 27)
	{
		key.code = ML_KEY_ESCAPE;
		key.cp   = 0;
	}

	else if (code == 13)
	{
		key.code = ML_KEY_ENTER;
		key.cp   = 0;
	}

	else if (code == 9)
	{
		key.code = ML_KEY_TAB;
		key.cp   = 0;
	}

	else if (code == 127)
	{
		key.code = ML_KEY_BACKSPACE;
		key.cp   = 0;
	}

	return key;
}

/* ESC [ parameters final, where bytes starts with ESC [. */
static MlVtDecode decode_csi (MlSlice bytes, int32_t final)
{
	MlVtDecode d = need_more ();
	uint64_t   end   = 2;
	uint32_t   param [2];
	uint64_t   count = 0;

	param [0] = 0;
	param [1] = 0;

	/* parameter and intermediate bytes, then the final byte 0x40..0x7E */
	while (
		   end < bytes.len
		&& end < MAX_SEQUENCE
		&& (uint8_t)bytes.data [end] < 0x40
	)
	{
		end++;
	}

	if (end < bytes.len && (uint8_t)bytes.data [end] <= 0x7E)
	{
		char letter = bytes.data [end];

		for (uint64_t i = 2; i < end; i++)
		{
			char c = bytes.data [i];

			if (c == ';')
			{
				count++;
			}

			else if (c >= '0' && c <= '9' && count < 2)
			{
				param [count] = param [count] * 10 + (uint32_t)(c - '0');
			}
		}

		d = make (ML_KEY_NONE, 0, 0, end + 1);

		if (letter == '~')
		{
			d.key.code = tilde_key (param [0]);
			d.key.mods = modifiers_from (param [1]);
		}

		else if (letter == 'u')
		{
			/* the kitty protocol: ESC [ code ; modifiers u */
			d.key = csi_u_key (param [0], modifiers_from (param [1]));
		}

		else if (letter == 'Z')
		{
			d.key.code = ML_KEY_TAB;
			d.key.mods = ML_MOD_SHIFT;
		}

		else
		{
			d.key.code = letter_key (letter);
			d.key.mods = modifiers_from (param [1]);
		}
	}

	else if (end < bytes.len || final)
	{
		/* too long or cut off: not a key we know, skip what there is */
		d = make (
			ML_KEY_NONE,
			0,
			0,
			ml_choose (
				end < bytes.len,
				end + 1,
				end
			)
		);
	}

	return d;
}

MlVtDecode ml_key_decode_vt (MlSlice bytes, int32_t final)
{
	MlVtDecode d = need_more ();

	if (bytes.len > 0 && (uint8_t)bytes.data [0] != ESC)
	{
		d = decode_plain (bytes, final);
	}

	else if (bytes.len == 1 && final)
	{
		d = make (ML_KEY_ESCAPE, 0, 0, 1);
	}

	else if (bytes.len >= 2 && bytes.data [1] == '[')
	{
		d = decode_csi (bytes, final);
	}

	else if (bytes.len >= 2 && bytes.data [1] == 'O')
	{
		if (bytes.len >= 3)
		{
			d = make (letter_key (bytes.data [2]), 0, 0, 3);
		}

		else if (final)
		{
			d = make (ML_KEY_NONE, 0, 0, 2);
		}
	}

	else if (bytes.len >= 2 && (uint8_t)bytes.data [1] == ESC)
	{
		/* two escapes: the first one is a key on its own */
		d = make (ML_KEY_ESCAPE, 0, 0, 1);
	}

	else if (bytes.len >= 2)
	{
		/* Alt and a key: ESC then the key's bytes */
		MlVtDecode inner = decode_plain (
			ml_slice_sub (bytes, 1, bytes.len - 1),
			final
		);

		if (inner.incomplete)
		{
			d = inner;
		}

		else
		{
			d = inner;
			d.key.mods |= ML_MOD_ALT;
			d.used     += 1;
		}
	}

	return d;
}
