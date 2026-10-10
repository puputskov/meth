#ifndef MATLOG_EXPORT_MATH_H
#define MATLOG_EXPORT_MATH_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>

/*
 * Typeset math for the exporters (plan section 4.11). A statement of the
 * algebra syntax is parsed and written from its tree, not from its text, so
 * a/b is a real fraction, √x a real root, x² a real power and
 * [[1,2],[3,4]] a real matrix:
 *
 *	ML_MATH_LATEX   the code for an amsmath formula: \frac{a}{b}, \sqrt{x},
 *	                {x}^{2}, \begin{pmatrix} 1 & 2 \\ 3 & 4 \end{pmatrix}
 *	ML_MATH_MATHML  presentation MathML elements: <mfrac>, <msqrt>, <msup>,
 *	                <mtable>; the caller puts them inside <math>
 *
 * Greek names (alpha, pi, π ...) become their symbols, names of more than one
 * letter are set upright (units, functions), x_1 has a subscript. Brackets
 * are put where the precedence needs them and nowhere else. The walk uses a
 * stack on the heap, so the depth of a formula does not matter.
 */

typedef enum MlMathNotation
{
	ML_MATH_LATEX = 0,
	ML_MATH_MATHML
} MlMathNotation;

/*
 * Appends the typeset form of source. ML_ERR_PARSE when it is not a valid
 * statement (out is left as it was).
 */
MlResult ml_math_typeset (MlStr *out, MlMathNotation n, MlSlice source);

/*
 * The same for a line of the boolean domain (lambda 0) or of the lambda
 * domain (lambda 1), written from its text: ∧ ∨ ¬ ⊕ → ↔ for the operators in
 * any spelling (&, and, ...), ⊤ and ⊥ for the constants, λ and a dot for a
 * binder, a thin space between the parts of an application. Text that is not
 * the syntax is set as it is. ML_ERR_NOMEM is the only failure.
 */
MlResult ml_logic_typeset (
	MlStr          *out,
	MlMathNotation n,
	int32_t        lambda,
	MlSlice        source
);

/* The symbol between a line and its result: "=", or "≈" when approx. */
MlResult ml_math_relation (MlStr *out, MlMathNotation n, int32_t approx);

/* Appends text, the characters that mean something in the notation escaped. */
MlResult ml_math_escape (MlStr *out, MlMathNotation n, MlSlice text);

#endif /* MATLOG_EXPORT_MATH_H */
