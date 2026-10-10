#ifndef METH_CORE_JSON_H
#define METH_CORE_JSON_H

#include <meth/core/err.h>
#include <meth/core/slice.h>
#include <meth/core/str.h>

/*
 * The pieces of JSON the program writes (the command line interface and the
 * web editor): all of them append to a string, none of them reads JSON.
 */

/* A string literal: quotes, escapes; UTF-8 passes through. */
MlResult ml_json_string (MlStr *out, MlSlice text);

/*
 * The rows of text (separated by newlines, a carriage return before a newline
 * is dropped) as an array of strings; no text is an empty array.
 */
MlResult ml_json_rows (MlStr *out, MlSlice text);

/* A number; null when it is not finite, since JSON has no such number. */
MlResult ml_json_number (MlStr *out, double value);

#endif /* METH_CORE_JSON_H */
