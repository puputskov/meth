#include <matlog/logic/logic.h>

static const char *const error_codes [ML_LOGIC_COUNT] =
{
	[ML_LOGIC_OK]                            = "",
	[ML_LOGIC_BOOL_UNEXPECTED_CHARACTER]     = "bool_unexpected_character",
	[ML_LOGIC_BOOL_EXPECTED_OPERAND]         = "bool_expected_operand",
	[ML_LOGIC_BOOL_EXPECTED_OPERATOR]        = "bool_expected_operator",
	[ML_LOGIC_BOOL_UNBALANCED_PARENTHESIS]   = "bool_unbalanced_parenthesis",
	[ML_LOGIC_BOOL_TOO_MANY_NAMES]           = "bool_too_many_names",
	[ML_LOGIC_BOOL_TABLE_TOO_BIG]            = "bool_table_too_big",
	[ML_LOGIC_BOOL_BAD_DEFINITION]           = "bool_bad_definition",
	[ML_LOGIC_LAMBDA_UNEXPECTED_CHARACTER]   = "lambda_unexpected_character",
	[ML_LOGIC_LAMBDA_EXPECTED_TERM]          = "lambda_expected_term",
	[ML_LOGIC_LAMBDA_EXPECTED_PARAMETER]     = "lambda_expected_parameter",
	[ML_LOGIC_LAMBDA_UNBALANCED_PARENTHESIS] = "lambda_unbalanced_parenthesis",
	[ML_LOGIC_LAMBDA_NUMBER_TOO_BIG]         = "lambda_number_too_big",
	[ML_LOGIC_LAMBDA_NO_NORMAL_FORM]         = "lambda_no_normal_form",
	[ML_LOGIC_LAMBDA_TOO_BIG]                = "lambda_too_big",
	[ML_LOGIC_LAMBDA_BAD_DEFINITION]         = "lambda_bad_definition"
};

static const char *const error_texts [ML_LOGIC_COUNT] =
{
	[ML_LOGIC_OK]                            = "",
	[ML_LOGIC_BOOL_UNEXPECTED_CHARACTER]     = "unexpected character",
	[ML_LOGIC_BOOL_EXPECTED_OPERAND]         = "an operand is missing",
	[ML_LOGIC_BOOL_EXPECTED_OPERATOR]        = "an operator is missing",
	[ML_LOGIC_BOOL_UNBALANCED_PARENTHESIS]   = "unbalanced parenthesis",
	[ML_LOGIC_BOOL_TOO_MANY_NAMES]           = "too many names (at most 10)",
	[ML_LOGIC_BOOL_TABLE_TOO_BIG]            = "a table needs at most 6 names",
	[ML_LOGIC_BOOL_BAD_DEFINITION]           = "only a name can be defined",
	[ML_LOGIC_LAMBDA_UNEXPECTED_CHARACTER]   = "unexpected character",
	[ML_LOGIC_LAMBDA_EXPECTED_TERM]          = "a term is missing",
	[ML_LOGIC_LAMBDA_EXPECTED_PARAMETER]     =
		"a lambda needs names and then a dot: \xCE\xBBx. body",
	[ML_LOGIC_LAMBDA_UNBALANCED_PARENTHESIS] = "unbalanced parenthesis",
	[ML_LOGIC_LAMBDA_NUMBER_TOO_BIG]         = "a number is at most 200",
	[ML_LOGIC_LAMBDA_NO_NORMAL_FORM]         =
		"no normal form within 5000 steps: the term may not have one",
	[ML_LOGIC_LAMBDA_TOO_BIG]                = "the term got too big",
	[ML_LOGIC_LAMBDA_BAD_DEFINITION]         = "only a name can be defined"
};

const char *ml_logic_error_code (MlLogicError e)
{
	return ml_choose (
		(int32_t)e >= 0 && e < ML_LOGIC_COUNT,
		error_codes [e],
		""
	);
}

const char *ml_logic_error_text (MlLogicError e)
{
	return ml_choose (
		(int32_t)e >= 0 && e < ML_LOGIC_COUNT,
		error_texts [e],
		""
	);
}

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
	o->error     = ML_LOGIC_OK;
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
	MlLogicOut   *o,
	MlLogicError error,
	uint64_t     start,
	uint64_t     len
)
{
	MlResult r = ml_str_append (
		&o->text,
		ml_slice_cstr (ml_logic_error_text (error))
	);

	if (r == ML_OK)
	{
		o->value     = 1;
		o->error     = error;
		o->err_start = start;
		o->err_len   = len;
	}

	return r;
}
