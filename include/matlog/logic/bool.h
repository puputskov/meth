#ifndef MATLOG_LOGIC_BOOL_H
#define MATLOG_LOGIC_BOOL_H

#include <matlog/logic/logic.h>

/*
 * The boolean domain. A line is an expression over names and the constants
 * true and false, or a definition `name = expression`, or `table expression`.
 *
 *	not    ~ ! ¬             prefix, binds tightest
 *	and    & && ∧ and        left
 *	xor    ^ ⊕ xor           left
 *	or     | || ∨ or        left
 *	imply  -> => →           right
 *	equal  <-> <=> == ↔      left, loosest
 *
 * Constants are true, false, 1, 0, ⊤ and ⊥. The result is `true` or `false`
 * when the value does not depend on any name, else the expression in
 * minimal sum of products form (Quine-McCluskey, with the essential prime
 * implicants and then a greedy cover) written with ∧ ∨ ¬, or the expression
 * as it was written when that is shorter. A defined name is replaced by its
 * definition, once, when a line is read. `table` also gives the truth table
 * as a block. At most ML_BOOL_MAX_VARS names may occur in a line; a table
 * needs at most ML_BOOL_MAX_TABLE.
 *
 * The errors are bool_unexpected_character, bool_expected_operand,
 * bool_expected_operator, bool_unbalanced_parenthesis, bool_too_many_names,
 * bool_bad_definition.
 */

#define ML_BOOL_MAX_VARS  10
#define ML_BOOL_MAX_TABLE 6

/* Runs one line; ML_ERR_NOMEM is the only failure. */
MlResult ml_bool_run (
	const MlLogicDefVec *defs,
	uint64_t            count,
	MlSlice             source,
	MlLogicOut          *out
);

#endif /* MATLOG_LOGIC_BOOL_H */
