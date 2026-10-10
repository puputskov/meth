#include <matlog/ui/highlight.h>

#include <matlog/core/heap.h>
#include <matlog/logic/bool.h>
#include <matlog/math/eval.h>
#include <matlog/math/unit.h>
#include <matlog/parse/lexer.h>
#include <matlog/parse/words.h>

#include <string.h>

/* For the commands of one domain (table, trace), no words of the syntax. */
static int32_t is_word (MlSlice text, const char *word)
{
	return ml_slice_equal (text, ml_slice_cstr (word));
}

/* The λ (or \\) of the lambda domain at pos. */
static int32_t is_lambda_at (MlSlice text, uint64_t pos)
{
	return (
		   (pos + 2 <= text.len && memcmp (text.data + pos, "\xCE\xBB", 2) == 0)
		|| (pos < text.len && text.data [pos] == '\\')
	);
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

	else if (
		   ml_word_find (name).kind == ML_WORD_KEYWORD
		|| ml_word_find (name).kind == ML_WORD_OPERATOR
	)
	{
		c = ML_CLASS_KEYWORD;
	}

	else if (ml_word_find (name).kind == ML_WORD_CONSTANT)
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
static const MlClass token_classes [ML_TOK_COUNT] = {
	[ML_TOK_NUMBER]      = ML_CLASS_NUMBER,
	[ML_TOK_PLUS]        = ML_CLASS_OPERATOR,
	[ML_TOK_MINUS]       = ML_CLASS_OPERATOR,
	[ML_TOK_STAR]        = ML_CLASS_OPERATOR,
	[ML_TOK_SLASH]       = ML_CLASS_OPERATOR,
	[ML_TOK_CARET]       = ML_CLASS_OPERATOR,
	[ML_TOK_BANG]        = ML_CLASS_OPERATOR,
	[ML_TOK_EQUALS]      = ML_CLASS_OPERATOR,
	[ML_TOK_LT]          = ML_CLASS_OPERATOR,
	[ML_TOK_GT]          = ML_CLASS_OPERATOR,
	[ML_TOK_LE]          = ML_CLASS_OPERATOR,
	[ML_TOK_GE]          = ML_CLASS_OPERATOR,
	[ML_TOK_EQEQ]        = ML_CLASS_OPERATOR,
	[ML_TOK_NE]          = ML_CLASS_OPERATOR,
	[ML_TOK_SQRT]        = ML_CLASS_OPERATOR,
	[ML_TOK_SUPERSCRIPT] = ML_CLASS_OPERATOR,
	[ML_TOK_LPAREN]      = ML_CLASS_PAREN,
	[ML_TOK_RPAREN]      = ML_CLASS_PAREN,
	[ML_TOK_LBRACKET]    = ML_CLASS_PAREN,
	[ML_TOK_RBRACKET]    = ML_CLASS_PAREN,
	[ML_TOK_COMMA]       = ML_CLASS_PAREN,
	[ML_TOK_SEMI]        = ML_CLASS_OPERATOR,
	[ML_TOK_ARROW]       = ML_CLASS_OPERATOR,
	[ML_TOK_ERROR]       = ML_CLASS_ERROR
};

/* Sets the class of the bytes from start on, len of them. */
static void paint (uint8_t *cls, uint64_t start, uint64_t len, MlClass c)
{
	for (uint64_t k = 0; k < len; k++)
	{
		cls [start + k] = (uint8_t)c;
	}
}

/* The classes of a line of the algebra domain, from the lexer. */
static MlResult classify_algebra (
	uint8_t     *cls,
	MlSlice     text,
	const MlEnv *env,
	uint64_t    snapshot
)
{
	MlLexResult lex = ml_lex (text);
	MlResult    r   = lex.result;

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

		paint (cls, t.start, t.len, c);
	}

	MlTokenVec_free (&lex.tokens);
	return r;
}

/* The class of a symbol of the boolean domain. */
static MlClass symbol_class (MlBoolSymbolKind kind)
{
	MlClass c = ML_CLASS_ERROR;

	c = ml_choose (
		   kind == ML_BSYM_AND
		|| kind == ML_BSYM_OR
		|| kind == ML_BSYM_NOT
		|| kind == ML_BSYM_XOR
		|| kind == ML_BSYM_IMPL
		|| kind == ML_BSYM_EQUIV,
		ML_CLASS_LOGIC,
		c
	);
	c = ml_choose (kind == ML_BSYM_CONST, ML_CLASS_CONSTANT, c);
	c = ml_choose (
		kind == ML_BSYM_LPAREN || kind == ML_BSYM_RPAREN,
		ML_CLASS_PAREN,
		c
	);
	c = ml_choose (kind == ML_BSYM_ASSIGN, ML_CLASS_OPERATOR, c);
	return c;
}

static int32_t is_blank_byte (char c)
{
	return c == ' ' || c == '\t';
}

static int32_t is_digit_byte (char c)
{
	return c >= '0' && c <= '9';
}

