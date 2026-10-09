#include <matlog/ui/theme.h>

#include <math.h>

static const char *const class_names [ML_CLASS_COUNT] =
{
	[ML_CLASS_TEXT]         = "text",
	[ML_CLASS_NUMBER]       = "number",
	[ML_CLASS_NAME]         = "name",
	[ML_CLASS_FUNCTION]     = "function",
	[ML_CLASS_CONSTANT]     = "constant",
	[ML_CLASS_UNIT]         = "unit",
	[ML_CLASS_OPERATOR]     = "operator",
	[ML_CLASS_PAREN]        = "paren",
	[ML_CLASS_KEYWORD]      = "keyword",
	[ML_CLASS_ERROR]        = "error",
	[ML_CLASS_MATCH]        = "match",
	[ML_CLASS_UNMATCHED]    = "unmatched",
	[ML_CLASS_GUTTER]       = "gutter",
	[ML_CLASS_RESULT]       = "result",
	[ML_CLASS_RESULT_ERROR] = "result-error",
	[ML_CLASS_GHOST]        = "ghost",
	[ML_CLASS_HINT_KEY]     = "hint-key",
	[ML_CLASS_NOTE]         = "note",
	[ML_CLASS_NOTE_CODE]    = "note-code",
	[ML_CLASS_HELP_TITLE]   = "help-title",
	[ML_CLASS_HELP_HEAD]    = "help-heading",
	[ML_CLASS_HELP_LINK]    = "help-link",
	[ML_CLASS_HELP_CODE]    = "help-code"
};

static const char *const level_names [ML_COLORS_COUNT] =
{
	[ML_COLORS_24BIT] = "24bit",
	[ML_COLORS_256]   = "256",
	[ML_COLORS_16]    = "16",
	[ML_COLORS_OFF]   = "off"
};

const char *ml_class_name (MlClass c)
{
	return ml_choose (c < ML_CLASS_COUNT, class_names [c], "?");
}

MlClass ml_class_from_name (MlSlice name)
{
	MlClass found = ML_CLASS_COUNT;

	for (uint32_t i = 0; i < ML_CLASS_COUNT; i++)
	{
		if (ml_slice_equal (name, ml_slice_cstr (class_names [i])))
		{
			found = (MlClass)i;
		}
	}

	return found;
}

const char *ml_color_level_name (MlColorLevel level)
{
	return ml_choose (level < ML_COLORS_COUNT, level_names [level], "?");
}

MlColorLevel ml_color_level_from_name (MlSlice name)
{
	MlColorLevel found = ML_COLORS_COUNT;

	for (uint32_t i = 0; i < ML_COLORS_COUNT; i++)
	{
		if (ml_slice_equal (name, ml_slice_cstr (level_names [i])))
		{
			found = (MlColorLevel)i;
		}
	}

	return found;
}

static int32_t contains (MlSlice text, const char *word)
{
	MlSlice  w     = ml_slice_cstr (word);
	int32_t  found = 0;

	for (uint64_t i = 0; i + w.len <= text.len && !found; i++)
	{
		found = ml_slice_equal (ml_slice_sub (text, i, w.len), w);
	}

	return found;
}

MlColorLevel ml_colors_detect (
	MlSlice colorterm,
	MlSlice term,
	int32_t no_color,
	int32_t os_truecolor
)
{
	MlColorLevel level = ML_COLORS_16;

	if (no_color || ml_slice_equal (term, ml_slice_cstr ("dumb")))
	{
		level = ML_COLORS_OFF;
	}

	else if (
		   contains (colorterm, "truecolor")
		|| contains (colorterm, "24bit")
		|| os_truecolor
	)
	{
		level = ML_COLORS_24BIT;
	}

	else if (contains (term, "256color"))
	{
		level = ML_COLORS_256;
	}

	return level;
}

const char *ml_theme_for_background (MlSlice colorfgbg)
{
	uint64_t start = colorfgbg.len;
	int32_t  value = 0;
	int32_t  ok    = colorfgbg.len > 0;

	/* the number after the last ';' */
	while (start > 0 && colorfgbg.data [start - 1] != ';')
	{
		start--;
	}

	ok = ok && start > 0 && start < colorfgbg.len && colorfgbg.len - start <= 2;

	for (uint64_t i = start; i < colorfgbg.len && ok; i++)
	{
		ok    = colorfgbg.data [i] >= '0' && colorfgbg.data [i] <= '9';
		value = value * 10 + (colorfgbg.data [i] - '0');
	}

	return ml_choose (
		ok && (value == 7 || (value >= 9 && value <= 15)),
		"light",
		"dark"
	);
}

