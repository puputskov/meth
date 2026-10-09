#include <matlog/core/utf8.h>

#include <string.h>

/* Sequence length from the lead byte; 0 for bytes that cannot start a sequence. */
static uint64_t seq_len (uint8_t lead)
{
	uint64_t n = 0;

	if (lead < 0x80)
	{
		n = 1;
	}

	else if (lead >= 0xC2 && lead <= 0xDF)
	{
		n = 2;
	}

	else if (lead >= 0xE0 && lead <= 0xEF)
	{
		n = 3;
	}

	else if (lead >= 0xF0 && lead <= 0xF4)
	{
		n = 4;
	}

	return n;
}

static uint8_t byte_at (MlSlice s, uint64_t i)
{
	return (uint8_t)s.data [i];
}

/* The payload bits of a lead byte: a 1 byte sequence uses 7 bits, a 2, 3, 4 byte one 5, 4, 3. */
static uint32_t lead_payload (uint8_t lead, uint64_t n)
{
	return n == 1 ? lead : (uint32_t)(lead & (0xFFu >> (n + 1)));
}

MlDecoded ml_utf8_decode (MlSlice s)
{
	MlDecoded d;

	d.cp   = 0;
	d.size = 0;

	if (s.len > 0)
	{
		uint64_t n  = seq_len (byte_at (s, 0));
		uint32_t c  = lead_payload (byte_at (s, 0), n);
		int32_t  ok = n != 0 && n <= s.len;

		for (uint64_t i = 1; ok && i < n; i++)
		{
			ok = (byte_at (s, i) & 0xC0) == 0x80;
			c = (c << 6) | (byte_at (s, i) & 0x3F);
		}

		/* overlong forms, surrogates, beyond U+10FFFF */
		ok = ok && !(n == 2 && c < 0x80) && !(n == 3 && c < 0x800) &&
			!(n == 4 && c < 0x10000) && !(c >= 0xD800 && c <= 0xDFFF) && c <= 0x10FFFF;

		d.cp   = ok ? c : ML_CP_REPLACEMENT;
		d.size = ok ? n : 1;
	}

	return d;
}

