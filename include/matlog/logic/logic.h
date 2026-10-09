#ifndef MATLOG_LOGIC_LOGIC_H
#define MATLOG_LOGIC_LOGIC_H

#include <matlog/core/str.h>
#include <matlog/core/vec.h>

/*
 * What the boolean and the lambda domain share. Each of them runs a line on
 * its own and gives an MlLogicOut; the names a line defines are kept by the
 * caller in a list of definitions (see session/results.h), in order, and a
 * line sees the first `count` of them, so the lines below a changed line can
 * be run again from the state before it.
 *
 * A definition is stored fully expanded: its text mentions no defined name,
 * only free ones, so looking a name up never needs another look up.
 */

typedef struct MlLogicDef
{
	int32_t domain; /* an MlDomain */
	MlStr   name;
	MlStr   text;
} MlLogicDef;

ML_VEC_DEFINE (MlLogicDefVec, MlLogicDef)

void ml_logic_defs_truncate (MlLogicDefVec *defs, uint64_t count);
void ml_logic_defs_free (MlLogicDefVec *defs);

/* Adds a definition of a name (copies of both texts). */
MlResult ml_logic_defs_add (
	MlLogicDefVec *defs,
	int32_t       domain,
	MlSlice       name,
	MlSlice       text
);

typedef struct MlLogicFound
{
	int32_t found;
	MlSlice text;
} MlLogicFound;

/* The latest definition of name among the first `count`, in a domain. */
MlLogicFound ml_logic_find (
	const MlLogicDefVec *defs,
	uint64_t            count,
	int32_t             domain,
	MlSlice             name
);

/* What can be wrong with a line of the boolean or the lambda domain. */
typedef enum MlLogicError
{
	ML_LOGIC_OK = 0,
	ML_LOGIC_BOOL_UNEXPECTED_CHARACTER,
	ML_LOGIC_BOOL_EXPECTED_OPERAND,
	ML_LOGIC_BOOL_EXPECTED_OPERATOR,
	ML_LOGIC_BOOL_UNBALANCED_PARENTHESIS,
	ML_LOGIC_BOOL_TOO_MANY_NAMES,
	ML_LOGIC_BOOL_TABLE_TOO_BIG,
	ML_LOGIC_BOOL_BAD_DEFINITION,
	ML_LOGIC_LAMBDA_UNEXPECTED_CHARACTER,
	ML_LOGIC_LAMBDA_EXPECTED_TERM,
	ML_LOGIC_LAMBDA_EXPECTED_PARAMETER,
	ML_LOGIC_LAMBDA_UNBALANCED_PARENTHESIS,
	ML_LOGIC_LAMBDA_NUMBER_TOO_BIG,
	ML_LOGIC_LAMBDA_NO_NORMAL_FORM,
	ML_LOGIC_LAMBDA_TOO_BIG,
	ML_LOGIC_LAMBDA_BAD_DEFINITION,
	ML_LOGIC_COUNT
} MlLogicError;

/* The machine readable name (`bool_expected_operand`) and the message. */
const char *ml_logic_error_code (MlLogicError e);
const char *ml_logic_error_text (MlLogicError e);

typedef struct MlLogicOut
{
	int32_t      value;     /* 1 when the line has a result (else empty line) */
	MlLogicError error;     /* not ML_LOGIC_OK: text is the message of it */
	uint64_t    err_start; /* the byte range of the error in the line */
	uint64_t    err_len;
	MlStr       text;      /* the result, or the message */
	MlStr       block;     /* rows to show under the line, may be empty */
	MlStr       def_name;  /* the name the line defines, empty if none */
	MlStr       def_text;  /* its expanded text */
} MlLogicOut;

void ml_logic_out_init (MlLogicOut *o);
void ml_logic_out_free (MlLogicOut *o);

/* Makes out an error, with its message; start and len are in the line. */
MlResult ml_logic_out_error (
	MlLogicOut   *o,
	MlLogicError error,
	uint64_t     start,
	uint64_t     len
);

#endif /* MATLOG_LOGIC_LOGIC_H */