/* ---- styles ------------------------------------------------------------- */

static MlColor rgb (uint32_t value)
{
	MlColor c =
	{
		.set = 1,
		.r   = (uint8_t)(value >> 16),
		.g   = (uint8_t)(value >> 8),
		.b   = (uint8_t)value
	};

	return c;
}

static const MlColor no_color =
{
	.set = 0,
	.r   = 0,
	.g   = 0,
	.b   = 0
};

#define RGB(v) \
	{ \
		.set = 1, \
		.r   = ((v) >> 16) & 0xFF, \
		.g   = ((v) >> 8) & 0xFF, \
		.b   = (v) & 0xFF \
	}
#define COLOR(v) { .fg = RGB (v) }
#define STRONG(v) { .fg = RGB (v), .bold = 1, .underline = 1 }

static const MlStyle plain_style =
{
	.bold = 0
};

typedef struct Builtin
{
	const char *name;
	uint32_t   background;
	int32_t    paint;
	MlStyle    styles [ML_CLASS_COUNT];
} Builtin;

static const Builtin builtins [] =
{
	{
		.name       = "dark",
		.background = 0x1E1E1E,
		.styles     =
		{
			[ML_CLASS_NUMBER]       = COLOR (0xB5CEA8),
			[ML_CLASS_NAME]         = COLOR (0x9CDCFE),
			[ML_CLASS_FUNCTION]     = COLOR (0xDCDCAA),
			[ML_CLASS_CONSTANT]     = COLOR (0x4FC1FF),
			[ML_CLASS_UNIT]         = COLOR (0x4EC9B0),
			[ML_CLASS_OPERATOR]     = COLOR (0xD4D4D4),
			[ML_CLASS_PAREN]        = COLOR (0xFFD700),
			[ML_CLASS_KEYWORD]      = COLOR (0xC586C0),
			[ML_CLASS_ERROR]        = { .fg = RGB (0xF44747), .underline = 1 },
			[ML_CLASS_MATCH]        = { .bold = 1, .underline = 1 },
			[ML_CLASS_UNMATCHED]    = { .fg = RGB (0xF44747), .bold = 1 },
			[ML_CLASS_GUTTER]       = { .dim = 1 },
			[ML_CLASS_RESULT]       = { .dim = 1 },
			[ML_CLASS_RESULT_ERROR] = COLOR (0xF44747),
			[ML_CLASS_GHOST]        = { .dim = 1 },
			[ML_CLASS_HINT_KEY]     = { .fg = RGB (0x4FC1FF), .bold = 1 },
			[ML_CLASS_NOTE_CODE]    = COLOR (0x4EC9B0),
			[ML_CLASS_HELP_TITLE]   = { .fg = RGB (0x569CD6), .bold = 1 },
			[ML_CLASS_HELP_HEAD]    = { .fg = RGB (0x4EC9B0), .bold = 1 },
			[ML_CLASS_HELP_LINK]    = { .fg = RGB (0x4FC1FF), .underline = 1 },
			[ML_CLASS_HELP_CODE]    = COLOR (0xCE9178)
		}
	},
	{
		.name       = "light",
		.background = 0xFFFFFF,
		.paint      = 1,
		.styles     =
		{
			[ML_CLASS_TEXT]         = COLOR (0x1F1F1F),
			[ML_CLASS_NUMBER]       = COLOR (0x098658),
			[ML_CLASS_NAME]         = COLOR (0x001080),
			[ML_CLASS_FUNCTION]     = COLOR (0x795E26),
			[ML_CLASS_CONSTANT]     = COLOR (0x0070C1),
			[ML_CLASS_UNIT]         = COLOR (0x267F99),
			[ML_CLASS_OPERATOR]     = COLOR (0x333333),
			[ML_CLASS_PAREN]        = COLOR (0x7A5C00),
			[ML_CLASS_KEYWORD]      = COLOR (0xAF00DB),
			[ML_CLASS_ERROR]        = { .fg = RGB (0xCD3131), .underline = 1 },
			[ML_CLASS_MATCH]        = { .bold = 1, .underline = 1 },
			[ML_CLASS_UNMATCHED]    = { .fg = RGB (0xCD3131), .bold = 1 },
			[ML_CLASS_GUTTER]       = COLOR (0x6E7781),
			[ML_CLASS_RESULT]       = COLOR (0x57606A),
			[ML_CLASS_RESULT_ERROR] = COLOR (0xCD3131),
			[ML_CLASS_GHOST]        = COLOR (0x8C959F),
			[ML_CLASS_HINT_KEY]     = { .fg = RGB (0x0070C1), .bold = 1 },
			[ML_CLASS_NOTE_CODE]    = COLOR (0x267F99),
			[ML_CLASS_HELP_TITLE]   = { .fg = RGB (0x0451A5), .bold = 1 },
			[ML_CLASS_HELP_HEAD]    = { .fg = RGB (0x267F99), .bold = 1 },
			[ML_CLASS_HELP_LINK]    = { .fg = RGB (0x0070C1), .underline = 1 },
			[ML_CLASS_HELP_CODE]    = COLOR (0xA31515)
		}
	},
	{
		.name       = "contrast",
		.background = 0x000000,
		.paint      = 1,
		.styles     =
		{
			[ML_CLASS_TEXT]         = COLOR (0xFFFFFF),
			[ML_CLASS_NUMBER]       = COLOR (0x7FFF7F),
			[ML_CLASS_NAME]         = COLOR (0x7FDFFF),
			[ML_CLASS_FUNCTION]     = COLOR (0xFFFF7F),
			[ML_CLASS_CONSTANT]     = COLOR (0xFFBF7F),
			[ML_CLASS_UNIT]         = COLOR (0x7FFFD4),
			[ML_CLASS_OPERATOR]     = COLOR (0xFFFFFF),
			[ML_CLASS_PAREN]        = COLOR (0xFFFFFF),
			[ML_CLASS_KEYWORD]      = COLOR (0xFF9FFF),
			[ML_CLASS_ERROR]        = STRONG (0xFF7F7F),
			[ML_CLASS_MATCH]        = STRONG (0xFFFFFF),
			[ML_CLASS_UNMATCHED]    = { .fg = RGB (0xFF7F7F), .bold = 1 },
			[ML_CLASS_GUTTER]       = COLOR (0xC0C0C0),
			[ML_CLASS_RESULT]       = COLOR (0xFFFFFF),
			[ML_CLASS_RESULT_ERROR] = COLOR (0xFF7F7F),
			[ML_CLASS_GHOST]        = COLOR (0xC0C0C0),
			[ML_CLASS_HINT_KEY]     = { .fg = RGB (0x7FDFFF), .bold = 1 },
			[ML_CLASS_NOTE]         = COLOR (0xFFFFFF),
			[ML_CLASS_NOTE_CODE]    = COLOR (0x7FFFD4),
			[ML_CLASS_HELP_TITLE]   = { .fg = RGB (0x7FDFFF), .bold = 1 },
			[ML_CLASS_HELP_HEAD]    = { .fg = RGB (0x7FFFD4), .bold = 1 },
			[ML_CLASS_HELP_LINK]    = { .fg = RGB (0xFFFF7F), .underline = 1 },
			[ML_CLASS_HELP_CODE]    = COLOR (0xFFBF7F)
		}
	},
	{
		.name       = "mono",
		.background = 0x000000,
		.styles     =
		{
			[ML_CLASS_FUNCTION]     = { .bold = 1 },
			[ML_CLASS_CONSTANT]     = { .bold = 1 },
			[ML_CLASS_UNIT]         = { .underline = 1 },
			[ML_CLASS_PAREN]        = { .dim = 1 },
			[ML_CLASS_KEYWORD]      = { .bold = 1 },
			[ML_CLASS_ERROR]        = { .bold = 1, .underline = 1 },
			[ML_CLASS_MATCH]        = { .bold = 1, .underline = 1 },
			[ML_CLASS_UNMATCHED]    = { .reverse = 1 },
			[ML_CLASS_GUTTER]       = { .dim = 1 },
			[ML_CLASS_RESULT]       = { .dim = 1 },
			[ML_CLASS_RESULT_ERROR] = { .bold = 1 },
			[ML_CLASS_GHOST]        = { .dim = 1 },
			[ML_CLASS_HINT_KEY]     = { .bold = 1 },
			[ML_CLASS_NOTE_CODE]    = { .bold = 1 },
			[ML_CLASS_HELP_TITLE]   = { .bold = 1, .underline = 1 },
			[ML_CLASS_HELP_HEAD]    = { .bold = 1 },
			[ML_CLASS_HELP_LINK]    = { .underline = 1 },
			[ML_CLASS_HELP_CODE]    = { .dim = 1 }
		}
	}
};

