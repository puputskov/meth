#ifndef METH_CORE_ERR_H
#define METH_CORE_ERR_H

#include <meth/core/types.h>

typedef enum MlResult
{
	ML_OK = 0,
	ML_ERR_NOMEM,   /* allocation failed */
	ML_ERR_INVALID, /* invalid argument */
	ML_ERR_RANGE,   /* index or value out of range */
	ML_ERR_UTF8,    /* malformed UTF-8 */
	ML_ERR_IO,      /* file or console failure */
	ML_ERR_PARSE,   /* syntax error */
	ML_ERR_LIMIT,   /* a value or a computation became too large */
	ML_ERR_DIVZERO  /* division by zero */
} MlResult;

const char *ml_result_str (MlResult r);

#endif /* METH_CORE_ERR_H */
