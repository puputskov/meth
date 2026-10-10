#ifndef METH_LOGIC_LAMBDA_H
#define METH_LOGIC_LAMBDA_H

#include <meth/logic/logic.h>

/*
 * The lambda calculus domain. A line is a term, a definition
 * `name = term`, or `trace term`.
 *
 *	x            a name (letters, digits, _ and '; a name of digits is a number)
 *	λx. t        an abstraction: also \x. t, and λx y z. t for λx. λy. λz. t;
 *	             the body goes as far to the right as it can
 *	t u          an application, which binds tighter than λ and is left
 *	             associative: f x y is (f x) y
 *	3            the Church numeral λf x. f (f (f x))  (at most 200)
 *
 * The result is the normal form, found by normal order reduction (the
 * leftmost outermost redex first; it finds a normal form whenever there is
 * one), written with the names of the term and the least number of
 * parentheses; a binder is renamed (x') when its name would capture a free
 * one. After ML_LAMBDA_MAX_STEPS steps the line is an error: the term may
 * have no normal form. `trace` also gives the terms of the reduction as the
 * block of the line, one beta step each (the first ML_LAMBDA_MAX_TRACE).
 *
 * A definition replaces the free names of later lines that it names, unless
 * a λ binds them, and keeps its value as a term, not as its normal form, so
 * `omega = (λx. x x) (λx. x x)` can be defined (its line is the error). Its
 * free names stay free: they are not replaced by later definitions.
 *
 * The errors are lambda_unexpected_character, lambda_expected_term,
 * lambda_expected_parameter, lambda_unbalanced_parenthesis,
 * lambda_number_too_big, lambda_no_normal_form, lambda_too_big and
 * lambda_bad_definition.
 */

#define ML_LAMBDA_MAX_STEPS 5000
#define ML_LAMBDA_MAX_TRACE 60
#define ML_LAMBDA_MAX_NUMBER 200

MlResult ml_lambda_run (
	const MlLogicDefVec *defs,
	uint64_t            count,
	MlSlice             source,
	MlLogicOut          *out
);

#endif /* METH_LOGIC_LAMBDA_H */
