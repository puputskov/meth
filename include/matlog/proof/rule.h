#ifndef MATLOG_PROOF_RULE_H
#define MATLOG_PROOF_RULE_H

#include <matlog/sym/expr.h>

/*
 * The named rewrite rules of a proof ("by rule distributive"). A rule is an
 * identity with symbols that stand for any formula, written in the section
 * [rules] of the texts:
 *
 *	distributive = a * (b + c) = a * b + a * c
 *
 * A step is justified by a rule when its two sides are the same formula
 * except for one place, and at that place one side is the rule read one way
 * and the other side the rule read the other way, for the same formulas
 * a, b, c. Where the sides differ is found by walking both trees together; the
 * left side of the rule has to match the formula there as it was written (the
 * tree before simplifying), the other side has to be equal after expanding.
 * What does not fit is never taken as valid.
 */

typedef enum MlProofRuleResult
{
	ML_PROOF_RULE_UNKNOWN = 0, /* there is no rule of that name */
	ML_PROOF_RULE_FITS,
	ML_PROOF_RULE_NO_FIT
} MlProofRuleResult;

/*
 * Whether the step left = right, given as the trees as written, is an
 * instance of the rule called name. ctx is used for the work (and left with
 * its failure state cleared).
 */
MlProofRuleResult ml_proof_rule_check (
	MlSymCtx    *ctx,
	MlSlice     name,
	const MlSym *left,
	const MlSym *right
);

#endif /* MATLOG_PROOF_RULE_H */
