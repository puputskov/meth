#include <matlog/core/err.h>

static const char *const names [] =
{
	[ML_OK]          = "ok",
	[ML_ERR_NOMEM]   = "out of memory",
	[ML_ERR_INVALID] = "invalid argument",
	[ML_ERR_RANGE]   = "out of range",
	[ML_ERR_UTF8]    = "malformed UTF-8",
	[ML_ERR_IO]      = "I/O error",
	[ML_ERR_PARSE]   = "syntax error"
};

const char *ml_result_str (MlResult r)
{
	int32_t known = (int32_t)r >= 0 && (uint64_t)r < sizeof names / sizeof names [0];

	return known ? names [r] : "unknown error";
}
