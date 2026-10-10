#ifndef METH_MATH_STEPLOG_H
#define METH_MATH_STEPLOG_H

#include <meth/core/err.h>
#include <meth/core/slice.h>
#include <meth/core/str.h>

/*
 * A log of the steps of a calculation, as text lines. The functions that can
 * explain themselves take a pointer to a log: NULL means steps are not wanted
 * and nothing is recorded (and nothing is paid for), so a normal evaluation is
 * as fast as before. Lines are appended with a newline; a failure to allocate
 * is kept in status and ends the recording, the calculation itself goes on.
 */

typedef struct MlStepLog
{
	MlStr    text;
	MlResult status;
} MlStepLog;

void ml_steplog_init (MlStepLog *log);
void ml_steplog_free (MlStepLog *log);

/* Appends line and a newline. A NULL log or a failed one ignores it. */
void ml_steplog_line (MlStepLog *log, MlSlice line);

/*
 * Lines may hold a tab: the text in front of it is the step and what follows
 * is a note on it (the name of the rule). This replaces each tab by the spaces
 * that put all the notes of the log in one column, three spaces behind the
 * longest step. Call it once, when the log is complete.
 */
void ml_steplog_align (MlStepLog *log);

/* Removes the newline at the end, if any. */
void ml_steplog_trim (MlStepLog *log);

#endif /* METH_MATH_STEPLOG_H */