/* A byte that can be inside a name: letters, digits, _ ' and non-ASCII. */
static int32_t is_name_byte (char c)
{
	uint8_t u = (uint8_t)c;

	return (
		   (u >= 'a' && u <= 'z')
		|| (u >= 'A' && u <= 'Z')
		|| is_digit_byte (c)
		|| u == '_'
		|| u == '\''
		|| u >= 0x80
	);
}

/* The end of the name that starts at pos (boolean: a symbol ends it). */
static uint64_t name_end (MlSlice text, uint64_t pos, int32_t stop_at_symbol)
{
	uint64_t end = pos;

	while (
		   end < text.len
		&& is_name_byte (text.data [end])
		&& !(
			   stop_at_symbol
			&& ml_bool_symbol_at (text, end).kind != ML_BSYM_NONE
		)
		&& !(!stop_at_symbol && is_lambda_at (text, end))
	)
	{
		end++;
	}

	return end;
}

/* The classes of a line of the boolean domain. */
static void classify_boolean (uint8_t *cls, MlSlice text)
{
	uint64_t pos   = 0;
	int32_t  first = 1;

	while (pos < text.len)
	{
		MlBoolSymbol sym = ml_bool_symbol_at (text, pos);
		uint64_t     n   = 1;
		MlClass      c   = ML_CLASS_ERROR;

		if (is_blank_byte (text.data [pos]))
		{
			c = ML_CLASS_TEXT;
		}

		else if (sym.kind != ML_BSYM_NONE)
		{
			n = sym.len;
			c = symbol_class (sym.kind);
		}

		else if (text.data [pos] == '0' || text.data [pos] == '1')
		{
			c = ML_CLASS_CONSTANT;
		}

		else if (
			   is_name_byte (text.data [pos])
			&& !is_digit_byte (text.data [pos])
			&& text.data [pos] != '\''
		)
		{
			MlSlice word;

			n    = name_end (text, pos, 1) - pos;
			word = ml_slice_sub (text, pos, n);
			c    = ML_CLASS_NAME;
			c    = ml_choose (
				ml_bool_word (word).kind != ML_BSYM_NONE,
				symbol_class (ml_bool_word (word).kind),
				c
			);
			c    = ml_choose (
				first && is_word (word, "table"),
				ML_CLASS_KEYWORD,
				c
			);
		}

		paint (cls, pos, n, c);
		first = first && c == ML_CLASS_TEXT;
		pos  += n;
	}
}

/* The classes of a line of the lambda domain. */
static void classify_lambda (uint8_t *cls, MlSlice text)
{
	uint64_t pos    = 0;
	int32_t  first  = 1;
	int32_t  binder = 0;

	while (pos < text.len)
	{
		uint64_t n = 1;
		MlClass  c = ML_CLASS_ERROR;

		if (is_blank_byte (text.data [pos]))
		{
			c = ML_CLASS_TEXT;
		}

		else if (is_lambda_at (text, pos))
		{
			n      = ml_choose (text.data [pos] == '\\', 1, 2);
			c      = ML_CLASS_BINDER;
			binder = 1;
		}

		else if (text.data [pos] == '.')
		{
			c      = ml_choose (binder, ML_CLASS_BINDER, ML_CLASS_OPERATOR);
			binder = 0;
		}

		else if (text.data [pos] == '(' || text.data [pos] == ')')
		{
			c = ML_CLASS_PAREN;
		}

		else if (text.data [pos] == '=')
		{
			c = ML_CLASS_OPERATOR;
		}

		else if (is_name_byte (text.data [pos]))
		{
			MlSlice word;
			int32_t digits = 1;

			n    = name_end (text, pos, 0) - pos;
			word = ml_slice_sub (text, pos, n);

			for (uint64_t k = 0; k < n; k++)
			{
				digits = digits && is_digit_byte (word.data [k]);
			}

			c = ML_CLASS_NAME;
			c = ml_choose (digits, ML_CLASS_NUMBER, c);
			c = ml_choose (
				first && is_word (word, "trace"),
				ML_CLASS_KEYWORD,
				c
			);
			c = ml_choose (binder, ML_CLASS_BINDER, c);
		}

		paint (cls, pos, n, c);
		first = first && c == ML_CLASS_TEXT;
		pos  += n;
	}
}

MlResult ml_highlight (
	MlRunVec    *out,
	MlDomain    domain,
	MlSlice     text,
	const MlEnv *env,
	uint64_t    snapshot,
	uint64_t    err_start,
	uint64_t    err_len
)
{
	MlResult r   = ML_OK;
	uint8_t  *cls = NULL;

	if (text.len > 0)
	{
		cls = (uint8_t *)ml_heap_alloc_zero (text.len);
		r   = ml_choose (cls != NULL, ML_OK, ML_ERR_NOMEM);
	}

	if (r == ML_OK && domain == ML_DOMAIN_BOOLEAN)
	{
		classify_boolean (cls, text);
	}

	else if (r == ML_OK && domain == ML_DOMAIN_LAMBDA)
	{
		classify_lambda (cls, text);
	}

	else if (r == ML_OK)
	{
		r = classify_algebra (cls, text, env, snapshot);
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

	ml_heap_free (cls);
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
