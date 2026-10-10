#ifndef METH_SYM_EXPR_H
#define METH_SYM_EXPR_H

#include <meth/math/rational.h>
#include <meth/math/steplog.h>
#include <meth/parse/parser.h>

/*
 * Symbolic expressions: trees of exact numbers, symbols, sums, products,
 * powers and functions of one argument. A tree is immutable and lives in an
 * arena; its nodes are shared freely, so building a new tree from parts of an
 * old one copies nothing. Nothing recurses: every walk over a tree (building,
 * comparing, printing, mapping) uses an explicit stack on the heap.
 *
 *	NUM    an exact rational
 *	VAR    a symbol (x, y, pi, e)
 *	ADD    two or more terms
 *	MUL    two or more factors
 *	POW    base, exponent
 *	FUNC   name and one argument: sin cos tan asin acos atan ln exp abs
 *
 * Subtraction is a sum with a factor -1, division is a product with a power
 * -1, a square root is a power 1/2. This keeps the number of node kinds, and
 * so of rules, small.
 *
 * The "smart" constructors (ml_sym_add and the others, in simplify.c) build
 * the canonical form out of canonical parts: sums and products are flat and
 * sorted, numbers are folded, like terms and equal bases are collected, and
 * x + 0, x * 1, x * 0, x^1 and x^0 disappear. The plain constructors here
 * build exactly what they are given.
 *
 * A failure (no memory, too many nodes, a division by zero) is kept in the
 * context and makes the operations return a harmless node, so a calculation
 * is checked once at its end with ml_symctx_ok.
 */

/*
 * The rules a calculation names in its steps. The text for the screen, the
 * documentation and the translations comes from ml_rule_name; the engine
 * itself only passes the enum around.
 */
typedef enum MlRule
{
	ML_RULE_NONE = 0,
	/* simplify and expand */
	ML_RULE_SIMPLIFY,
	ML_RULE_ADD_NUMBERS,
	ML_RULE_COLLECT_TERMS,
	ML_RULE_MULTIPLY_NUMBERS,
	ML_RULE_MULTIPLY_OUT,
	ML_RULE_COMBINE_FACTORS,
	ML_RULE_EXPAND_POWER,
	ML_RULE_SIMPLIFY_POWER,
	ML_RULE_EVALUATE_FUNCTION,
	/* derivatives */
	ML_RULE_DIFF_CONSTANT,
	ML_RULE_DIFF_SUM,
	ML_RULE_DIFF_CONSTANT_MULTIPLE,
	ML_RULE_DIFF_PRODUCT,
	ML_RULE_DIFF_POWER,
	ML_RULE_DIFF_EXPONENTIAL,
	ML_RULE_DIFF_GENERAL_POWER,
	ML_RULE_DIFF_CHAIN,
	/* integrals */
	ML_RULE_INT_CONSTANT,
	ML_RULE_INT_RECIPROCAL,
	ML_RULE_INT_POWER,
	ML_RULE_INT_EXPONENTIAL,
	ML_RULE_INT_SINE,
	ML_RULE_INT_COSINE,
	ML_RULE_INT_TANGENT,
	ML_RULE_INT_LOGARITHM,
	ML_RULE_INT_ARCTANGENT,
	ML_RULE_INT_PARTIAL_FRACTIONS,
	ML_RULE_INT_SUBSTITUTION,
	ML_RULE_INT_PARTS,
	ML_RULE_INT_HALF_ANGLE,
	ML_RULE_COUNT
} MlRule;

/* The words for a rule ("power rule"); empty for ML_RULE_NONE. */
const char *ml_rule_name (MlRule rule);

typedef enum MlSymKind
{
	ML_SYM_NUM = 0,
	ML_SYM_VAR,
	ML_SYM_ADD,
	ML_SYM_MUL,
	ML_SYM_POW,
	ML_SYM_FUNC
} MlSymKind;

typedef struct MlSym MlSym;

struct MlSym
{
	MlSymKind          kind;
	MlRat              num;   /* NUM */
	MlSlice            name;  /* VAR, FUNC */
	const MlSym *const *kids;
	uint64_t           count; /* number of kids */
};

