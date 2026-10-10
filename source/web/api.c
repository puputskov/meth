#include <meth/cli/cli.h>
#include <meth/core/heap.h>

/*
 * What the page calls (build/web/meth.wasm). The page writes text into the
 * module's memory with meth_alloc, calls a function and reads the answer from
 * the pointer and length that meth_result_ptr and meth_result_len give. No
 * structure of the library is seen by the page.
 */

#define EXPORT(name) \
	__attribute__ ((export_name (name)))

static MlStr last;
static int32_t started = 0;

EXPORT ("meth_alloc")
uint8_t *meth_alloc (uint32_t size)
{
	return ml_heap_alloc (size);
}

EXPORT ("meth_free")
void meth_free (uint8_t *p)
{
	ml_heap_free (p);
}

/*
 * Runs `meth eval --format json EXPR`. Returns the exit code of the command;
 * the output is the result text. Standard error is appended after it.
 */
EXPORT ("meth_eval")
int32_t meth_eval (const uint8_t *expr, uint32_t len)
{
	MlSliceVec args;
	MlStr      out;
	MlStr      err;
	int32_t    code = ML_EXIT_USAGE;

	if (!started)
	{
		ml_str_init (&last);
		started = 1;
	}

	ml_str_init (&out);
	ml_str_init (&err);
	MlSliceVec_init (&args);

	if (
		   MlSliceVec_push (&args, ml_slice_cstr ("eval")) == ML_OK
		&& MlSliceVec_push (&args, ml_slice_cstr ("--format")) == ML_OK
		&& MlSliceVec_push (&args, ml_slice_cstr ("json")) == ML_OK
		&& MlSliceVec_push (&args, ml_slice ((const char *)expr, len)) == ML_OK
	)
	{
		code = ml_cli_run (&args, ml_slice ("", 0), &out, &err);
	}

	ml_str_clear (&last);
	ml_str_append (&last, ml_str_view (out));
	ml_str_append (&last, ml_str_view (err));
	MlSliceVec_free (&args);
	ml_str_free (&out);
	ml_str_free (&err);
	return code;
}

EXPORT ("meth_result_ptr")
const char *meth_result_ptr (void)
{
	return ml_str_cstr (last);
}

EXPORT ("meth_result_len")
uint32_t meth_result_len (void)
{
	return (uint32_t)last.len;
}
