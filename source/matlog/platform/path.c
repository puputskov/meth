#include <matlog/platform/path.h>

static int32_t is_sep (char ch)
{
	return ch == '/' || ch == '\\';
}

/* Index just past the last separator, 0 when there is none. */
static uint64_t name_start (MlSlice path)
{
	uint64_t i     = path.len;
	int32_t  found = 0;

	while (i > 0 && !found)
	{
		found = is_sep (path.data [i - 1]);
		i    -= found ? 0 : 1;
	}

	return i;
}

MlSlice ml_path_name (MlSlice path)
{
	uint64_t start = name_start (path);

	return ml_slice_sub (path, start, path.len - start);
}

MlSlice ml_path_dir (MlSlice path)
{
	uint64_t start = name_start (path);
	uint64_t end   = start;

	/* drop trailing separators, but keep a lone root separator */
	while (end > 1 && is_sep (path.data [end - 1]))
	{
		end--;
	}

	return ml_slice_sub (path, 0, end);
}

int32_t ml_path_is_absolute (MlSlice path)
{
	int32_t drive = path.len >= 2 && path.data [1] == ':';

	return (path.len >= 1 && is_sep (path.data [0])) || drive;
}

MlResult ml_path_join (MlStr *out, MlSlice dir, MlSlice name)
{
	MlResult r = ML_OK;

	if (dir.len == 0 || ml_path_is_absolute (name))
	{
		r = ml_str_append (out, name);
	}

	else
	{
		int32_t has_sep = is_sep (dir.data [dir.len - 1]);

		r = ml_str_append (out, dir);

		if (r == ML_OK && !has_sep)
		{
			r = ml_str_append (out, ml_slice_cstr ("/"));
		}

		if (r == ML_OK)
		{
			r = ml_str_append (out, name);
		}
	}

	return r;
}
