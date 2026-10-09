#include "internal.h"

const MlSym *ml_sym_diff (MlSymCtx *ctx, const MlSym *s, const MlSym *x)
{
	(void)x;
	return ml_choose (ctx->status == ML_OK, s, ml_sym_failed ());
}
