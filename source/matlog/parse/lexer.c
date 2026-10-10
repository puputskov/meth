#include <matlog/parse/lexer.h>

#include <matlog/core/utf8.h>

#define CP_SUPERSCRIPT_MINUS 0x207B

static const char *const kind_names [ML_TOK_COUNT] = {
	[ML_TOK_END]         = "end",
	[ML_TOK_NUMBER]      = "number",
	[ML_TOK_NAME]        = "name",
	[ML_TOK_PLUS]        = "+",
	[ML_TOK_MINUS]       = "-",
	[ML_TOK_STAR]        = "*",
	[ML_TOK_SLASH]       = "/",
	[ML_TOK_CARET]       = "^",
	[ML_TOK_BANG]        = "!",
	[ML_TOK_LPAREN]      = "(",
	[ML_TOK_RPAREN]      = ")",
	[ML_TOK_LBRACKET]    = "[",
	[ML_TOK_RBRACKET]    = "]",
	[ML_TOK_COMMA]       = ",",
	[ML_TOK_SEMI]        = ";",
	[ML_TOK_ARROW]       = "->",
	[ML_TOK_EQUALS]      = "=",
	[ML_TOK_SQRT]        = "sqrt",
	[ML_TOK_SUPERSCRIPT] = "superscript",
	[ML_TOK_LT]          = "<",
	[ML_TOK_GT]          = ">",
	[ML_TOK_LE]          = "<=",
	[ML_TOK_GE]          = ">=",
	[ML_TOK_EQEQ]        = "==",
	[ML_TOK_NE]          = "!=",
	[ML_TOK_AND]         = "and",
	[ML_TOK_OR]          = "or",
	[ML_TOK_TO]          = "to",
	[ML_TOK_ERROR]       = "error"
};

const char *ml_token_kind_name (MlTokenKind kind)
{
	return ml_choose (
		(int32_t)kind >= 0 && kind < ML_TOK_COUNT,
		kind_names [kind],
		"unknown"
	);
}

MlSlice ml_token_text (MlSlice source, MlToken token)
{
	return ml_slice_sub (source, token.start, token.len);
}

typedef struct Symbol
{
	uint32_t    cp;
	MlTokenKind kind;
} Symbol;

/*
 * Characters that are a token on their own, in their ASCII and unicode forms.
 */
static const Symbol symbols [] = {
	{ .cp = '+',    .kind = ML_TOK_PLUS },
	{ .cp = '-',    .kind = ML_TOK_MINUS },
	{ .cp = 0x2212, .kind = ML_TOK_MINUS }, /* minus sign */
	{ .cp = '*',    .kind = ML_TOK_STAR },
	{ .cp = 0x00D7, .kind = ML_TOK_STAR },  /* multiplication sign */
	{ .cp = 0x00B7, .kind = ML_TOK_STAR },  /* middle dot */
	{ .cp = 0x22C5, .kind = ML_TOK_STAR },  /* dot operator */
	{ .cp = '/',    .kind = ML_TOK_SLASH },
	{ .cp = 0x00F7, .kind = ML_TOK_SLASH }, /* division sign */
	{ .cp = 0x2215, .kind = ML_TOK_SLASH }, /* division slash */
	{ .cp = '^',    .kind = ML_TOK_CARET },
	{ .cp = '!',    .kind = ML_TOK_BANG },
	{ .cp = '(',    .kind = ML_TOK_LPAREN },
	{ .cp = ')',    .kind = ML_TOK_RPAREN },
	{ .cp = '[',    .kind = ML_TOK_LBRACKET },
	{ .cp = ']',    .kind = ML_TOK_RBRACKET },
	{ .cp = ',',    .kind = ML_TOK_COMMA },
	{ .cp = ';',    .kind = ML_TOK_SEMI },
	{ .cp = '=',    .kind = ML_TOK_EQUALS },
	{ .cp = '<',    .kind = ML_TOK_LT },
	{ .cp = '>',    .kind = ML_TOK_GT },
	{ .cp = 0x2264, .kind = ML_TOK_LE },    /* less than or equal to */
	{ .cp = 0x2265, .kind = ML_TOK_GE },    /* greater than or equal to */
	{ .cp = 0x2260, .kind = ML_TOK_NE },    /* not equal to */
	{ .cp = 0x221A, .kind = ML_TOK_SQRT }   /* square root */
};

/* The value 0 to 9 of a superscript digit, or -1 when cp is not one. */
static int32_t superscript_digit (uint32_t cp)
{
	int32_t digit = -1;

	if (cp == 0x2070)
	{
		digit = 0;
	}

	else if (cp == 0x00B9)
	{
		digit = 1;
	}

	else if (cp == 0x00B2 || cp == 0x00B3)
	{
		digit = (int32_t)(cp - 0x00B0);
	}

	else if (cp >= 0x2074 && cp <= 0x2079)
	{
		digit = (int32_t)(cp - 0x2070);
	}

	return digit;
}

static int32_t is_space (uint32_t cp)
{
	return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n';
}

static int32_t is_digit (uint32_t cp)
{
	return cp >= '0' && cp <= '9';
}

static MlDecoded decode_at (MlSlice source, uint64_t pos)
{
	return ml_utf8_decode (ml_slice_sub (source, pos, source.len - pos));
}

