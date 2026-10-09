#ifndef MATLOG_SESSION_STEPS_H
#define MATLOG_SESSION_STEPS_H

#include <matlog/session/results.h>

/*
 * The steps of a line, regenerated on request. Nothing about them is stored:
 * the line is parsed and evaluated again, in the environment as it was when
 * the line ran (the snapshot the results keep), with a step log switched on.
 *
 * Lines that have steps (the statement may be an assignment, `B = ...`):
 *
 *	det (A)        the elimination and the product of the pivots
 *	inv (A)        row reduction of A with the identity beside it
 *	A^-1           the same
 *	rref (A)       the row operations of the reduction
 *	rank (A)       the same
 *	solve (A, b)   row reduction of A with b beside it
 *	A * B          the sum of every cell of the product (up to 36 cells)
 *
 * where A and B are any expressions that evaluate to matrices. A line of the
 * lambda domain has the terms of its reduction, one beta step per row (the
 * rows `trace` gives), and a line of the boolean domain has its truth table
 * (what `table` gives; up to ML_BOOL_MAX_TABLE names). Everything else has no
 * steps yet.
 */

typedef struct MlLineSteps
{
	MlResult result; /* ML_ERR_NOMEM is the only failure */
	int32_t  found;  /* 1 when the line has steps and out got them */
} MlLineSteps;

/*
 * Appends the steps of line idx to out, as text rows separated by newlines
 * (typeset matrices included, no newline at the end).
 */
MlLineSteps ml_line_steps (
	MlStr           *out,
	const MlResults *results,
	const MlSession *s,
	uint64_t        idx
);

#endif /* MATLOG_SESSION_STEPS_H */
