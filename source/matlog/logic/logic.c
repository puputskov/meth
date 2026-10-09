#include <matlog/logic/logic.h>

void ml_logic_defs_truncate (MlLogicDefVec *defs, uint64_t count)
{
	for (uint64_t i = count; i < defs->len; i++)
	{
		ml_str_free (&defs->data [i].name);
		ml_str_free (&defs->data [i].text);
	}

	if (count < defs->len)
	{
		defs->len = count;
	}
}

void ml_logic_defs_free (MlLogicDefVec *defs)
{
	ml_logic_defs_truncate (defs, 0);
	MlLogicDefVec_free (defs);
}

MlResult ml_logic_defs_add (
	MlLogicDefVec *defs,
	int32_t       domain,
	MlSlice       name,
	MlSlice       text
)
{
	MlLogicDef d;
	MlResult   r;

	d.domain = domain;
	ml_str_init (&d.name);
	ml_str_init (&d.text);
	r = ml_str_append (&d.name, name);

	if (r == ML_OK)
	{
		r = ml_str_append (&d.text, text);
	}

	if (r == ML_OK)
	{
		r = MlLogicDefVec_push (defs, d);
	}

	if (r != ML_OK)
	{
		ml_str_free (&d.name);
		ml_str_free (&d.text);
	}

	return r;
}

MlLogicFound ml_logic_find (
	const MlLogicDefVec *defs,
	uint64_t            count,
	int32_t             domain,
	MlSlice             name
)
{
	MlLogicFound f =
	{
		.found = 0,
		.text  = { .data = "", .len = 0 }
	};
	uint64_t     n = ml_choose (count < defs->len, count, defs->len);

	for (uint64_t i = n; i > 0 && !f.found; i--)
	{
		const MlLogicDef *d = &defs->data [i - 1];

		if (d->domain == domain && ml_slice_equal (ml_str_view (d->name), name))
		{
			f.found = 1;
			f.text  = ml_str_view (d->text);
		}
	}

	return f;
}

void ml_logic_out_init (MlLogicOut *o)
{
	o->value     = 0;
	o->error     = 0;
	o->code      = "";
	o->err_start = 0;
	o->err_len   = 0;
	ml_str_init (&o->text);
	ml_str_init (&o->block);
	ml_str_init (&o->def_name);
	ml_str_init (&o->def_text);
}

void ml_logic_out_free (MlLogicOut *o)
{
	ml_str_free (&o->text);
	ml_str_free (&o->block);
	ml_str_free (&o->def_name);
	ml_str_free (&o->def_text);
}

MlResult ml_logic_out_error (
	MlLogicOut *o,
	const char *code,
	const char *message,
	uint64_t   start,
	uint64_t   len
)
{
	MlResult r = ml_str_append (&o->text, ml_slice_cstr (message));

	if (r == ML_OK)
	{
		o->value     = 1;
		o->error     = 1;
		o->code      = code;
		o->err_start = start;
		o->err_len   = len;
	}

	return r;
}
