#ifndef METH_SYM_INTERNAL_H
#define METH_SYM_INTERNAL_H

#include <meth/parse/words.h>
#include <meth/sym/expr.h>

/* Shared by the files of the symbolic module. */

ML_VEC_DEFINE (SymPtrVec, const MlSym *)

/* A tree where a failed operation leaves a harmless symbol. */
const MlSym *ml_sym_failed (void);

/*
 * The rewrite that ml_sym_map applies to a node whose kids are already
 * rewritten: the canonical form (or the expanded one) of the node over those
 * kids. For a leaf the kids are none. In ML_SYM_SUBSTITUTE mode the symbol is
 * replaced here.
 */
const MlSym *ml_sym_rewrite (
	MlSymCtx           *ctx,
	const MlSym        *orig,
	const MlSym *const *kids,
	uint64_t           count
);

/* Rewrites s bottom up with ml_sym_rewrite, using ctx->mode. */
const MlSym *ml_sym_map (MlSymCtx *ctx, const MlSym *s);

/* ---- polynomials with rational coefficients (poly.c) ---- */

/* c [i] is the coefficient of x^i; len is the degree + 1, 0 for zero. */
typedef struct MlPoly
{
	MlRat    *c;
	uint64_t len;
} MlPoly;

MlPoly ml_poly_zero (void);
MlPoly ml_poly_make (MlSymCtx *ctx, uint64_t len);
MlPoly ml_poly_trim (MlPoly p);
int32_t ml_poly_is_zero (MlPoly p);
MlRat ml_poly_lead (MlPoly p);
MlPoly ml_poly_add (MlSymCtx *ctx, MlPoly a, MlPoly b);
MlPoly ml_poly_sub (MlSymCtx *ctx, MlPoly a, MlPoly b);
MlPoly ml_poly_mul (MlSymCtx *ctx, MlPoly a, MlPoly b);
MlPoly ml_poly_scale (MlSymCtx *ctx, MlPoly a, MlRat k);

/* a = q b + r with r of lower degree than b; b must not be zero. */
void ml_poly_divmod (
	MlSymCtx *ctx,
	MlPoly   a,
	MlPoly   b,
	MlPoly   *q,
	MlPoly   *r
);

/* The polynomial in x that an expanded expression is, 0 if it is not one. */
int32_t ml_poly_from_sym (
	MlSymCtx    *ctx,
	const MlSym *s,
	const MlSym *x,
	MlPoly      *out
);

const MlSym *ml_poly_to_sym (MlSymCtx *ctx, MlPoly p, const MlSym *x);

/* The rational roots of p with their multiplicities, and what is left. */
#define ML_POLY_MAX_ROOTS 16
#define MAX_POLY_DEGREE   24

typedef struct MlPolyRoots
{
	int32_t  ok;      /* 0 when the coefficients are too big to search */
	uint64_t count;
	MlRat    root [ML_POLY_MAX_ROOTS];
	uint64_t mult [ML_POLY_MAX_ROOTS];
	MlPoly   rest;    /* p / (lead * the linear factors), monic */
	MlRat    lead;    /* the leading coefficient of p */
} MlPolyRoots;

MlPolyRoots ml_poly_roots (MlSymCtx *ctx, MlPoly p);

/*
 * The antiderivative of a rational function p / q, or NULL when q has a factor
 * that is not linear and not a single irreducible quadratic (partial
 * fractions, ratfun.c). The function is given as its two polynomials.
 */
const MlSym *ml_int_rational (
	MlSymCtx    *ctx,
	MlPoly      num,
	MlPoly      den,
	const MlSym *x
);

/* f as a quotient of two polynomials (the denominator of degree >= 1). */
int32_t ml_rational_parts (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlPoly      *num,
	MlPoly      *den
);

/* The antiderivative of f when it is a quotient of polynomials, else NULL. */
const MlSym *ml_int_rational_sym (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x
);

/* The square root of a positive rational: 2 sqrt (2) for 8, 3/2 for 9/4. */
const MlSym *ml_rat_root_node (MlSymCtx *ctx, MlRat r);

/* The distinct inner parts of f that contain x, the outer ones first. */
void ml_sym_collect_parts (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	SymPtrVec   *out
);

/*
 * u = a x + b with a and b free of x and a not zero: 1 and the parts, else 0.
 */
int32_t ml_sym_linear_parts (
	MlSymCtx    *ctx,
	const MlSym *u,
	const MlSym *x,
	const MlSym **a,
	const MlSym **b
);

/* ---- integration (integrate.c, intmore.c) ---- */

/* The table of integrals, for a product of factors that all contain x. */
const MlSym *ml_int_table (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlRule      *rule
);

/* The antiderivative by the table and partial fractions alone, or NULL. */
const MlSym *ml_int_simple (MlSymCtx *ctx, const MlSym *g, const MlSym *x);

/* Splits a term into the factors without x and the product with x. */
void ml_int_split (
	MlSymCtx    *ctx,
	const MlSym *t,
	const MlSym *x,
	const MlSym **free_part,
	const MlSym **x_part
);

/* Substitution u = inner part of f; NULL when none works. */
const MlSym *ml_int_subst (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlRule      *rule
);

/* Integration by parts, repeated, also solving I = A + k I; NULL if none. */
const MlSym *ml_int_parts (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	MlRule      *rule
);

/* 1 when the derivative of big agrees with f at some sample points. */
int32_t ml_int_verify (
	MlSymCtx    *ctx,
	const MlSym *big,
	const MlSym *f,
	const MlSym *x
);

/* A line of the steps: head, a, mid, b (a, mid and b may be NULL). */
void ml_int_note (
	MlSymCtx    *ctx,
	const char  *head,
	const MlSym *a,
	const char  *mid,
	const MlSym *b
);

#endif /* METH_SYM_INTERNAL_H */
