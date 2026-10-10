#ifndef METH_WEB_INTERNAL_H
#define METH_WEB_INTERNAL_H

#include <meth/core/json.h>
#include <meth/core/str.h>
#include <meth/texts/texts.h>
#include <meth/web/web.h>

/* ---- writing JSON ------------------------------------------------------ */

/*
 * Each helper appends to the answer when r is still ML_OK and returns the new
 * state, so a sequence reads as r = lit (r, o, ...); r = num (r, o, ...).
 */

static inline MlResult lit (MlResult r, MlStr *o, const char *text)
{
	return ml_choose (
		r == ML_OK,
		ml_str_append (o, ml_slice_cstr (text)),
		r
	);
}

static inline MlResult num (MlResult r, MlStr *o, uint64_t n)
{
	char buf [ML_TEXT_NUMBER];

	return ml_choose (
		r == ML_OK,
		ml_str_append (o, ml_text_number (buf, n)),
		r
	);
}

static inline MlResult str (MlResult r, MlStr *o, MlSlice text)
{
	return ml_choose (r == ML_OK, ml_json_string (o, text), r);
}

static inline MlResult rows (MlResult r, MlStr *o, MlSlice text)
{
	return ml_choose (r == ML_OK, ml_json_rows (o, text), r);
}

static inline MlResult flag (MlResult r, MlStr *o, int32_t on)
{
	return lit (r, o, ml_choose (on, "true", "false"));
}

#endif /* METH_WEB_INTERNAL_H */
