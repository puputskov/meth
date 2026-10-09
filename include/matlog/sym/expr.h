#ifndef MATLOG_SYM_EXPR_H
#define MATLOG_SYM_EXPR_H

#include <matlog/math/rational.h>
#include <matlog/math/steplog.h>
#include <matlog/parse/parser.h>

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
	ML_SYM_SUBSTITUTE   /* replace a symbol by an expression, then rebuild */
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

/* Multiplies out products of sums and integer powers of sums. */
const MlSym *ml_sym_expand (MlSymCtx *ctx, const MlSym *s);

/* s with every x replaced by value, then simplified. */
const MlSym *ml_sym_substitute (
	MlSymCtx    *ctx,
	const MlSym *s,
	const MlSym *x,
	const MlSym *value
);

/* ---- calculus (diff.c, integrate.c) ---- */

/* The derivative of s by the symbol x, in canonical form. */
const MlSym *ml_sym_diff (MlSymCtx *ctx, const MlSym *s, const MlSym *x);

typedef struct MlSymIntegral
{
	int32_t     found;  /* 0 when no closed form was found */
	const MlSym *value; /* an antiderivative, without the constant */
} MlSymIntegral;

MlSymIntegral ml_sym_integrate (MlSymCtx *ctx, const MlSym *s, const MlSym *x);

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

#endif /* MATLOG_SYM_EXPR_H */