uint64_t ml_theme_builtin_count (void)
{
	return ml_count (builtins);
}

const char *ml_theme_builtin_name (uint64_t index)
{
	return ml_choose (index < ml_count (builtins), builtins [index].name, "");
}

void ml_theme_set_name (MlTheme *theme, MlSlice name)
{
	uint64_t n = ml_choose (
		name.len < ML_THEME_NAME_MAX,
		name.len,
		ML_THEME_NAME_MAX - 1
	);

	for (uint64_t i = 0; i < n; i++)
	{
		theme->name [i] = name.data [i];
	}

	theme->name [n] = 0;
}

static void theme_from (MlTheme *out, const Builtin *b)
{
	uint64_t n = 0;

	while (b->name [n] != 0 && n + 1 < ML_THEME_NAME_MAX)
	{
		out->name [n] = b->name [n];
		n++;
	}

	out->name [n]   = 0;
	out->background = rgb (b->background);
	out->paint      = b->paint;

	for (uint32_t c = 0; c < ML_CLASS_COUNT; c++)
	{
		out->styles [c] = b->styles [c];
	}
}

MlResult ml_theme_builtin (MlTheme *out, MlSlice name)
{
	MlResult r = ML_ERR_INVALID;

	for (uint64_t i = 0; i < ml_count (builtins); i++)
	{
		if (ml_slice_equal (name, ml_slice_cstr (builtins [i].name)))
		{
			theme_from (out, &builtins [i]);
			r = ML_OK;
		}
	}

	return r;
}

