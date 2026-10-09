#ifndef MATLOG_MATH_STEPLOG_H
#define MATLOG_MATH_STEPLOG_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>

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

/* Removes the newline at the end, if any. */
void ml_steplog_trim (MlStepLog *log);

#endif /* MATLOG_MATH_STEPLOG_H */
