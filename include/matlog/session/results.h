#ifndef MATLOG_SESSION_RESULTS_H
#define MATLOG_SESSION_RESULTS_H

#include <matlog/math/env.h>
#include <matlog/session/session.h>

/*
 * The results of a session: one entry per line, kept next to the session and
 * never stored in the file. They are computed when a session is opened and
 * again, from the changed line downward, after each edit.
 *
 * Math lines of the algebra domain are evaluated in order in one environment,
 * so what a line defines flows down to the lines below it. A line gives
 *
 *	ML_RESULT_VALUE   the text of a number (`7/2`, `≈ 1.41421356`) or of a
 *	                  matrix (`[[1, 2], [3, 4]]`); a definition of a variable
 *	                  gives the value it was given. A matrix also has a block:
 *	                  its typeset rows (see ml_mat_typeset), separated by
 *	                  newlines, for display under the line
 *	ML_RESULT_ERROR   the message of an error and the byte range in the line
 *	ML_RESULT_NONE    nothing: notes, blanks, unknown lines, lines of other
 *	                  domains, function definitions
 *
 * An include line runs the included file in the same environment, as if its
 * lines stood at that place; the lines of the file get no result of their own
 * here. The path is relative to the directory of the file that includes it.
 * An include that cannot be read, that includes a file that is already being
 * included (a cycle), or that nests more than ML_RESULTS_MAX_INCLUDES files
 * deep is an error on the include line. Nothing recurses: the files being
 * included are kept in a stack on the heap.
 *
 * Every entry remembers the snapshot of the environment before its line, so
 * the line can be evaluated again in the environment it saw (ghost mode, and
 * the steps later).
 */

#define ML_RESULTS_MAX_INCLUDES 16

typedef enum MlResultKind
{
	ML_RESULT_NONE = 0,
	ML_RESULT_VALUE,
	ML_RESULT_ERROR
} MlResultKind;

typedef struct MlLineResult
{
	MlResultKind kind;
	MlStr        text;      /* the value or the message */
	MlStr        block;     /* typeset rows of a matrix, else empty */
	const char   *code;     /* ML_RESULT_ERROR: ml_error_code, "parse_..." */
	MlValueKind  value;     /* ML_RESULT_VALUE: exact, approx or matrix */
	uint64_t     err_start; /* ML_RESULT_ERROR: byte range in the line */
	uint64_t     err_len;
	uint64_t     snapshot;  /* the environment before the line */
} MlLineResult;

ML_VEC_DEFINE (MlLineResultVec, MlLineResult)

typedef struct MlResultView
{
	MlResultKind kind; /* ML_RESULT_NONE for a line that does not exist */
	MlSlice      text;
	MlSlice      block;
	MlSlice      code;      /* the machine readable error name, else empty */
	MlValueKind  value;     /* the kind of a value: exact, approx or matrix */
	uint64_t     err_start;
	uint64_t     err_len;
	uint64_t     snapshot;
} MlResultView;

typedef struct MlResults
{
	MlEnv           env;
	MlLineResultVec lines;
	int32_t         free_symbols; /* see MlEnv; kept when the env restarts */
} MlResults;

void ml_results_init (MlResults *r);
void ml_results_free (MlResults *r);

/*
 * Recomputes the results of the lines from `from` to the end of the session
 * (the results above are kept; from is clamped to what is known, so 0 does
 * it all). The result of every line below is replaced, and the list gets
 * exactly one entry per line. ML_ERR_NOMEM is the only failure.
 */
MlResult ml_results_update (MlResults *r, const MlSession *s, uint64_t from);

/* The result of line idx, valid until the next update. */
MlResultView ml_results_get (const MlResults *r, uint64_t idx);

#endif /* MATLOG_SESSION_RESULTS_H */
