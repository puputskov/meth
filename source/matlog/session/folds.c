#include <matlog/session/folds.h>

#include <matlog/session/settings.h>

#define SECTION "folds"

MlResult ml_folds_format (MlStr *out, const MlSession *s)
{
	MlResult r     = ML_OK;
	int32_t  first = 1;

	for (uint64_t i = 0; i < s->lines.len && r == ML_OK; i++)
	{
		MlBlock b = ml_session_block (s, i);

		if (b.first == i && b.folded && b.last > b.first)
		{
			r = ml_str_appendf (
				out,
				"%s%u",
				ml_choose (first, "", " "),
				(uint32_t)i
			);
			first = 0;
		}
	}

	return r;
}

static int32_t is_digit (char c)
{
	return c >= '0' && c <= '9';
}

/* Folds the blocks that start at the line numbers listed in value. */
static void apply_folds (MlSession *s, MlSlice value)
{
	uint64_t pos = 0;

	while (pos < value.len)
	{
		uint64_t n      = 0;
		int32_t  digits = 0;

		while (pos < value.len && !is_digit (value.data [pos]))
		{
			pos++;
		}

		while (pos < value.len && is_digit (value.data [pos]))
		{
			n      = n * 10 + (uint64_t)(value.data [pos] - '0');
			digits = 1;
			pos++;
		}

		if (digits && n < s->lines.len)
		{
			MlBlock b = ml_session_block (s, n);

			if (b.first == n && b.last > b.first)
			{
				ml_session_set_folded (s, n, 1);
			}
		}
	}
}

MlResult ml_folds_load (MlSession *s)
{
	MlResult r     = ML_OK;
	MlStr    value;
	int32_t  found = 0;

	ml_str_init (&value);

	if (s->path.len > 0)
	{
		r = ml_settings_get (
			&value,
			ml_slice_cstr (SECTION),
			ml_str_view (s->path),
			&found
		);
	}

	if (r == ML_OK && found)
	{
		apply_folds (s, ml_str_view (value));
	}

	ml_str_free (&value);
	return r;
}

MlResult ml_folds_save (const MlSession *s)
{
	MlResult r = ML_OK;
	MlStr    entry;

	ml_str_init (&entry);

	if (s->path.len > 0)
	{
		r = ml_folds_format (&entry, s);
	}

	/* a settings folder that does not exist is not worth an error */
	if (r == ML_OK && s->path.len > 0 && entry.len > 0)
	{
		ml_settings_set (
			ml_slice_cstr (SECTION),
			ml_str_view (s->path),
			ml_str_view (entry)
		);
	}

	else if (r == ML_OK && s->path.len > 0)
	{
		ml_settings_remove (ml_slice_cstr (SECTION), ml_str_view (s->path));
	}

	ml_str_free (&entry);
	return r;
}
