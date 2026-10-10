#include <matlog/export/math.h>

#include <matlog/core/utf8.h>
#include <matlog/logic/bool.h>

#include <string.h>

/* What a token of a boolean or a lambda line is set as. */
typedef enum Atom
{
	ATOM_NAME = 0,
	ATOM_NUMBER,
	ATOM_OPERATOR, /* a symbol set as a relation: ∧ ∨ ... */
	ATOM_OPEN,
	ATOM_CLOSE,
	ATOM_KEYWORD,
	ATOM_TEXT
} Atom;

static MlResult put (MlStr *out, const char *text)
{
	return ml_str_append (out, ml_slice_cstr (text));
}

/* An operator: its LaTeX command and its character for MathML. */
typedef struct Symbol
{
	MlBoolSymbolKind kind;
	const char      *latex;
	const char      *mathml;
} Symbol;

static const Symbol symbols [] = {
	{ .kind = ML_BSYM_AND,     .latex = "\\land ",  .mathml = "\xE2\x88\xA7" },
	{ .kind = ML_BSYM_OR,      .latex = "\\lor ",   .mathml = "\xE2\x88\xA8" },
	{ .kind = ML_BSYM_NOT,     .latex = "\\lnot ",  .mathml = "\xC2\xAC"     },
	{ .kind = ML_BSYM_XOR,     .latex = "\\oplus ", .mathml = "\xE2\x8A\x95" },
	{ .kind   = ML_BSYM_IMPL,
	 .latex  = "\\rightarrow ",
	 .mathml = "\xE2\x86\x92"												},
	{ .kind   = ML_BSYM_EQUIV,
	 .latex  = "\\leftrightarrow ",
	 .mathml = "\xE2\x86\x94"												}
};

static const Symbol *symbol_of (MlBoolSymbolKind kind)
{
	const Symbol *found = NULL;

	for (uint64_t i = 0; i < ml_count (symbols); i++)
	{
		if (symbols [i].kind == kind)
		{
			found = &symbols [i];
		}
	}

	return found;
}

/* Greek letters the names of the lines may use, as LaTeX commands. */
typedef struct Greek
{
	const char *utf8;
	const char *latex;
} Greek;

static const Greek greek [] = {
	{ .utf8 = "\xCE\xB1", .latex = "\\alpha " },
	{ .utf8 = "\xCE\xB2", .latex = "\\beta "  },
	{ .utf8 = "\xCE\xB3", .latex = "\\gamma " },
	{ .utf8 = "\xCE\xB4", .latex = "\\delta " },
	{ .utf8 = "\xCE\xB8", .latex = "\\theta " },
	{ .utf8 = "\xCE\xBC", .latex = "\\mu "    },
	{ .utf8 = "\xCF\x80", .latex = "\\pi "    },
	{ .utf8 = "\xCF\x83", .latex = "\\sigma " },
	{ .utf8 = "\xCF\x86", .latex = "\\phi "   },
	{ .utf8 = "\xCF\x89", .latex = "\\omega " }
};

static int32_t is_blank (char c)
{
	return c == ' ' || c == '\t';
}