/* End of the run of code points that satisfy test, starting at pos. */
static uint64_t scan_while (
	MlSlice source,
	uint64_t pos,
	int32_t (*test) (uint32_t)
)
{
	uint64_t end = pos;
	int32_t  go  = 1;

	while (go && end < source.len)
	{
		MlDecoded d = decode_at (source, end);

		go = test (d.cp);

		if (go)
		{
			end += d.size;
		}
	}

	return end;
}

static int32_t is_word (uint32_t cp)
{
	return ml_cp_is_word (cp);
}

static int32_t is_superscript_part (uint32_t cp)
{
	return cp == CP_SUPERSCRIPT_MINUS || superscript_digit (cp) >= 0;
}

/*
 * The end of the number that starts at pos: digits, then an optional fraction.
 */
static uint64_t scan_number (MlSlice source, uint64_t pos)
{
	uint64_t end = scan_while (source, pos, is_digit);

	if (
		   end + 1 < source.len
		&& source.data [end] == '.'
		&& source.data [end + 1] >= '0'
		&& source.data [end + 1] <= '9'
	)
	{
		end = scan_while (source, end + 1, is_digit);
	}

	return end;
}

static MlTokenKind symbol_kind (uint32_t cp)
{
	MlTokenKind kind = ML_TOK_ERROR;

	for (uint64_t i = 0; i < ml_count (symbols); i++)
	{
		if (symbols [i].cp == cp)
		{
			kind = symbols [i].kind;
		}
	}

	return kind;
}

/* What the token that starts at pos is, and where it ends. */
static MlToken next_token (MlSlice source, uint64_t pos)
{
	MlDecoded d     = decode_at (source, pos);
	MlToken   token = {
		.kind  = ML_TOK_ERROR,
		.start = pos,
		.len   = d.size
	};

	int32_t starts_number = (
		   is_digit (d.cp)
		|| (
			   d.cp == '.'
			&& pos + 1 < source.len
			&& is_digit (source.data [pos + 1])
		)
	);

	if (starts_number)
	{
		/* ".5" is the dot and its digits; "3.14" is digits, dot, digits */
		uint64_t end = ml_choose (
			d.cp == '.',
			scan_while (source, pos + 1, is_digit),
			scan_number (source, pos)
		);

		token.kind = ML_TOK_NUMBER;
		token.len  = end - pos;
	}

	else if (ml_cp_is_word (d.cp))
	{
		token.kind = ML_TOK_NAME;
		token.len  = scan_while (source, pos, is_word) - pos;
	}

	else if (is_superscript_part (d.cp))
	{
		token.kind = ML_TOK_SUPERSCRIPT;
		token.len  = scan_while (source, pos, is_superscript_part) - pos;
	}

	else
	{
		token.kind = symbol_kind (d.cp);

		/* -> is two characters */
		if (d.cp == '-' && pos + 1 < source.len && source.data [pos + 1] == '>')
		{
			token.kind = ML_TOK_ARROW;
			token.len  = 2;
		}

		/* <= >= == != are two characters */
		else if (
			   pos + 1 < source.len
			&& source.data [pos + 1] == '='
			&& (d.cp == '<' || d.cp == '>' || d.cp == '=' || d.cp == '!')
		)
		{
			token.kind = ml_choose (
				d.cp == '<',
				ML_TOK_LE,
				ml_choose (
					d.cp == '>',
					ML_TOK_GE,
					ml_choose (d.cp == '=', ML_TOK_EQEQ, ML_TOK_NE)
				)
			);
			token.len  = 2;
		}
	}

	return token;
}

MlLexResult ml_lex (MlSlice source)
{
	MlLexResult lexed = {
		.result = ML_OK
	};

	uint64_t pos = 0;

	MlTokenVec_init (&lexed.tokens);

	while (pos < source.len && lexed.result == ML_OK)
	{
		MlDecoded d = decode_at (source, pos);

		if (is_space (d.cp))
		{
			pos += d.size;
		}

		else
		{
			MlToken token = next_token (source, pos);

			lexed.result = MlTokenVec_push (&lexed.tokens, token);
			pos         += token.len;
		}
	}

	if (lexed.result == ML_OK)
	{
		MlToken end = {
			.kind  = ML_TOK_END,
			.start = source.len,
			.len   = 0
		};

		lexed.result = MlTokenVec_push (&lexed.tokens, end);
	}

	return lexed;
}

MlResult ml_superscript_to_ascii (MlStr *out, MlSlice text)
{
	MlResult r   = ml_choose (text.len > 0, ML_OK, ML_ERR_PARSE);
	uint64_t pos = 0;

	while (r == ML_OK && pos < text.len)
	{
		MlDecoded d     = decode_at (text, pos);
		int32_t   digit = superscript_digit (d.cp);

		if (d.cp == CP_SUPERSCRIPT_MINUS && pos == 0)
		{
			r = ml_str_append (out, ml_slice_cstr ("-"));
		}

		else if (digit >= 0)
		{
			r = ml_str_appendf (out, "%d", digit);
		}

		else
		{
			r = ML_ERR_PARSE;
		}

		pos += d.size;
	}

	return r;
}