MlTheme ml_theme_default (void)
{
	MlTheme t;

	theme_from (&t, &builtins [0]);
	return t;
}

static int32_t color_equal (MlColor a, MlColor b)
{
	return (
		   a.set == b.set
		&& (
			   !a.set
			|| (a.r == b.r && a.g == b.g && a.b == b.b)
		)
	);
}

int32_t ml_style_is_plain (MlStyle s)
{
	return (
		   !s.fg.set
		&& !s.bg.set
		&& !s.bold
		&& !s.dim
		&& !s.underline
		&& !s.reverse
	);
}

int32_t ml_style_equal (MlStyle a, MlStyle b)
{
	return (
		   color_equal (a.fg, b.fg)
		&& color_equal (a.bg, b.bg)
		&& a.bold      == b.bold
		&& a.dim       == b.dim
		&& a.underline == b.underline
		&& a.reverse   == b.reverse
	);
}

/* ---- escape sequences --------------------------------------------------- */

static int32_t sq (int32_t v)
{
	return v * v;
}

/* The 16 ANSI colours as xterm shows them. */
static const uint32_t ansi16 [16] =
{
	0x000000, 0xCD0000, 0x00CD00, 0xCDCD00,
	0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
	0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00,
	0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF
};

static const int32_t cube_levels [6] = { 0, 95, 135, 175, 215, 255 };

static int32_t distance (MlColor c, int32_t r, int32_t g, int32_t b)
{
	return (
		  sq ((int32_t)c.r - r)
		+ sq ((int32_t)c.g - g)
		+ sq ((int32_t)c.b - b)
	);
}

static int32_t cube_index (int32_t v)
{
	int32_t best  = 0;
	int32_t delta = sq (v);

	for (int32_t i = 1; i < 6; i++)
	{
		if (sq (v - cube_levels [i]) < delta)
		{
			delta = sq (v - cube_levels [i]);
			best  = i;
		}
	}

	return best;
}

/* The nearest of the 256 colours: the 6x6x6 cube or the 24 greys. */
static int32_t nearest_256 (MlColor c)
{
	int32_t ri    = cube_index (c.r);
	int32_t gi    = cube_index (c.g);
	int32_t bi    = cube_index (c.b);
	int32_t cube  = 16 + 36 * ri + 6 * gi + bi;
	int32_t dcube = distance (
		c,
		cube_levels [ri],
		cube_levels [gi],
		cube_levels [bi]
	);
	int32_t avg   = ((int32_t)c.r + (int32_t)c.g + (int32_t)c.b) / 3;
	int32_t gi24  = (avg - 8 + 5) / 10;
	int32_t index = cube;

	gi24 = ml_choose (gi24 < 0, 0, ml_choose (gi24 > 23, 23, gi24));

	if (distance (c, 8 + 10 * gi24, 8 + 10 * gi24, 8 + 10 * gi24) < dcube)
	{
		index = 232 + gi24;
	}

	return index;
}