static int32_t is_name_byte (char c)
{
	uint8_t u = (uint8_t)c;

	return (
		(u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')
		|| (u >= '0' && u <= '9') || u == '_' || u == '\'' || u >= 0x80
	);
}

static int32_t is_lambda_at (MlSlice s, uint64_t pos)
{
	return (
		(pos + 2 <= s.len && memcmp (s.data + pos, "\xCE\xBB", 2) == 0)
		|| (pos < s.len && s.data [pos] == '\\')
	);
}

/* A name or a number, set as one piece. */
static MlResult put_word (MlStr *out, MlMathNotation n, Atom kind, MlSlice word)
{
	MlResult r = ML_OK;

	if (n == ML_MATH_MATHML)
	{
		r = put (
			out,
			ml_choose (
				kind == ATOM_NUMBER,
				"<mn>",
				ml_choose (
					kind == ATOM_KEYWORD,
					"<mi mathvariant=\"normal\">",
					"<mi>"
				)
			)
		);
		r = ml_choose (r == ML_OK, ml_math_escape (out, n, word), r);
		r = ml_choose (
			r == ML_OK,
			put (out, ml_choose (kind == ATOM_NUMBER, "</mn>", "</mi>")),
			r
		);
	}

	else if (kind == ATOM_KEYWORD)
	{
		r = put (out, "\\operatorname{");
		r = ml_choose (r == ML_OK, ml_math_escape (out, n, word), r);
		r = ml_choose (r == ML_OK, put (out, "}"), r);
	}

	else
	{
		MlStr    plain;
		int32_t  found   = 0;
		uint64_t letters = word.len;

		/* primes are not letters: x' is a single letter */
		while (letters > 1 && word.data [letters - 1] == '\'')
		{
			letters--;
		}

		ml_str_init (&plain);

		for (uint64_t g = 0; g < ml_count (greek) && r == ML_OK; g++)
		{
			if (ml_slice_equal (word, ml_slice_cstr (greek [g].utf8)))
			{
				r     = put (out, greek [g].latex);
				found = 1;
			}
		}

		if (!found && r == ML_OK)
		{
			r = ml_math_escape (&plain, n, word);

			/* a name of several letters is set upright */
			if (r == ML_OK && kind == ATOM_NAME && letters > 1)
			{
				r = put (out, "\\mathit{");
				r = ml_choose (
					r == ML_OK,
					ml_str_append (out, ml_str_view (plain)),
					r
				);
				r = ml_choose (r == ML_OK, put (out, "}"), r);
			}

			else if (r == ML_OK)
			{
				r = ml_str_append (out, ml_str_view (plain));
			}
		}

		ml_str_free (&plain);
	}

	return r;
}

/* A piece that is not a word: an operator, a bracket, the dot of a binder. */
static MlResult put_mark (
	MlStr         *out,
	MlMathNotation n,
	const char    *latex,
	const char    *mathml
)
{
	MlResult r = ML_OK;

	if (n == ML_MATH_MATHML)
	{
		r = put (out, "<mo>");
		r = ml_choose (r == ML_OK, put (out, mathml), r);
		r = ml_choose (r == ML_OK, put (out, "</mo>"), r);
	}

	else
	{
		r = put (out, latex);
	}

	return r;
}

/* The end of the word that starts at pos: letters, digits, _ and '. */
static uint64_t word_end (MlSlice s, uint64_t pos, int32_t lambda)
{
	uint64_t end = pos;

	while (end < s.len && is_name_byte (s.data [end])
		   && !(lambda && end > pos && is_lambda_at (s, end))
		   && !(
			   !lambda && end > pos
			   && ml_bool_symbol_at (s, end).kind != ML_BSYM_NONE
		   ))
	{
		end++;
	}

	return end;
}

/* The constant ⊤ or ⊥. */
static MlResult put_constant (MlStr *out, MlMathNotation n, int32_t value)
{
	return put_mark (
		out,
		n,
		ml_choose (value, "\\top ", "\\bot "),
		ml_choose (value, "\xE2\x8A\xA4", "\xE2\x8A\xA5")
	);
}

/*
 * One symbol of the boolean domain: an operator, a constant, a bracket, the
 * = of a definition. closes is set when an operand ends with it.
 */
static MlResult put_symbol (
	MlStr         *out,
	MlMathNotation n,
	MlBoolSymbol   sym,
	int32_t       *closes
)
{
	const Symbol *op = symbol_of (sym.kind);
	MlResult      r  = ML_OK;

	*closes = sym.kind == ML_BSYM_CONST || sym.kind == ML_BSYM_RPAREN;

	if (op != NULL)
	{
		r = put_mark (out, n, op->latex, op->mathml);
	}

	else if (sym.kind == ML_BSYM_CONST)
	{
		r = put_constant (out, n, sym.value);
	}

	else if (sym.kind == ML_BSYM_LPAREN)
	{
		r = put_mark (out, n, "(", "(");
	}

	else if (sym.kind == ML_BSYM_RPAREN)
	{
		r = put_mark (out, n, ")", ")");
	}

	else
	{
		r = put_mark (out, n, "=", "=");
	}

	return r;
}

/* A name, a number or a keyword; spaced from the operand before it. */
static MlResult put_atom (
	MlStr         *out,
	MlMathNotation n,
	MlSlice        word,
	int32_t        lambda,
	int32_t        first,
	int32_t        operand,
	int32_t       *closes
)
{
	MlResult r      = ML_OK;
	int32_t  digits = 1;
	int32_t  keyword
		= (first
		   && ml_slice_equal (
			   word,
			   ml_slice_cstr (ml_choose (lambda, "trace", "table"))
		   ));
	Atom kind;

	for (uint64_t k = 0; k < word.len; k++)
	{
		digits = digits && word.data [k] >= '0' && word.data [k] <= '9';
	}

	kind = ml_choose (
		keyword,
		ATOM_KEYWORD,
		ml_choose (digits, ATOM_NUMBER, ATOM_NAME)
	);
	*closes = !keyword;

	if (operand && n == ML_MATH_LATEX)
	{
		r = put (out, "\\,");
	}

	r = ml_choose (r == ML_OK, put_word (out, n, kind, word), r);

	if (keyword && n == ML_MATH_LATEX && r == ML_OK)
	{
		r = put (out, "\\;");
	}

	return r;
}

MlResult ml_logic_typeset (
	MlStr         *out,
	MlMathNotation n,
	int32_t        lambda,
	MlSlice        source
)
{
	MlResult r     = ML_OK;
	uint64_t pos   = 0;
	int32_t  first = 1;
	int32_t  operand
		= 0; /* the piece before ends an operand: f x is f applied */

	r = ml_choose (n == ML_MATH_MATHML, put (out, "<mrow>"), ML_OK);

	while (pos < source.len && r == ML_OK)
	{
		char         c   = source.data [pos];
		MlBoolSymbol sym = ml_choose (
			lambda,
			ml_bool_symbol_at (ml_slice ("", 0), 0),
			ml_bool_symbol_at (source, pos)
		);
		uint64_t step   = 1;
		int32_t  closes = 0;

		if (is_blank (c))
		{
			r = ML_OK;
		}

		else if (lambda && is_lambda_at (source, pos))
		{
			step = ml_choose (c == '\\', 1, 2);
			r    = ml_choose (
				operand && n == ML_MATH_LATEX,
				put (out, "\\,"),
				ML_OK
			);
			r = ml_choose (
				r == ML_OK,
				put_mark (out, n, "\\lambda ", "\xCE\xBB"),
				r
			);
		}

		else if (lambda && c == '.')
		{
			r = put_mark (out, n, ".\\,", ".");
		}

		else if (lambda && (c == '(' || c == ')'))
		{
			r = ml_choose (
				c == '(' && operand && n == ML_MATH_LATEX,
				put (out, "\\,"),
				ML_OK
			);
			r = ml_choose (
				r == ML_OK,
				put_mark (
					out,
					n,
					ml_choose (c == '(', "(", ")"),
					ml_choose (c == '(', "(", ")")
				),
				r
			);
			closes = c == ')';
		}

		else if (lambda && c == '=')
		{
			r = put_mark (out, n, "=", "=");
		}

		else if (!lambda && sym.kind != ML_BSYM_NONE)
		{
			step = sym.len;
			r    = put_symbol (out, n, sym, &closes);
		}

		else if (is_name_byte (c))
		{
			MlSlice word = ml_slice_sub (
				source,
				pos,
				word_end (source, pos, lambda) - pos
			);

			step = word.len;

			if (!lambda && ml_bool_word (word).kind != ML_BSYM_NONE)
			{
				r = put_symbol (out, n, ml_bool_word (word), &closes);
			}

			else
			{
				r = put_atom (out, n, word, lambda, first, operand, &closes);
			}
		}

		else
		{
			/* anything else is set as it is */
			MlDecoded d
				= ml_utf8_decode (ml_slice_sub (source, pos, source.len - pos));

			step = ml_choose (d.size > 0, d.size, 1);
			r    = put (
				out,
				ml_choose (n == ML_MATH_MATHML, "<mtext>", "\\text{")
			);
			r = ml_choose (
				r == ML_OK,
				ml_math_escape (out, n, ml_slice_sub (source, pos, step)),
				r
			);
			r = ml_choose (
				r == ML_OK,
				put (out, ml_choose (n == ML_MATH_MATHML, "</mtext>", "}")),
				r
			);
		}

		if (!is_blank (c))
		{
			first   = 0;
			operand = closes;
		}

		pos += step;
	}

	return ml_choose (
		r == ML_OK && n == ML_MATH_MATHML,
		put (out, "</mrow>"),
		r
	);
}
