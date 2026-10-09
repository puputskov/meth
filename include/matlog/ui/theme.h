#ifndef MATLOG_UI_THEME_H
#define MATLOG_UI_THEME_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>

/*
 * Themes (plan section 4.10). Text is split into classes and a theme gives
 * every class a style: foreground and background colour as 24 bit RGB, bold,
 * dim, underline and reverse. A style is turned into an escape sequence for
 * the colour level of the terminal: 24 bit, 256 colours, the 16 ANSI colours,
 * or none (only bold, dim, underline and reverse are left).
 *
 * A theme does not paint a background of its own: the terminal's background
 * stays. `background` is the colour the theme expects it to have, which the
 * contrast test and the choice of a built in theme use.
 *
 * A user theme is a text file with one `class = style` entry per line:
 *
 *	number = #b5cea8
 *	error  = #f44747 underline
 *	status = #ffffff on #264f78 bold
 *
 * A style is an optional foreground `#rrggbb`, an optional `on #rrggbb`
 * background and the words bold, dim, underline, reverse; `none` is a style
 * without anything. `#` after white space starts a comment. Classes that the
 * file does not name keep the style of the theme it is applied to.
 */

typedef enum MlClass
{
	ML_CLASS_TEXT = 0, /* anything else, also white space */
	ML_CLASS_NUMBER,
	ML_CLASS_NAME,     /* a variable, or a name that stands for nothing */
	ML_CLASS_FUNCTION, /* built in or user defined function */
	ML_CLASS_CONSTANT, /* pi, e */
	ML_CLASS_UNIT,
	ML_CLASS_OPERATOR,
	ML_CLASS_PAREN,    /* ( ) [ ] and the comma */
	ML_CLASS_KEYWORD,  /* include, lambda */
	ML_CLASS_ERROR,    /* a bad character, the range of an error */
	ML_CLASS_MATCH,    /* the bracket under the cursor and its partner */
	ML_CLASS_UNMATCHED,
	ML_CLASS_GUTTER,   /* line number and domain label */
	ML_CLASS_RESULT,
	ML_CLASS_RESULT_ERROR,
	ML_CLASS_GHOST,    /* the preview of the result, completion text */
	ML_CLASS_HINT_KEY, /* the important part of a hint, see hint.h */
	ML_CLASS_COUNT
} MlClass;

typedef enum MlColorLevel
{
	ML_COLORS_24BIT = 0,
	ML_COLORS_256,
	ML_COLORS_16,
	ML_COLORS_OFF, /* no colours, the attributes stay */
	ML_COLORS_COUNT
} MlColorLevel;

typedef struct MlColor
{
	int32_t set; /* 0: the colour of the terminal */
	uint8_t r;
	uint8_t g;
	uint8_t b;
} MlColor;

typedef struct MlStyle
{
	MlColor fg;
	MlColor bg;
	int32_t bold;
	int32_t dim;
	int32_t underline;
	int32_t reverse;
} MlStyle;

#define ML_THEME_NAME_MAX 32

typedef struct MlTheme
{
	char    name [ML_THEME_NAME_MAX];
	MlColor background; /* what the theme expects behind the text */
	MlStyle styles [ML_CLASS_COUNT];
} MlTheme;

/* "number", "function" ... as written in theme files. */
const char *ml_class_name (MlClass c);

/* ML_CLASS_COUNT when the name is none of them. */
MlClass ml_class_from_name (MlSlice name);

/* "24bit", "256", "16", "off". */
const char *ml_color_level_name (MlColorLevel level);

/* ML_COLORS_COUNT when the name is none of them. */
MlColorLevel ml_color_level_from_name (MlSlice name);

/*
 * The level for a terminal from its environment: colorterm and term are the
 * values of COLORTERM and TERM (empty when unset), no_color is 1 when NO_COLOR
 * is set to anything, os_truecolor is 1 when the OS console is known to show
 * 24 bit colour (see ml_console_truecolor).
 */
MlColorLevel ml_colors_detect (
	MlSlice colorterm,
	MlSlice term,
	int32_t no_color,
	int32_t os_truecolor
);

/* Built in themes: "dark", "light", "contrast", "mono". */
uint64_t ml_theme_builtin_count (void);
const char *ml_theme_builtin_name (uint64_t index);

/* ML_ERR_INVALID when there is no built in theme of that name. */
MlResult ml_theme_builtin (MlTheme *out, MlSlice name);

/* The dark theme, the one that missing entries fall back to. */
MlTheme ml_theme_default (void);

/* Sets the name of a theme (cut to ML_THEME_NAME_MAX - 1 bytes). */
void ml_theme_set_name (MlTheme *theme, MlSlice name);

/* 1 when the style draws nothing special (no colour, no attribute). */
int32_t ml_style_is_plain (MlStyle s);
int32_t ml_style_equal (MlStyle a, MlStyle b);

/*
 * Appends the escape sequence that switches to the style, for the colour level.
 * It does not reset first (the caller writes "\x1b[0m" before when something
 * else is on) and writes nothing for a plain style.
 */
MlResult ml_style_sgr (MlStr *out, MlStyle s, MlColorLevel level);

/* The WCAG contrast ratio of two colours, 1.0 to 21.0. */
double ml_contrast_ratio (MlColor a, MlColor b);

/*
 * Parses the style words of an entry ("#b5cea8 bold", "#fff on #000").
 * ML_ERR_PARSE when something in it is not a style word.
 */
MlResult ml_style_parse (MlStyle *out, MlSlice text);

/*
 * Applies one `class = style` line to the theme. Empty lines and comments do
 * nothing. ML_ERR_PARSE when the line is not an entry or names no class.
 */
MlResult ml_theme_apply_line (MlTheme *theme, MlSlice line);

/*
 * Applies the lines of a theme file. A line that does not parse is left out
 * and a message "line N: ..." is appended to report (when it is not NULL).
 * The number of lines left out is returned in bad; ML_ERR_NOMEM is the only
 * failure.
 */
MlResult ml_theme_apply_text (
	MlTheme  *theme,
	MlSlice  text,
	MlStr    *report,
	uint64_t *bad
);

#endif /* MATLOG_UI_THEME_H */