static int32_t nearest_16 (MlColor c)
{
	int32_t best  = 0;
	int32_t delta = 0x7FFFFFFF;

	for (int32_t i = 0; i < 16; i++)
	{
		int32_t d = distance (
			c,
			(int32_t)((ansi16 [i] >> 16) & 0xFF),
			(int32_t)((ansi16 [i] >> 8) & 0xFF),
			(int32_t)(ansi16 [i] & 0xFF)
		);

		if (d < delta)
		{
			delta = d;
			best  = i;
		}
	}

	return best;
}

/* Appends one parameter of an SGR sequence, with its ';'. */
static MlResult add_param (MlStr *params, const char *text)
{
	MlResult r = ML_OK;

	if (params->len > 0)
	{
		r = ml_str_append (params, ml_slice_cstr (";"));
	}

	if (r == ML_OK)
	{
		r = ml_str_append (params, ml_slice_cstr (text));
	}

	return r;
}

static MlResult add_color (
	MlStr        *params,
	MlColor      c,
	int32_t      background,
	MlColorLevel level
)
{
	MlResult r    = ML_OK;
	int32_t  base = ml_choose (background, 40, 30);

	if (c.set && level == ML_COLORS_24BIT)
	{
		r = add_param (params, ml_choose (background, "48;2", "38;2"));

		if (r == ML_OK)
		{
			r = ml_str_appendf (
				params,
				";%u;%u;%u",
				(uint32_t)c.r,
				(uint32_t)c.g,
				(uint32_t)c.b
			);
		}
	}

	else if (c.set && level == ML_COLORS_256)
	{
		r = add_param (params, ml_choose (background, "48;5", "38;5"));

		if (r == ML_OK)
		{
			r = ml_str_appendf (params, ";%d", nearest_256 (c));
		}
	}

	else if (c.set && level == ML_COLORS_16)
	{
		int32_t idx = nearest_16 (c);

		r = add_param (params, "");

		if (r == ML_OK)
		{
			r = ml_str_appendf (
				params,
				"%d",
				ml_choose (idx < 8, base + idx, base + 60 + idx - 8)
			);
		}
	}

	return r;
}

MlResult ml_style_sgr (MlStr *out, MlStyle s, MlColorLevel level)
{
	MlResult r = ML_OK;
	MlStr    params;

	ml_str_init (&params);

	if (s.bold)
	{
		r = add_param (&params, "1");
	}

	if (r == ML_OK && s.dim)
	{
		r = add_param (&params, "2");
	}

	if (r == ML_OK && s.underline)
	{
		r = add_param (&params, "4");
	}

	if (r == ML_OK && s.reverse)
	{
		r = add_param (&params, "7");
	}

	if (r == ML_OK)
	{
		r = add_color (&params, s.fg, 0, level);
	}

	if (r == ML_OK)
	{
		r = add_color (&params, s.bg, 1, level);
	}

	if (r == ML_OK && params.len > 0)
	{
		r = ml_str_appendf (
			out,
			"\x1b[%.*sm",
			(int)params.len,
			params.data
		);
	}

	ml_str_free (&params);
	return r;
}

static double channel (uint8_t v)
{
	double c = (double)v / 255.0;

	return ml_choose (
		c <= 0.03928,
		c / 12.92,
		pow ((c + 0.055) / 1.055, 2.4)
	);
}

static double luminance (MlColor c)
{
	return (
		  0.2126 * channel (c.r)
		+ 0.7152 * channel (c.g)
		+ 0.0722 * channel (c.b)
	);
}

double ml_contrast_ratio (MlColor a, MlColor b)
{
	double la = luminance (a);
	double lb = luminance (b);

	return (
		  (ml_choose (la > lb, la, lb) + 0.05)
		/ (ml_choose (la > lb, lb, la) + 0.05)
	);
}

/* ---- parsing ------------------------------------------------------------ */

