#include <matlog/session/options.h>

#include <string.h>

static const MlOption options [] =
{
	{
		.name    = "theme",
		.section = "display",
		.kind    = ML_OPTION_TEXT,
		.values  = "",
		.initial = "auto",
		.summary = "the colour theme (a built in one or a name.theme file)"
	},
	{
		.name    = "colors",
		.section = "display",
		.kind    = ML_OPTION_CHOICE,
		.values  = "24bit|256|16|off",
		.initial = "auto",
		.summary = "how many colours the terminal shows"
	},
	{
		.name    = "highlight",
		.section = "display",
		.kind    = ML_OPTION_BOOL,
		.values  = "",
		.initial = "on",
		.summary = "syntax colours in the editor"
	},
	{
		.name    = "watch",
		.section = "display",
		.kind    = ML_OPTION_BOOL,
		.values  = "",
		.initial = "off",
		.summary = "the watch window with the names and their values"
	},
	{
		.name    = "ghost",
		.section = "display",
		.kind    = ML_OPTION_BOOL,
		.values  = "",
		.initial = "on",
		.summary = "the preview of the result while typing"
	}
};

uint64_t ml_option_count (void)
{
	return ml_count (options);
}

MlOption ml_option_at (uint64_t index)
{
	return options [ml_choose (index < ml_count (options), index, 0)];
}

uint64_t ml_option_find (MlSlice name)
{
	uint64_t found = ml_count (options);

	for (
		uint64_t i = 0;
		i < ml_count (options) && found == ml_count (options);
		i++
	)
	{
		if (ml_slice_equal (name, ml_slice_cstr (options [i].name)))
		{
			found = i;
		}
	}

	return found;
}

static int32_t same (MlSlice a, const char *word)
{
	return ml_slice_equal (a, ml_slice_cstr (word));
}

int32_t ml_option_bool (MlSlice value)
{
	int32_t on  = (
		   same (value, "on")
		|| same (value, "true")
		|| same (value, "yes")
		|| same (value, "1")
	);
	int32_t off = (
		   same (value, "off")
		|| same (value, "false")
		|| same (value, "no")
		|| same (value, "0")
	);

	return ml_choose (on, 1, ml_choose (off, 0, -1));
}

/* 1 when value is one of the words of the list a|b|c. */
static int32_t in_list (MlSlice value, const char *list)
{
	int32_t  found = 0;
	uint64_t start = 0;
	uint64_t n     = strlen (list);

	for (uint64_t i = 0; i <= n; i++)
	{
		if (i == n || list [i] == '|')
		{
			found = (
				   found
				|| ml_slice_equal (value, ml_slice (list + start, i - start))
			);
			start = i + 1;
		}
	}

	return found;
}

MlResult ml_option_check (uint64_t index, MlSlice value, MlStr *out)
{
	MlResult r = ML_ERR_PARSE;

	if (index < ml_count (options))
	{
		MlOption o = options [index];

		if (o.kind == ML_OPTION_BOOL && ml_option_bool (value) >= 0)
		{
			r = ml_str_append (
				out,
				ml_slice_cstr (ml_choose (ml_option_bool (value), "on", "off"))
			);
		}

		else if (o.kind == ML_OPTION_CHOICE && in_list (value, o.values))
		{
			r = ml_str_append (out, value);
		}

		else if (o.kind == ML_OPTION_TEXT && value.len > 0)
		{
			r = ml_str_append (out, value);
		}
	}

	return r;
}
