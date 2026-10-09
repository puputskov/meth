#include "internal.h"

MlSymIntegral ml_sym_integrate (MlSymCtx *ctx, const MlSym *s, const MlSym *x)
{
	MlSymIntegral r =
	{
		.found = 0,
		.value = s
	};

	(void)ctx;
	(void)x;
	return r;
}
