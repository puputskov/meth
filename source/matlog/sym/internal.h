#ifndef MATLOG_SYM_INTERNAL_H
#define MATLOG_SYM_INTERNAL_H

#include <matlog/sym/expr.h>

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

#endif /* MATLOG_SYM_INTERNAL_H */