/* What the generic walker does at every node. */
typedef enum MlSymMode
{
	ML_SYM_REBUILD = 0, /* canonical form of the tree (simplify) */
	ML_SYM_EXPAND,      /* also multiply out products of sums and powers */
	ML_SYM_SUBSTITUTE,  /* replace a symbol by an expression, then rebuild */
	ML_SYM_REPLACE,     /* replace every copy of a subtree, then rebuild */
	ML_SYM_COPY         /* a new tree with every node and number of its own */
} MlSymMode;

#define ML_SYM_BUDGET 200000

typedef struct MlSymCtx
{
	MlArena      *arena;
	MlResult     status;
	uint64_t     budget;     /* nodes that may still be made */
	MlStepLog    *log;       /* NULL: steps are not recorded */
	MlSymMode    mode;
	const MlSym  *subst_var; /* ML_SYM_SUBSTITUTE */
	const MlSym  *subst_val;
	/* ml_sym_from_ast: the byte range of the text that is not a formula */
	uint64_t     fail_start;
	uint64_t     fail_len;
	/* ml_sym_from_ast keeps the tree as written, to show how it simplifies */
	int32_t      raw;
} MlSymCtx;

void ml_symctx_init (MlSymCtx *ctx, MlArena *arena);
int32_t ml_symctx_ok (const MlSymCtx *ctx);

/* ---- plain constructors ---- */

const MlSym *ml_sym_num (MlSymCtx *ctx, MlRat value);
const MlSym *ml_sym_int (MlSymCtx *ctx, int64_t value);
const MlSym *ml_sym_var (MlSymCtx *ctx, MlSlice name);

/* A node over kids (copied), exactly as given. FUNC nodes need a name. */
const MlSym *ml_sym_raw (
	MlSymCtx           *ctx,
	MlSymKind          kind,
	const MlSym *const *kids,
	uint64_t           count
);

const MlSym *ml_sym_func (MlSymCtx *ctx, MlSlice name, const MlSym *arg);

/*
 * A function node over any number of kids, kept as given. The only one the
 * engine makes is the set of solutions, "set" (see ml_sym_set).
 */
const MlSym *ml_sym_func_n (
	MlSymCtx           *ctx,
	MlSlice            name,
	const MlSym *const *kids,
	uint64_t           count
);

/* The set {a, b ...} of items, without repeats, in increasing order. */
const MlSym *ml_sym_set (MlSymCtx *ctx, const MlSym *const *items, uint64_t n);

int32_t ml_sym_is_set (const MlSym *s);

/* ---- inspection ---- */

int32_t ml_sym_is_num (const MlSym *s);
int32_t ml_sym_is_zero (const MlSym *s);
int32_t ml_sym_is_one (const MlSym *s);
int32_t ml_sym_is_var (const MlSym *s, MlSlice name);

/* 1 and the value when s is a whole number that fits an int64_t. */
int32_t ml_sym_to_i64 (const MlSym *s, int64_t *value);

/* -1, 0 or 1: a total order on trees; equal trees compare as 0. */
int32_t ml_sym_compare (MlSymCtx *ctx, const MlSym *a, const MlSym *b);
int32_t ml_sym_equal (MlSymCtx *ctx, const MlSym *a, const MlSym *b);

/* 1 when the symbol called name occurs anywhere in s. */
int32_t ml_sym_has_var (const MlSym *s, MlSlice name);

/* ---- canonical constructors (simplify.c) ---- */

const MlSym *ml_sym_add (MlSymCtx *ctx, const MlSym *a, const MlSym *b);
const MlSym *ml_sym_sub (MlSymCtx *ctx, const MlSym *a, const MlSym *b);
const MlSym *ml_sym_mul (MlSymCtx *ctx, const MlSym *a, const MlSym *b);
const MlSym *ml_sym_div (MlSymCtx *ctx, const MlSym *a, const MlSym *b);
const MlSym *ml_sym_neg (MlSymCtx *ctx, const MlSym *a);
const MlSym *ml_sym_pow (MlSymCtx *ctx, const MlSym *a, const MlSym *b);
const MlSym *ml_sym_call (MlSymCtx *ctx, MlSlice name, const MlSym *arg);

/* A sum or a product of n terms. */
const MlSym *ml_sym_sum (MlSymCtx *ctx, const MlSym *const *terms, uint64_t n);
const MlSym *ml_sym_product (
	MlSymCtx           *ctx,
	const MlSym *const *factors,
	uint64_t           n
);

/* ---- whole tree operations (simplify.c) ---- */

/* The canonical form of s. */
const MlSym *ml_sym_simplify (MlSymCtx *ctx, const MlSym *s);

