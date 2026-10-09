#include <matlog/ui/highlight.h>

#include <matlog/math/eval.h>
#include <matlog/math/unit.h>
#include <matlog/parse/lexer.h>

static int32_t is_word (MlSlice text, const char *word)
{
	return ml_slice_equal (text, ml_slice_cstr (word));
}

static int32_t is_builtin_function (MlSlice name)
{
	int32_t found = 0;

	for (uint64_t i = 0; i < ml_function_name_count () && !found; i++)
	{
		found = ml_slice_equal (name, ml_function_name_at (i));
	}

	return found;
}

/* The class of a name token; called is 1 when a '(' follows it. */
static MlClass name_class (
	MlSlice     name,
	const MlEnv *env,
	uint64_t    snapshot,
	int32_t     called
)
{
	MlBinding b = { .kind = ML_BIND_NONE };
	MlClass   c = ML_CLASS_NAME;

	if (env != NULL)
	{
		b = ml_env_lookup (env, name, snapshot);
	}

	if (b.kind == ML_BIND_VAR)
	{
		c = ML_CLASS_NAME;
	}

	else if (b.kind == ML_BIND_FUNC)
	{
		c = ML_CLASS_FUNCTION;
	}

	else if (is_word (name, "include") || is_word (name, "lambda"))
	{
		c = ML_CLASS_KEYWORD;
	}

	else if (
		   is_word (name, "pi")
		|| is_word (name, "e")
		|| is_word (name, "\xCF\x80")
	)
	{
		c = ML_CLASS_CONSTANT;
	}

	else if (is_builtin_function (name) || called)
	{
		c = ML_CLASS_FUNCTION;
	}

	else if (ml_unit_find (name).found)
	{
		c = ML_CLASS_UNIT;
	}

	return c;
}

/* The class of each token kind; names are told apart by name_class. */
static const MlClass token_classes [ML_TOK_COUNT] =
{
	[ML_TOK_NUMBER]      = ML_CLASS_NUMBER,
	[ML_TOK_PLUS]        = ML_CLASS_OPERATOR,
	[ML_TOK_MINUS]       = ML_CLASS_OPERATOR,
	[ML_TOK_STAR]        = ML_CLASS_OPERATOR,
	[ML_TOK_SLASH]       = ML_CLASS_OPERATOR,
	[ML_TOK_CARET]       = ML_CLASS_OPERATOR,
	[ML_TOK_BANG]        = ML_CLASS_OPERATOR,
	[ML_TOK_EQUALS]      = ML_CLASS_OPERATOR,
	[ML_TOK_SQRT]        = ML_CLASS_OPERATOR,
	[ML_TOK_SUPERSCRIPT] = ML_CLASS_OPERATOR,
	[ML_TOK_LPAREN]      = ML_CLASS_PAREN,
	[ML_TOK_RPAREN]      = ML_CLASS_PAREN,
	[ML_TOK_LBRACKET]    = ML_CLASS_PAREN,
	[ML_TOK_RBRACKET]    = ML_CLASS_PAREN,
	[ML_TOK_COMMA]       = ML_CLASS_PAREN,
	[ML_TOK_ERROR]       = ML_CLASS_ERROR
};

MlResult ml_highlight (
	MlRunVec    *out,
	MlSlice     text,
	const MlEnv *env,
	uint64_t    snapshot,
	uint64_t    err_start,
	uint64_t    err_len
)
{
	MlLexResult lex  = ml_lex (text);
	MlResult    r    = lex.result;
	uint8_t     *cls = NULL;

	if (r == ML_OK && text.len > 0)
	{
		cls = (uint8_t *)calloc (text.len, 1);
		r   = ml_choose (cls != NULL, ML_OK, ML_ERR_NOMEM);
	}

	for (uint64_t i = 0; i < lex.tokens.len && r == ML_OK; i++)
	{
		MlToken t      = lex.tokens.data [i];
		int32_t called = (
			   i + 1 < lex.tokens.len
			&& lex.tokens.data [i + 1].kind == ML_TOK_LPAREN
			&& lex.tokens.data [i + 1].start == t.start + t.len
		);
		MlClass c      = ml_choose (
			t.kind == ML_TOK_NAME,
			name_class (ml_token_text (text, t), env, snapshot, called),
			token_classes [t.kind]
		);

		for (uint64_t k = 0; k < t.len; k++)
		{
			cls [t.start + k] = (uint8_t)c;
		}
	}

	for (
		uint64_t k = 0;
		k < err_len && err_start + k < text.len && r == ML_OK;
		k++
	)
	{
		cls [err_start + k] = ML_CLASS_ERROR;
	}

	/* runs: bytes of the same class together */
	for (uint64_t pos = 0; pos < text.len && r == ML_OK; )
	{
		uint64_t end = pos + 1;
		MlRun    run;

		while (end < text.len && cls [end] == cls [pos])
		{
			end++;
		}

		run.start = pos;
		run.len   = end - pos;
		run.cls   = (MlClass)cls [pos];
		r         = MlRunVec_push (out, run);
		pos       = end;
	}

	free (cls);
	MlTokenVec_free (&lex.tokens);
	return r;
}

MlClass ml_runs_class_at (const MlRunVec *runs, uint64_t pos)
{
	MlClass c = ML_CLASS_TEXT;

	for (uint64_t i = 0; i < runs->len; i++)
	{
		MlRun run = runs->data [i];

		if (pos >= run.start && pos - run.start < run.len)
		{
			c = runs->data [i].cls;
		}
	}

	return c;
}