static int32_t is_blank (char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static MlSlice trim (MlSlice s)
{
	uint64_t start = 0;
	uint64_t end   = s.len;

	while (start < end && is_blank (s.data [start]))
	{
		start++;
	}

	while (end > start && is_blank (s.data [end - 1]))
	{
		end--;
	}

	return ml_slice_sub (s, start, end - start);
}

static int32_t hex_value (char c)
{
	int32_t v = -1;

	if (c >= '0' && c <= '9')
	{
		v = c - '0';
	}

	else if (c >= 'a' && c <= 'f')
	{
		v = c - 'a' + 10;
	}

	else if (c >= 'A' && c <= 'F')
	{
		v = c - 'A' + 10;
	}

	return v;
}

/* "#rrggbb" or "#rgb"; ML_ERR_PARSE for anything else. */
static MlResult parse_color (MlColor *out, MlSlice word)
{
	MlResult r      = ML_ERR_PARSE;
	int32_t  digits = (int32_t)word.len - 1;
	int32_t  ok     = word.len > 0 && word.data [0] == '#';
	int32_t  v [6]  = { 0, 0, 0, 0, 0, 0 };

	ok = ok && (digits == 3 || digits == 6);

	for (int32_t i = 0; i < digits && ok; i++)
	{
		v [i] = hex_value (word.data [i + 1]);
		ok    = v [i] >= 0;
	}

	if (ok)
	{
		MlColor c = no_color;

		c.set = 1;
		c.r   = (uint8_t)ml_choose (
			digits == 3,
			v [0] * 17,
			v [0] * 16 + v [1]
		);
		c.g   = (uint8_t)ml_choose (
			digits == 3,
			v [1] * 17,
			v [2] * 16 + v [3]
		);
		c.b   = (uint8_t)ml_choose (
			digits == 3,
			v [2] * 17,
			v [4] * 16 + v [5]
		);
		*out  = c;
		r     = ML_OK;
	}

	return r;
}

/* The next word of text from pos on; a slice of length 0 at the end. */
static MlSlice next_word (MlSlice text, uint64_t *pos)
{
	uint64_t start;

	while (*pos < text.len && is_blank (text.data [*pos]))
	{
		(*pos)++;
	}

	start = *pos;

	while (*pos < text.len && !is_blank (text.data [*pos]))
	{
		(*pos)++;
	}

	return ml_slice_sub (text, start, *pos - start);
}

static int32_t word_is (MlSlice word, const char *text)
{
	return ml_slice_equal (word, ml_slice_cstr (text));
}

MlResult ml_style_parse (MlStyle *out, MlSlice text)
{
	MlResult r    = ML_OK;
	MlStyle  s    = plain_style;
	uint64_t pos  = 0;
	int32_t  done = 0;

	while (r == ML_OK && !done)
	{
		MlSlice word = next_word (text, &pos);
		MlColor probe;

		if (word.len == 0)
		{
			done = 1;
		}

		else if (word_is (word, "bold"))
		{
			s.bold = 1;
		}

		else if (word_is (word, "dim"))
		{
			s.dim = 1;
		}

		else if (word_is (word, "underline"))
		{
			s.underline = 1;
		}

		else if (word_is (word, "reverse"))
		{
			s.reverse = 1;
		}

		else if (word_is (word, "none"))
		{
			s = plain_style;
		}

		else if (word_is (word, "on"))
		{
			r = parse_color (&s.bg, next_word (text, &pos));
		}

		else if (parse_color (&probe, word) == ML_OK)
		{
			s.fg = probe;
		}

		/* a '#' that is not a colour starts a comment */
		else if (word.data [0] == '#')
		{
			done = 1;
		}

		else
		{
			r = ML_ERR_PARSE;
		}
	}

	if (r == ML_OK)
	{
		*out = s;
	}

	return r;
}

MlResult ml_theme_apply_line (MlTheme *theme, MlSlice line)
{
	MlResult r     = ML_OK;
	MlSlice  t     = trim (line);
	uint64_t eq    = 0;

	while (eq < t.len && t.data [eq] != '=')
	{
		eq++;
	}

	if (t.len > 0 && t.data [0] != '#')
	{
		MlSlice name = trim (ml_slice_sub (t, 0, eq));
		MlSlice rest = ml_choose (
			eq < t.len,
			ml_slice_sub (t, eq + 1, t.len - eq - 1),
			ml_slice ("", 0)
		);
		MlClass c    = ml_class_from_name (name);

		if (eq >= t.len)
		{
			r = ML_ERR_PARSE;
		}

		else if (word_is (name, "paint"))
		{
			uint64_t pos  = 0;
			MlSlice  word = next_word (rest, &pos);

			r = ml_choose (
				word_is (word, "on") || word_is (word, "off"),
				ML_OK,
				ML_ERR_PARSE
			);

			if (r == ML_OK)
			{
				theme->paint = word_is (word, "on");
			}
		}

		else if (word_is (name, "background"))
		{
			uint64_t pos  = 0;
			MlColor  next = no_color;

			r = parse_color (&next, next_word (rest, &pos));

			if (r == ML_OK)
			{
				theme->background = next;
			}
		}

		else if (c == ML_CLASS_COUNT)
		{
			r = ML_ERR_PARSE;
		}

		else
		{
			MlStyle s = plain_style;

			r = ml_style_parse (&s, rest);

			if (r == ML_OK)
			{
				theme->styles [c] = s;
			}
		}
	}

	return r;
}

static MlResult append_color (MlStr *out, MlColor c)
{
	return ml_str_appendf (
		out,
		"#%02x%02x%02x",
		(uint32_t)c.r,
		(uint32_t)c.g,
		(uint32_t)c.b
	);
}

/* Appends a word of a style, with a space in front unless it is the first. */
static MlResult add_word (MlStr *out, int32_t *first, const char *word)
{
	MlResult r = ML_OK;

	if (!*first)
	{
		r = ml_str_append (out, ml_slice_cstr (" "));
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr (word));
	}

	*first = 0;
	return r;
}

