#ifndef MATLOG_UI_MARKUP_H
#define MATLOG_UI_MARKUP_H

#include <matlog/core/str.h>
#include <matlog/core/vec.h>

/*
 * Inline formatting of notes, in Markdown:
 *
 *	**bold**   *italic*   _italic_   <u>underline</u>   ~~strike~~   `code`
 *
 * They nest (`**bold and *italic***`, `<u>**both**</u>`) and obey the rules
 * of Markdown for where a mark can open or close: the opening mark is followed
 * by a non space and the closing mark is preceded by one, so `2 * 3 * 4` is
 * text, and an underscore inside a word (`x_1`, `snake_case`) is text. A mark
 * that finds no partner stays as it was typed. A backslash makes the next mark
 * a plain character (`\*`), and the text of a code span is not formatted.
 *
 * The note stays plain text in the session file, markers included. For the
 * screen ml_markup_parse takes the markers away and gives the text with a
 * style for each of its bytes; ml_markup_append writes a part of it with the
 * escape sequences of the styles.
 */

#define ML_STYLE_BOLD      1u
#define ML_STYLE_ITALIC    2u
#define ML_STYLE_UNDERLINE 4u
#define ML_STYLE_STRIKE    8u
#define ML_STYLE_CODE      16u
#define ML_STYLE_MARK      32u /* a mark that is shown (live parse), dim */

ML_VEC_DEFINE (MlByteVec, uint8_t)

typedef struct MlMarkup
{
	MlStr     plain;  /* the text without the markers that took effect */
	MlByteVec styles; /* one ML_STYLE_ mask per byte of plain */
} MlMarkup;

void ml_markup_init (MlMarkup *m);
void ml_markup_free (MlMarkup *m);

/* Replaces the content of m with the formatted form of text. */
MlResult ml_markup_parse (MlMarkup *m, MlSlice text);

/*
 * The live form, for the note being typed: the text is kept as typed, marks
 * included, so the cursor column stays where it is; the marks that took
 * effect are styled ML_STYLE_MARK (drawn dim) and the text between them is
 * styled as it will be: **bold** shows its stars dim and its word bold.
 */
MlResult ml_markup_parse_live (MlMarkup *m, MlSlice text);

/* 1 when any byte of the text has a style. */
int32_t ml_markup_has_styles (const MlMarkup *m);

/*
 * Appends the bytes from..from+len of m.plain to out, with the escape
 * sequences of the styles: every change of style resets and sets the new
 * one, and the style is reset at the end.
 */
MlResult ml_markup_append (
	MlStr          *out,
	const MlMarkup *m,
	uint64_t       from,
	uint64_t       len
);

/*
 * The same with a base style: base is the escape sequence that every reset
 * is followed by (the style of the plain text of the note), code the one of a
 * code span (empty: the default). Either may be empty.
 */
MlResult ml_markup_append_themed (
	MlStr          *out,
	const MlMarkup *m,
	uint64_t       from,
	uint64_t       len,
	MlSlice        base,
	MlSlice        code
);

#endif /* MATLOG_UI_MARKUP_H */