MlEncoded ml_utf8_encode (uint32_t cp)
{
	MlEncoded e;

	memset (&e, 0, sizeof e);

	if (cp < 0x80)
	{
		e.bytes [0] = (char)cp;
		e.size      = 1;
	}

	else if (cp < 0x800)
	{
		e.bytes [0] = (char)(0xC0 | (cp >> 6));
		e.bytes [1] = (char)(0x80 | (cp & 0x3F));
		e.size      = 2;
	}

	else if (cp >= 0xD800 && cp <= 0xDFFF)
	{
		e.size = 0;
	}

	else if (cp < 0x10000)
	{
		e.bytes [0] = (char)(0xE0 | (cp >> 12));
		e.bytes [1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		e.bytes [2] = (char)(0x80 | (cp & 0x3F));
		e.size      = 3;
	}

	else if (cp <= 0x10FFFF)
	{
		e.bytes [0] = (char)(0xF0 | (cp >> 18));
		e.bytes [1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		e.bytes [2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		e.bytes [3] = (char)(0x80 | (cp & 0x3F));
		e.size      = 4;
	}

	return e;
}

int32_t ml_utf8_valid (MlSlice s)
{
	uint64_t pos = 0;
	int32_t  ok  = 1;

	while (ok && pos < s.len)
	{
		MlDecoded d = ml_utf8_decode (ml_slice_sub (s, pos, s.len - pos));

		/* a genuine U+FFFD takes 3 bytes; a 1 byte result for it means malformed */
		ok = !(d.cp == ML_CP_REPLACEMENT && d.size == 1);
		pos += d.size;
	}

	return ok;
}

uint64_t ml_utf8_count (MlSlice s)
{
	uint64_t pos   = 0;
	uint64_t count = 0;

	while (pos < s.len)
	{
		pos += ml_utf8_decode (ml_slice_sub (s, pos, s.len - pos)).size;
		count++;
	}

	return count;
}

uint64_t ml_utf8_next (MlSlice s, uint64_t pos)
{
	return pos < s.len ? pos + ml_utf8_decode (ml_slice_sub (s, pos, s.len - pos)).size : s.len;
}

uint64_t ml_utf8_prev (MlSlice s, uint64_t pos)
{
	uint64_t i     = pos > 0 ? pos - 1 : 0;
	uint64_t limit = pos > 4 ? pos - 4 : 0;

	/* step back over at most 3 continuation bytes */
	while (i > limit && (byte_at (s, i) & 0xC0) == 0x80)
	{
		i--;
	}

	return i;
}

typedef struct Range
{
	uint32_t lo;
	uint32_t hi;
	int32_t  width;
} Range;

/* Code points >= U+0300 that are not one column wide. */
static const Range special_widths [] =
{
	{ .lo = 0x0300, .hi = 0x036F, .width = 0 }, { .lo = 0x0483, .hi = 0x0489, .width = 0 },
	{ .lo = 0x0591, .hi = 0x05BD, .width = 0 }, { .lo = 0x1100, .hi = 0x115F, .width = 2 },
	{ .lo = 0x1AB0, .hi = 0x1AFF, .width = 0 }, { .lo = 0x1DC0, .hi = 0x1DFF, .width = 0 },
	{ .lo = 0x200B, .hi = 0x200F, .width = 0 }, { .lo = 0x202A, .hi = 0x202E, .width = 0 },
	{ .lo = 0x2060, .hi = 0x2064, .width = 0 }, { .lo = 0x20D0, .hi = 0x20FF, .width = 0 },
	{ .lo = 0x2E80, .hi = 0x303E, .width = 2 }, { .lo = 0x3041, .hi = 0x33FF, .width = 2 },
	{ .lo = 0x3400, .hi = 0x4DBF, .width = 2 }, { .lo = 0x4E00, .hi = 0xA4CF, .width = 2 },
	{ .lo = 0xA960, .hi = 0xA97F, .width = 2 }, { .lo = 0xAC00, .hi = 0xD7A3, .width = 2 },
	{ .lo = 0xF900, .hi = 0xFAFF, .width = 2 }, { .lo = 0xFE00, .hi = 0xFE0F, .width = 0 },
	{ .lo = 0xFE20, .hi = 0xFE2F, .width = 0 }, { .lo = 0xFE30, .hi = 0xFE6F, .width = 2 },
	{ .lo = 0xFEFF, .hi = 0xFEFF, .width = 0 }, { .lo = 0xFF00, .hi = 0xFF60, .width = 2 },
	{ .lo = 0xFFE0, .hi = 0xFFE6, .width = 2 }, { .lo = 0x1F300, .hi = 0x1F64F, .width = 2 },
	{ .lo = 0x1F900, .hi = 0x1F9FF, .width = 2 }, { .lo = 0x20000, .hi = 0x3FFFD, .width = 2 },
	{ .lo = 0xE0100, .hi = 0xE01EF, .width = 0 }
};

int32_t ml_cp_width (uint32_t cp)
{
	int32_t w     = 1;
	int32_t found = 0;

	if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0))
	{
		w = 0;
	}

	else if (cp >= 0x300)
	{
		for (uint64_t i = 0; i < sizeof special_widths / sizeof special_widths [0] && !found; i++)
		{
			found = cp >= special_widths [i].lo && cp <= special_widths [i].hi;
			w = found ? special_widths [i].width : w;
		}
	}

	return w;
}

uint64_t ml_utf8_width (MlSlice s)
{
	uint64_t pos   = 0;
	uint64_t width = 0;

	while (pos < s.len)
	{
		MlDecoded d = ml_utf8_decode (ml_slice_sub (s, pos, s.len - pos));

		width += (uint64_t)ml_cp_width (d.cp);
		pos += d.size;
	}

	return width;
}

int32_t ml_cp_is_word (uint32_t cp)
{
	int32_t ascii = (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
	int32_t latin = (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7) || cp == 0xAA || cp == 0xB5 || cp == 0xBA;
	int32_t greek = (cp >= 0x370 && cp <= 0x3FF && cp != 0x37E && cp != 0x387);
	int32_t other = (cp >= 0x400 && cp <= 0x52F) || (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x4E00 && cp <= 0x9FFF);

	return ascii || latin || greek || other;
}
