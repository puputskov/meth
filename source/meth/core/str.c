#include <meth/core/str.h>

#include <meth/core/heap.h>
#include <meth/core/utf8.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ml_str_init (MlStr *s)
{
	s->data = NULL;
	s->len  = 0;
	s->cap  = 0;
}

void ml_str_free (MlStr *s)
{
	ml_heap_free (s->data);
	ml_str_init (s);
}

void ml_str_clear (MlStr *s)
{
	s->len = 0;
	if (s->data)
	{
		s->data [0] = '\0';
	}
}

MlSlice ml_str_view (MlStr s)
{
	return ml_slice (
		ml_choose (
			s.data != NULL,
			s.data,
			""
		),
		s.len
	);
}

const char *ml_str_cstr (MlStr s)
{
	return ml_choose (
		s.data,
		s.data,
		""
	);
}

MlResult ml_str_reserve (MlStr *s, uint64_t len)
{
	MlResult r = ML_OK;

	if (len >= SIZE_MAX - 1)
	{
		r = ML_ERR_NOMEM;
	}

	else if (len + 1 > s->cap)
	{
		uint64_t need = len + 1;
		uint64_t cap  = ml_choose (
			s->cap,
			s->cap,
			16
		);
		char *p;

		while (cap < need)
		{
			cap = ml_choose (
				(cap > SIZE_MAX / 2),
				need,
				cap * 2
			);
		}

		p = ml_heap_realloc (s->data, cap);
		if (p)
		{
			if (!s->data)
			{
				p [0] = '\0';
			}

			s->data = p;
			s->cap  = cap;
		}

		else
		{
			r = ML_ERR_NOMEM;
		}
	}

	return r;
}

MlResult ml_str_insert (MlStr *s, uint64_t pos, MlSlice text)
{
	MlResult r = ML_OK;

	if (pos > s->len)
	{
		r = ML_ERR_RANGE;
	}

	else if (text.len > SIZE_MAX - 1 - s->len)
	{
		r = ML_ERR_NOMEM;
	}

	else
	{
		r = ml_str_reserve (s, s->len + text.len);
	}

	if (r == ML_OK && text.len > 0)
	{
		memmove (s->data + pos + text.len, s->data + pos, s->len - pos);
		memcpy (s->data + pos, text.data, text.len);
		s->len += text.len;
		s->data [s->len] = '\0';
	}

	return r;
}

MlResult ml_str_append (MlStr *s, MlSlice text)
{
	return ml_str_insert (s, s->len, text);
}

MlResult ml_str_append_cp (MlStr *s, uint32_t cp)
{
	MlEncoded e = ml_utf8_encode (cp);

	return ml_choose (
		e.size > 0,
		ml_str_append (s, ml_slice (e.bytes, e.size)),
		ML_ERR_UTF8
	);
}

MlResult ml_str_appendf (MlStr *s, const char *fmt, ...)
{
	MlResult r = ML_OK;
	va_list  ap;
	va_list  ap2;
	int32_t  n;

	va_start (ap, fmt);
	va_copy (ap2, ap);
	n = vsnprintf (NULL, 0, fmt, ap);
	if (n >= 0)
	{
		r = ml_str_reserve (s, s->len + (uint64_t)n);
		if (r == ML_OK)
		{
			vsnprintf (s->data + s->len, (uint64_t)n + 1, fmt, ap2);
			s->len += (uint64_t)n;
		}
	}

	else
	{
		r = ML_ERR_INVALID;
	}

	va_end (ap2);
	va_end (ap);
	return r;
}

MlResult ml_str_erase (MlStr *s, uint64_t pos, uint64_t n)
{
	MlResult r = ml_choose (
		pos <= s->len,
		ML_OK,
		ML_ERR_RANGE
	);

	if (r == ML_OK)
	{
		uint64_t count = ml_choose (
			n < s->len - pos,
			n,
			s->len - pos
		);

		if (count > 0)
		{
			memmove (
				s->data + pos,
				s->data + pos + count,
				s->len - pos - count
			);
			s->len -= count;
			s->data [s->len] = '\0';
		}
	}

	return r;
}
