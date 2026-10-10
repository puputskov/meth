#ifndef METH_UI_HIGHLIGHT_H
#define METH_UI_HIGHLIGHT_H

#include <meth/core/vec.h>
#include <meth/math/env.h>
#include <meth/session/session.h>
#include <meth/ui/theme.h>

/*
 * Syntax highlighting of a math line (plan section 4.10): a pure function from
 * the text, the environment as one line saw it and an optional error range to
 * the runs of one class each. The classes come from the lexer, so they follow
 * the same rules as the parser, also for the unicode spellings; text that does
 * not parse is highlighted token by token.
 *
 * The runs are in order, do not overlap and cover the whole text, white space
 * included (class ML_CLASS_TEXT); neighbours of the same class are one run.
 *
 *	number    3  1.5  .5
 *	name      a variable, or a name that stands for nothing
 *	function  a built in function, a function of the environment, and a name
 *	          that is followed by '(' and is not a variable
 *	constant  pi  π  e
 *	unit      a unit name that is not a variable (m, km, kg, ...)
 *	operator  + - * / ^ ! = √ and superscript powers
 *	paren     ( ) [ ] ,
 *	keyword   include  lambda
 *
 * The boolean and the lambda domain have their own scanners, with the rules
 * of their engines (logic/bool.h, logic/lambda.h), and two more classes:
 *
 *	logic     and or not xor and their symbols (& | ~ ! ^ -> <-> ∧ ∨ ¬ ⊕ → ↔)
 *	binder    the λ (or \), the names after it up to the dot, and the dot
 *	constant  true false 0 1 ⊤ ⊥ (boolean)
 *	number    a Church numeral (lambda)
 *	keyword   table (boolean) and trace (lambda), at the start of the line
 *	name      any other name; paren ( ), operator =
 *
 *	error     a character that is not part of the syntax, and the byte range
 *	          of an error (err_len 0: none)
 */

typedef struct MlRun
{
	uint64_t start; /* byte offset */
	uint64_t len;
	MlClass  cls;
} MlRun;

ML_VEC_DEFINE (MlRunVec, MlRun)

/*
 * Appends the runs of text to out (which the caller clears first when it
 * wants only these). env may be NULL: then no name is told apart as a
 * variable or a function of the environment.
 */
MlResult ml_highlight (
	MlRunVec    *out,
	MlDomain    domain,
	MlSlice     text,
	const MlEnv *env,
	uint64_t    snapshot,
	uint64_t    err_start,
	uint64_t    err_len
);

/* The class of the byte at offset pos in runs (ML_CLASS_TEXT past the end). */
MlClass ml_runs_class_at (const MlRunVec *runs, uint64_t pos);

#endif /* METH_UI_HIGHLIGHT_H */
