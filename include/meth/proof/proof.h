#ifndef METH_PROOF_PROOF_H
#define METH_PROOF_PROOF_H

#include <meth/core/err.h>
#include <meth/core/slice.h>
#include <meth/core/str.h>
#include <meth/core/vec.h>

/*
 * The proof checker (plan section 4.12). A proof is text, one statement per
 * line:
 *
 *	theorem double: x + x = 2x
 *	given x > 0
 *	x + x = 2x  by simplify
 *	qed
 *
 * A step is an equation and a justification after "by". A line that starts
 * with "=" or "..." continues the right hand side of the step above:
 *
 *	theorem square: (x + 1)^2 = x^2 + 2x + 1
 *	(x + 1)^2 = (x + 1)(x + 1)  by simplify
 *	= x^2 + 2x + 1              by expand
 *	qed
 *
 * The justifications the checker decides:
 *
 *	simplify      both sides have the same simplified form
 *	expand        both sides have the same expanded form
 *	hypothesis    the equation is one of the "given" equations
 *	step N        the equation is step N above (counted from 1)
 *	lemma NAME    the equation is a proved theorem above
 *	sorry         a placeholder: reported, never counted as proved
 *
 * Every step is checked, never taken on trust. valid means the engine is
 * sure; invalid means it found values for which the equation is false (or a
 * justification that does not say it); unchecked is everything it cannot
 * decide, so it never says valid when it is not sure. "qed" closes the
 * theorem when the steps lead from the left side of the claim to its right
 * side and every step is valid.
 */

typedef enum MlProofMark
{
	ML_PROOF_NONE = 0, /* a blank line, a comment, a line with no verdict */
	ML_PROOF_VALID,
	ML_PROOF_INVALID,
	ML_PROOF_UNCHECKED,
	ML_PROOF_SORRY,
	ML_PROOF_SYNTAX,
	ML_PROOF_MARK_COUNT
} MlProofMark;

typedef struct MlProofLine
{
	MlProofMark mark;
	MlStr       note; /* why, in the language of the texts; empty if valid */
} MlProofLine;

ML_VEC_DEFINE (MlProofLineVec, MlProofLine)

typedef struct MlProofReport
{
	MlProofLineVec lines;    /* one per line of the source */
	uint64_t       theorems; /* how many "theorem" lines */
	uint64_t       proved;   /* how many of them were closed by "qed" */
} MlProofReport;

void ml_proof_report_init (MlProofReport *rep);
void ml_proof_report_free (MlProofReport *rep);

/* Checks the whole text; ML_ERR_NOMEM is the only failure. */
MlResult ml_proof_check (MlProofReport *rep, MlSlice source);

#endif /* METH_PROOF_PROOF_H */
