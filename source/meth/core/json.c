#include <meth/core/json.h>

#include <math.h>

static MlResult add (MlStr *out, const char *text)
{
	return ml_str_append (out, ml_slice_cstr (text));
}

MlResult ml_json_string (MlStr *out, MlSlice text)
{
	MlResult r = add (out, "\"");

	for (uint64_t i = 0; i < text.len && r == ML_OK; i++)
	{
		uint8_t c = (uint8_t)text.data [i];

		if (c == '"' || c == '\\')
		{
			r = ml_str_appendf (out, "\\%c", (int32_t)c);
		}

		else if (c == '\n')
		{
			r = add (out, "\\n");
		}

		else if (c == '\r')
		{
			r = add (out, "\\r");
		}

		else if (c == '\t')
		{
			r = add (out, "\\t");
		}

		else if (c < 0x20)
		{
			r = ml_str_appendf (out, "\\u%04x", (uint32_t)c);
		}

		else
		{
			r = ml_str_append (out, ml_slice_sub (text, i, 1));
		}
	}

	if (r == ML_OK)
	{
		r = add (out, "\"");
	}

	return r;
}

MlResult ml_json_rows (MlStr *out, MlSlice text)
{
	MlResult r     = add (out, "[");
	uint64_t pos   = 0;
	int32_t  first = 1;

	while (r == ML_OK && pos < text.len)
	{
		uint64_t end = pos;
		MlSlice  row;

		while (end < text.len && text.data [end] != '\n')
		{
			end++;
		}

		row = ml_slice_sub (text, pos, end - pos);
		pos = ml_choose (end < text.len, end + 1, end);

		while (row.len > 0 && row.data [row.len - 1] == '\r')
		{
			row.len--;
		}

		r = ml_choose (first, r, add (out, ","));
		first = 0;

		if (r == ML_OK)
		{
			r = ml_json_string (out, row);
		}
	}

	if (r == ML_OK)
	{
		r = add (out, "]");
	}

	return r;
}

MlResult ml_json_number (MlStr *out, double value)
{
	MlResult r = ML_OK;

	if (isfinite (value))
	{
		r = ml_str_appendf (out, "%.17g", value);
	}

	else
	{
		r = add (out, "null");
	}

	return r;
}
