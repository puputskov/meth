#include <meth/core/slice.h>

#include <string.h>

MlSlice ml_slice (const char *data, uint64_t len)
{
	MlSlice s;

	s.data = data;
	s.len  = len;
	return s;
}

MlSlice ml_slice_cstr (const char *cstr)
{
	return ml_slice (
		cstr,
		ml_choose (
			cstr != NULL,
			strlen (cstr),
			0
		)
	);
}

MlSlice ml_slice_sub (MlSlice s, uint64_t pos, uint64_t n)
{
	MlSlice r = ml_slice (s.data, 0);

	if (pos < s.len)
	{
		r.data = s.data + pos;
		r.len  = ml_choose (
			(n < s.len - pos),
			n,
			s.len - pos
		);
	}

	return r;
}

int32_t ml_slice_equal (MlSlice a, MlSlice b)
{
	return (
		   a.len == b.len
		&& (a.len == 0 || memcmp (a.data, b.data, a.len) == 0)
	);
}