/* The style words of a style, as ml_style_parse reads them. */
static MlResult format_style (MlStr *out, MlStyle s)
{
	MlResult r     = ML_OK;
	int32_t  first = 1;

	if (s.fg.set)
	{
		r     = append_color (out, s.fg);
		first = 0;
	}

	if (r == ML_OK && s.bg.set)
	{
		r = add_word (out, &first, "on");

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_slice_cstr (" "));
		}

		if (r == ML_OK)
		{
			r = append_color (out, s.bg);
		}
	}

	if (r == ML_OK && s.bold)
	{
		r = add_word (out, &first, "bold");
	}

	if (r == ML_OK && s.dim)
	{
		r = add_word (out, &first, "dim");
	}

	if (r == ML_OK && s.underline)
	{
		r = add_word (out, &first, "underline");
	}

	if (r == ML_OK && s.reverse)
	{
		r = add_word (out, &first, "reverse");
	}

	if (r == ML_OK && first)
	{
		r = add_word (out, &first, "none");
	}

	return r;
}

MlResult ml_theme_format (MlStr *out, const MlTheme *theme)
{
	MlResult r = ml_str_appendf (out, "background   = ");

	if (r == ML_OK)
	{
		r = append_color (out, theme->background);
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr ("\n"));
	}

	if (r == ML_OK)
	{
		r = ml_str_appendf (
			out,
			"paint        = %s\n",
			ml_choose (theme->paint, "on", "off")
		);
	}

	for (uint32_t c = 0; c < ML_CLASS_COUNT && r == ML_OK; c++)
	{
		r = ml_str_appendf (out, "%-12s = ", class_names [c]);

		if (r == ML_OK)
		{
			r = format_style (out, theme->styles [c]);
		}

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_slice_cstr ("\n"));
		}
	}

	return r;
}

MlResult ml_theme_apply_text (
	MlTheme  *theme,
	MlSlice  text,
	MlStr    *report,
	uint64_t *bad
)
{
	MlResult r      = ML_OK;
	uint64_t pos    = 0;
	uint64_t number = 0;

	*bad = 0;

	while (pos < text.len && r == ML_OK)
	{
		uint64_t end = pos;

		while (end < text.len && text.data [end] != '\n')
		{
			end++;
		}

		number++;

		MlSlice line = ml_slice_sub (text, pos, end - pos);

		if (ml_theme_apply_line (theme, line) != ML_OK)
		{
			(*bad)++;

			if (report != NULL && report->len == 0)
			{
				r = ml_str_appendf (
					report,
					"line %u: not a class = style entry",
					(uint32_t)number
				);
			}
		}

		pos = end + 1;
	}

	return r;
}
