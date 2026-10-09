#ifndef MATLOG_UI_GHOST_H
#define MATLOG_UI_GHOST_H

#include <matlog/session/results.h>

/*
 * Ghost mode: the preview of the result of the line being typed. The text is
 * parsed and evaluated in the environment as it is at that line (the snapshot
 * the results keep for it, or the whole environment for a line that comes
 * after all others), and the value is given as text, like the result of a
 * line ("7/2", "≈ 1.41421356").
 *
 * Nothing is shown (shown is 0 and out is untouched) unless the text is a
 * whole valid statement that evaluates without error and has a value: `2+`,
 * `(3`, `1/0`, an unknown name, an empty line and a function definition all
 * show nothing. Nothing is bound: the environment is the same afterwards, so
 * `a = 3` previews 3 and does not define a.
 */

typedef struct MlGhost
{
	MlResult result; /* ML_ERR_NOMEM is the only failure */
	int32_t  shown;  /* 1 when out got the value */
} MlGhost;

/*
 * The preview of text typed as line idx of the session (idx may be the number
 * of lines, for a new line at the end) in a domain. The value is appended to
 * out. The boolean and the lambda domain run the line with the definitions
 * made above it, and show the result text when the line has a value without
 * an error; their definitions are not kept.
 */
MlGhost ml_ghost (
	MlStr           *out,
	const MlResults *results,
	MlDomain        domain,
	uint64_t        idx,
	MlSlice         text
);

#endif /* MATLOG_UI_GHOST_H */
