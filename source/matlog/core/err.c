#include <matlog/core/err.h>

static const char *const names[] =
{
	"ok",
	"out of memory",
	"invalid argument",
	"out of range",
	"malformed UTF-8",
	"I/O error",
	"syntax error"
};

const char *ml_result_str (MlResult r)
{
	int32_t known = (int32_t)r >= 0 && (uint64_t)r < sizeof names / sizeof names[0];

	return known ? names[r] : "unknown error";
}