/*
 * The same tree built again in the arena of ctx, leaves and numbers included.
 * simplify may hand back nodes it was given (a symbol, a number), so only a
 * copy can be kept after the arena of the original is reset.
 */
const MlSym *ml_sym_copy (MlSymCtx *ctx, const MlSym *s);

/* Multiplies out products of sums and integer powers of sums. */
const MlSym *ml_sym_expand (MlSymCtx *ctx, const MlSym *s);

/* s with every x replaced by value, then simplified. */
const MlSym *ml_sym_substitute (
	MlSymCtx    *ctx,
	const MlSym *s,
	const MlSym *x,
	const MlSym *value
);

/* s with every subtree equal to from replaced by value, then simplified. */
const MlSym *ml_sym_replace (
	MlSymCtx    *ctx,
	const MlSym *s,
	const MlSym *from,
	const MlSym *value
);

/*
 * The value of s as a double, when it has no symbols but pi and e and uses
 * only functions the tool knows. 0 when it has not.
 */
int32_t ml_sym_to_double (MlArena *arena, const MlSym *s, double *out);

/* ---- calculus (diff.c, integrate.c, limit.c) ---- */

/* The derivative of s by the symbol x, in canonical form. */
const MlSym *ml_sym_diff (MlSymCtx *ctx, const MlSym *s, const MlSym *x);

typedef struct MlSymIntegral
{
	int32_t     found;  /* 0 when no closed form was found */
	const MlSym *value; /* an antiderivative, without the constant */
} MlSymIntegral;

MlSymIntegral ml_sym_integrate (MlSymCtx *ctx, const MlSym *s, const MlSym *x);

typedef enum MlLimitKind
{
	ML_LIMIT_NONE = 0, /* the engine cannot find it */
	ML_LIMIT_VALUE,    /* a finite value */
	ML_LIMIT_PINF,     /* grows without bound */
	ML_LIMIT_NINF,     /* falls without bound */
	ML_LIMIT_DIFFER    /* the two sides have different limits */
} MlLimitKind;

typedef struct MlSymLimit
{
	MlLimitKind kind;
	const MlSym *value; /* ML_LIMIT_VALUE */
} MlSymLimit;

/*
 * The limit of f when the symbol x goes to at: an expression without x, or
 * the symbol inf or its negative. side is 1 from above, -1 from below and 0
 * for both (which must agree). Direct substitution first, then l'Hopital's
 * rule on quotients and on products of a zero and an infinite factor. A limit
 * the engine cannot decide is ML_LIMIT_NONE, never a guess.
 */
MlSymLimit ml_sym_limit (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	const MlSym *at,
	int32_t     side
);

typedef struct MlSymSolution
{
	int32_t     found; /* 0 when the engine cannot solve the equation */
	const MlSym *set;  /* the real solutions, as a set */
} MlSymSolution;

/*
 * The real solutions of f = 0 for the symbol x: the roots of a polynomial
 * (rational roots, then the quadratic formula for what is left), of a quotient
 * of polynomials (without the zeros of the denominator), and of an
 * expression that is a polynomial in exp (x), ln (x), a power or a root of x.
 * An equation the engine cannot solve is not found, never guessed.
 */
MlSymSolution ml_sym_solve (MlSymCtx *ctx, const MlSym *f, const MlSym *x);

/* ---- text ---- */

/*
 * The expression as text in the syntax of the tool: 2x, x² + 2x + 1, √x,
 * 1/x, sin(x)·cos(x). It parses back to the same expression.
 */
MlResult ml_sym_format (MlStr *out, const MlSym *s);

/* ---- from the syntax tree (build.c) ---- */

struct MlEnv;

/*
 * Builds the expression of a node of a parsed statement. Names bound to
 * numbers (or to expressions) in env, as of the snapshot, are replaced by
 * their values, except the one called free; every other name is a symbol.
 * Calls of user defined functions are inlined. Assignments, lists, matrices
 * and calls of functions the engine does not know make it fail: the context
 * gets the status ML_ERR_INVALID and the byte range in fail_start and
 * fail_len.
 */
const MlSym *ml_sym_from_ast (
	MlSymCtx           *ctx,
	const struct MlEnv *env,
	uint64_t           snapshot,
	MlSlice            source,
	const MlAst        *ast,
	uint64_t           node,
	MlSlice            free
);

#endif /* METH_SYM_EXPR_H */
