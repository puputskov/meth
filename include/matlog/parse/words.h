#ifndef MATLOG_PARSE_WORDS_H
#define MATLOG_PARSE_WORDS_H

#include <matlog/core/slice.h>

/*
 * The words of the syntax, written once. A word is a name that means
 * something by itself: a keyword, an operator written as a word, or a
 * constant. The parser, the evaluator, the highlighter, the completion and the
 * symbolic engine ask this table and never compare a name with "pi" or "and"
 * themselves, so a new constant or another spelling is one row here.
 *
 * Every word has an id, and a word can be spelled in more than one way (pi and
 * the Greek letter): the spellings are rows with the same id, the first one is
 * the ASCII one. A variable or a function of the user still hides a word where
 * the language says so; the table only says what the words are.
 */

typedef enum MlWordKind
{
	ML_WORD_NONE = 0,
	ML_WORD_KEYWORD,  /* include, lambda, let */
	ML_WORD_OPERATOR, /* and, or, to: operators spelled as words */
	ML_WORD_CONSTANT, /* pi, e, true, false: names with a value */
	ML_WORD_SYMBOL    /* inf: a name the symbolic functions read */
} MlWordKind;

typedef enum MlWordId
{
	ML_WORD_ID_NONE = 0,
	ML_WORD_ID_INCLUDE,
	ML_WORD_ID_LAMBDA,
	ML_WORD_ID_LET,
	ML_WORD_ID_AND,
	ML_WORD_ID_OR,
	ML_WORD_ID_TO,
	ML_WORD_ID_PI,
	ML_WORD_ID_E,
	ML_WORD_ID_TRUE,
	ML_WORD_ID_FALSE,
	ML_WORD_ID_INF
} MlWordId;

typedef struct MlWord
{
	MlWordKind kind;
	MlWordId   id;
} MlWord;

/* The word with this spelling, kind ML_WORD_NONE for any other name. */
MlWord ml_word_find (MlSlice name);

/* 1 when name is a spelling of the word id. */
int32_t ml_word_is (MlSlice name, MlWordId id);

/* The ASCII spelling of a word, an empty slice for ML_WORD_ID_NONE. */
MlSlice ml_word_name (MlWordId id);

/* The spellings, in table order (the ASCII one of a word first). */
uint64_t ml_word_count (void);
MlSlice  ml_word_spelling (uint64_t index);
MlWord   ml_word_at (uint64_t index);

/* 1 when the completion offers the spelling (index) of the word. */
int32_t ml_word_listed (uint64_t index);

#endif /* MATLOG_PARSE_WORDS_H */
