#ifndef MATLOG_SESSION_WATCH_H
#define MATLOG_SESSION_WATCH_H

#include <matlog/session/results.h>

/*
 * The watch window (plan section 4.21): every name that is defined at a line
 * of the session with its type and its value. The results keep the
 * environment before each line (a snapshot), so the names at any line are
 * known without running anything again.
 *
 * The rows are the names that are visible after the line has run: the
 * variables and functions of the algebra domain, and the definitions of the
 * boolean and the lambda domain. A name that was defined more than once has
 * one row, its newest value. Rows are in the order of their definition, and
 * `fresh` marks the ones the line itself defined or changed.
 *
 *	type    exact  approx  expr  quantity  vector 3  matrix 2x3
 *	        f(x, y) for a function, boolean and lambda for their definitions
 *	value   the text of the value as a result shows it, the definition of a
 *	        function (`f(x) = x^2`)
 */

typedef struct MlWatchRow
{
	MlStr   name;
	MlStr   type;
	MlStr   value;
	int32_t fresh;
} MlWatchRow;

ML_VEC_DEFINE (MlWatchRowVec, MlWatchRow)

void ml_watch_rows_free (MlWatchRowVec *rows);

/*
 * Appends the rows for line `line` (a line number at or past the number of
 * lines means the end of the session, where the line being typed would be).
 * ML_ERR_NOMEM is the only failure.
 */
MlResult ml_watch_rows (
	MlWatchRowVec   *out,
	const MlResults *res,
	uint64_t        line
);

#endif /* MATLOG_SESSION_WATCH_H */
