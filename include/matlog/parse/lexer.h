#ifndef MATLOG_PARSE_LEXER_H
#define MATLOG_PARSE_LEXER_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>
#include <matlog/core/vec.h>

/*
 * Tokens of the algebra syntax. Every token knows the byte range of the source
 * text it came from, so later stages can point at the exact place of an error.
 * The ASCII and unicode spellings of an operator give the same token kind:
 *
 *	*  ×  ·  ⋅        STAR          /  ÷  ∕     SLASH
 *	-  −             MINUS         √           SQRT
 *	x² x⁻¹ x¹⁰      NAME then SUPERSCRIPT (a run of superscript digits)
 *
 * Names are words: letters (also Greek and other scripts), digits and '_',
 * not starting with a digit. Numbers are digits with an optional fraction
 * (".5" and "3.14" but not "5."). White space separates tokens and is skipped.
 * Anything else becomes an ML_TOK_ERROR token and lexing goes on, so one
 * pass reports every bad character. The list always ends with ML_TOK_END.
 */

typedef enum MlTokenKind
{
	ML_TOK_END = 0, /* end of the text, always the last token */
	ML_TOK_NUMBER,
	ML_TOK_NAME,
	ML_TOK_PLUS,
	ML_TOK_MINUS,
	ML_TOK_STAR,
	ML_TOK_SLASH,
	ML_TOK_CARET,
	ML_TOK_BANG,
	ML_TOK_LPAREN,
	ML_TOK_RPAREN,
	ML_TOK_COMMA,
	ML_TOK_EQUALS,
	ML_TOK_SQRT,
	ML_TOK_SUPERSCRIPT,
	ML_TOK_ERROR, /* a character that is not part of the syntax */
	ML_TOK_COUNT
} MlTokenKind;

typedef struct MlToken
{
	MlTokenKind kind;
	uint64_t    start; /* byte offset in the source */
	uint64_t    len;   /* bytes */
} MlToken;

ML_VEC_DEFINE (MlTokenVec, MlToken)

typedef struct MlLexResult
{
	MlResult   result;
	MlTokenVec tokens; /* owned by the caller, free with MlTokenVec_free */
} MlLexResult;

MlLexResult ml_lex (MlSlice source);

/* "number", "name", "+" ... for messages and tests. */
const char *ml_token_kind_name (MlTokenKind kind);

/* The source text of a token. */
MlSlice ml_token_text (MlSlice source, MlToken token);

/*
 * Appends the ASCII digits of a superscript token: "²" gives "2", "⁻¹⁰"
 * gives "-10". ML_ERR_PARSE when the text is not a superscript run.
 */
MlResult ml_superscript_to_ascii (MlStr *out, MlSlice text);

#endif /* MATLOG_PARSE_LEXER_H */
