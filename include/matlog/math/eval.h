#ifndef MATLOG_MATH_EVAL_H
#define MATLOG_MATH_EVAL_H

#include <matlog/math/env.h>

/*
 * The evaluator of the algebra syntax: walks a parsed statement and gives the
 * value. Evaluation has no side effects: it reads the environment (up to a
 * snapshot, see env.h) and allocates its numbers from the arena it is given,
 * and nothing else changes. Binding the result of a definition is the job of
 * the caller, who can use ml_eval_commit for that.
 *
 *	expression       its value
 *	name = expr      the value of expr (the caller binds it)
 *	f(x) = expr      ML_VALUE_NONE (the caller binds the function)
 *	empty line       ML_VALUE_NONE
 *
 * Symbolic calls: simplify (f), expand (f), subs (f, x, v), diff (f, x [, n]),
 * integrate (f, x [, a, b]) read their arguments as formulas, in which a name
 * is a symbol (names with a number as value are replaced by it, except the
 * symbol that is worked on). The value is an expression, or a number when
 * no symbol is left. Elsewhere an unknown name is still an error.
 *
 * Names are looked up, newest first, among the parameters of the function that
 * is running, the bindings of the environment, and the built in constants
 * (pi and e, which are approximations). A call is looked up the same way, in
 * the order parameters, environment, built in functions: f(2) calls a user
 * function, but if f is a variable (or a constant, or a parameter) the call is
 * a product, so a(2) is a times 2. Functions see the environment of the line
 * that calls them (late binding), so a function may use a variable that is
 * defined after it, and recursion works because a function sees itself.
 *
 * Built in functions: sqrt abs sin cos tan asin acos atan ln log (base 10) exp
 * floor ceil round (halves away from zero) min max (two or more arguments)
 * gcd lcm (two integers). Results are exact where they can be (sqrt (4), abs,
 * floor, gcd, sin (0), ln (1)...) and approximate otherwise.
 *
 * An error value carries the byte range of the text in the line. An error that
 * happens inside the body of a user function is reported at the call, since
 * the body is text of another line.
 *
 * The evaluator does not recurse: it walks the tree with explicit stacks on the
 * heap, so it needs no more C stack for a deep expression or a deep chain of
 * calls than for a short one. The stacks are limited to ML_EVAL_MAX_DEPTH
 * levels of nodes in total, counting the levels of every running function, so
 * recursion without end gives ML_EVAL_DEPTH instead of eating all memory.
 * Results of more than 100000 bits are refused (ML_EVAL_TOO_LARGE).
 */

#define ML_EVAL_MAX_DEPTH 20000

MlValue ml_eval (
	MlArena     *arena,
	const MlEnv *env,
	uint64_t    snapshot,
	MlSlice     source,
	const MlAst *ast
);

/*
 * Like ml_eval, and the symbolic calls in the statement (simplify, expand,
 * diff, integrate) explain themselves into the log, see steplog.h. The value
 * is the same as ml_eval gives.
 */
MlValue ml_eval_steps (
	MlArena     *arena,
	const MlEnv *env,
	uint64_t    snapshot,
	MlSlice     source,
	const MlAst *ast,
	MlStepLog   *log
);

/*
 * Keeps what a statement defines: a = expr binds the number a (nothing when
 * value is an error), f(x) = expr binds the function. Other statements do
 * nothing. The value is the one ml_eval gave for the same statement.
 */
MlResult ml_eval_commit (
	MlEnv       *env,
	MlSlice     source,
	const MlAst *ast,
	MlValue     value
);

#endif /* MATLOG_MATH_EVAL_H */
