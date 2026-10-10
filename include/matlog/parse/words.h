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
	ML_WORD_SYMBOL,   /* inf, set: names the symbolic engine reads or makes */
	ML_WORD_FUNCTION, /* sin, ln, det ...: functions the engines know by name */
	ML_WORD_SYMBOLIC, /* simplify, diff ...: functions of the symbolic engine */
	ML_WORD_COMMAND   /* table, trace: the commands of the logic domains */
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
	ML_WORD_ID_INF,
	ML_WORD_ID_SET,
	ML_WORD_ID_SIN,
	ML_WORD_ID_COS,
	ML_WORD_ID_TAN,
	ML_WORD_ID_ASIN,
	ML_WORD_ID_ACOS,
	ML_WORD_ID_ATAN,
	ML_WORD_ID_LN,
	ML_WORD_ID_LOG,
	ML_WORD_ID_EXP,
	ML_WORD_ID_ABS,
	ML_WORD_ID_SQRT,
	ML_WORD_ID_DET,
	ML_WORD_ID_INV,
	ML_WORD_ID_RREF,
	ML_WORD_ID_RANK,
	ML_WORD_ID_SOLVE,
	ML_WORD_ID_SIMPLIFY,
	ML_WORD_ID_EXPAND,
	ML_WORD_ID_SUBS,
	ML_WORD_ID_DIFF,
	ML_WORD_ID_INTEGRATE,
	ML_WORD_ID_LIMIT,
	ML_WORD_ID_TABLE,
	ML_WORD_ID_TRACE
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

/* The same with the short name of the word: ML_WORD_IS (name, SIN). */
#define ML_WORD_IS(name, w)  ml_word_is ((name), ML_WORD_ID_##w)
#define ML_WORD_NAME(w)      ml_word_name (ML_WORD_ID_##w)

/* The spellings, in table order (the ASCII one of a word first). */
uint64_t ml_word_count (void);
MlSlice  ml_word_spelling (uint64_t index);
MlWord   ml_word_at (uint64_t index);

/* How many spellings have the kind, and the n-th of them in table order. */
uint64_t ml_word_kind_count (MlWordKind kind);
MlSlice  ml_word_kind_at (MlWordKind kind, uint64_t n);

/* 1 when the completion offers the spelling (index) of the word. */
int32_t ml_word_listed (uint64_t index);

#endif /* MATLOG_PARSE_WORDS_H */
