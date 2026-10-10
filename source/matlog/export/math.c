#include <matlog/export/math.h>
#include <matlog/parse/words.h>

#include <matlog/core/utf8.h>
#include <matlog/core/vec.h>
#include <matlog/parse/lexer.h>
#include <matlog/parse/parser.h>

#include <string.h>

/* The pieces a formula is made of, written differently by each notation. */
typedef enum Tok
{
	T_GOPEN = 0,  /* a group: {  <mrow> */
	T_GCLOSE,
	T_LP,         /* round brackets that grow with their content */
	T_RP,
	T_LB,         /* square brackets */
	T_RB,
	T_PLUS,
	T_MINUS,
	T_CDOT,       /* a visible product */
	T_SPACE,      /* factors side by side */
	T_COMMA,
	T_EQ,
	T_FRAC_OPEN,  /* \frac  <mfrac>, then two groups */
	T_FRAC_CLOSE,
	T_SQRT_OPEN,
	T_SQRT_CLOSE,
	T_SUP_OPEN,   /* base group, power group */
	T_SUP_MID,
	T_SUP_CLOSE,
	T_BANG,
	T_TO,
	T_MAT_OPEN,
	T_MAT_CLOSE,
	T_ROW_OPEN,
	T_ROW_CLOSE,
	T_ROW_SEP,
	T_CELL_OPEN,
	T_CELL_CLOSE,
	T_CELL_SEP,
	T_APPROX,
	T_ABS_OPEN,
	T_ABS_CLOSE,
	T_LT,         /* the comparisons and the words and, or */
	T_GT,
	T_LE,
	T_GE,
	T_EQEQ,
	T_NE,
	T_AND,
	T_OR,
	T_SEMI,       /* ; between statements and the word let */
	T_LET,
	T_MAPSTO,     /* x -> body */
	T_COUNT
} Tok;

static const char *const latex_tok [T_COUNT] = {
	[T_GOPEN]      = "{",
	[T_GCLOSE]     = "}",
	[T_LP]         = "\\left(",
	[T_RP]         = "\\right)",
	[T_LB]         = "\\left[",
	[T_RB]         = "\\right]",
	[T_PLUS]       = "+",
	[T_MINUS]      = "-",
	[T_CDOT]       = "\\cdot ",
	[T_SPACE]      = " ",
	[T_COMMA]      = ",\\,",
	[T_EQ]         = "=",
	[T_FRAC_OPEN]  = "\\frac",
	[T_FRAC_CLOSE] = "",
	[T_SQRT_OPEN]  = "\\sqrt{",
	[T_SQRT_CLOSE] = "}",
	[T_SUP_OPEN]   = "",
	[T_SUP_MID]    = "^",
	[T_SUP_CLOSE]  = "",
	[T_BANG]       = "!",
	[T_TO]         = "\\;\\mathrm{to}\\;",
	[T_MAT_OPEN]   = "\\begin{pmatrix} ",
	[T_MAT_CLOSE]  = " \\end{pmatrix}",
	[T_ROW_OPEN]   = "",
	[T_ROW_CLOSE]  = "",
	[T_ROW_SEP]    = " \\\\ ",
	[T_CELL_OPEN]  = "",
	[T_CELL_CLOSE] = "",
	[T_CELL_SEP]   = " & ",
	[T_APPROX]     = "\\approx ",
	[T_ABS_OPEN]   = "\\left|",
	[T_ABS_CLOSE]  = "\\right|",
	[T_LT]         = "<",
	[T_GT]         = ">",
	[T_LE]         = "\\le ",
	[T_GE]         = "\\ge ",
	[T_EQEQ]       = "=",
	[T_NE]         = "\\ne ",
	[T_AND]        = "\\;\\mathrm{and}\\;",
	[T_OR]         = "\\;\\mathrm{or}\\;",
	[T_SEMI]       = ";\\;",
	[T_LET]        = "\\mathrm{let}\\;",
	[T_MAPSTO]     = "\\mapsto "
};

static const char *const mathml_tok [T_COUNT] = {
	[T_GOPEN]      = "<mrow>",
	[T_GCLOSE]     = "</mrow>",
	[T_LP]         = "<mo>(</mo>",
	[T_RP]         = "<mo>)</mo>",
	[T_LB]         = "<mo>[</mo>",
	[T_RB]         = "<mo>]</mo>",
	[T_PLUS]       = "<mo>+</mo>",
	[T_MINUS]      = "<mo>\xE2\x88\x92</mo>",
	[T_CDOT]       = "<mo>\xE2\x8B\x85</mo>",
	[T_SPACE]      = "<mo>\xE2\x81\xA2</mo>",
	[T_COMMA]      = "<mo>,</mo>",
	[T_EQ]         = "<mo>=</mo>",
	[T_FRAC_OPEN]  = "<mfrac>",
	[T_FRAC_CLOSE] = "</mfrac>",
	[T_SQRT_OPEN]  = "<msqrt>",
	[T_SQRT_CLOSE] = "</msqrt>",
	[T_SUP_OPEN]   = "<msup>",
	[T_SUP_MID]    = "",
	[T_SUP_CLOSE]  = "</msup>",
	[T_BANG]       = "<mo>!</mo>",
	[T_TO]         = "<mspace width=\"0.5em\"/><mi>to</mi>"
	                 "<mspace width=\"0.5em\"/>",
	[T_MAT_OPEN]   = "<mrow><mo>(</mo><mtable>",
	[T_MAT_CLOSE]  = "</mtable><mo>)</mo></mrow>",
	[T_ROW_OPEN]   = "<mtr>",
	[T_ROW_CLOSE]  = "</mtr>",
	[T_ROW_SEP]    = "",
	[T_CELL_OPEN]  = "<mtd>",
	[T_CELL_CLOSE] = "</mtd>",
	[T_CELL_SEP]   = "",
	[T_APPROX]     = "<mo>\xE2\x89\x88</mo>",
	[T_ABS_OPEN]   = "<mo>|</mo>",
	[T_ABS_CLOSE]  = "<mo>|</mo>",
	[T_LT]         = "<mo>&lt;</mo>",
	[T_GT]         = "<mo>&gt;</mo>",
	[T_LE]         = "<mo>\xE2\x89\xA4</mo>",
	[T_GE]         = "<mo>\xE2\x89\xA5</mo>",
	[T_EQEQ]       = "<mo>=</mo>",
	[T_NE]         = "<mo>\xE2\x89\xA0</mo>",
	[T_AND]        = "<mspace width=\"0.5em\"/><mi>and</mi>"
	                 "<mspace width=\"0.5em\"/>",
	[T_OR]         = "<mspace width=\"0.5em\"/><mi>or</mi>"
	                 "<mspace width=\"0.5em\"/>",
	[T_SEMI]       = "<mo>;</mo><mspace width=\"0.5em\"/>",
	[T_LET]        = "<mi>let</mi><mspace width=\"0.5em\"/>",
	[T_MAPSTO]     = "<mo>\xE2\x86\xA6</mo>"
};

static MlResult put_tok (MlStr *out, MlMathNotation n, Tok t)
{
	return ml_str_append (
		out,
		ml_slice_cstr (
			ml_choose (n == ML_MATH_LATEX, latex_tok [t], mathml_tok [t])
		)
	);
}

static MlResult put (MlStr *out, const char *text)
{
	return ml_str_append (out, ml_slice_cstr (text));
}

/* ---- escaping ----------------------------------------------------------- */

MlResult ml_math_escape (MlStr *out, MlMathNotation n, MlSlice text)
{
	MlResult r = ML_OK;

	for (uint64_t i = 0; i < text.len && r == ML_OK; i++)
	{
		char        c    = text.data [i];
		const char *with = NULL;

		if (n == ML_MATH_LATEX)
		{
			with = ml_choose (
				c == '\\',
				"\\textbackslash{}",
				ml_choose (
					c == '~',
					"\\textasciitilde{}",
					ml_choose (c == '^', "\\textasciicircum{}", NULL)
				)
			);

			if (with == NULL && strchr ("&%$#_{}", c) != NULL)
			{
				r = put (out, "\\");
			}
		}

		else
		{
			with = ml_choose (
				c == '&',
				"&amp;",
				ml_choose (
					c == '<',
					"&lt;",
					ml_choose (c == '>', "&gt;", NULL)
				)
			);
		}

		if (r == ML_OK && with != NULL)
		{
			r = put (out, with);
		}

		else if (r == ML_OK)
		{
			r = ml_str_append (out, ml_slice_sub (text, i, 1));
		}
	}

	return r;
}

/* ---- names -------------------------------------------------------------- */

typedef struct Greek
{
	const char *name;
	const char *latex;
	const char *utf8;
} Greek;

static const Greek greek [] = {
	{ .name = "alpha",   .latex = "\\alpha ",   .utf8 = "\xCE\xB1" },
	{ .name = "beta",    .latex = "\\beta ",    .utf8 = "\xCE\xB2" },
	{ .name = "gamma",   .latex = "\\gamma ",   .utf8 = "\xCE\xB3" },
	{ .name = "delta",   .latex = "\\delta ",   .utf8 = "\xCE\xB4" },
	{ .name = "epsilon", .latex = "\\epsilon ", .utf8 = "\xCE\xB5" },
	{ .name = "zeta",    .latex = "\\zeta ",    .utf8 = "\xCE\xB6" },
	{ .name = "eta",     .latex = "\\eta ",     .utf8 = "\xCE\xB7" },
	{ .name = "theta",   .latex = "\\theta ",   .utf8 = "\xCE\xB8" },
	{ .name = "iota",    .latex = "\\iota ",    .utf8 = "\xCE\xB9" },
	{ .name = "kappa",   .latex = "\\kappa ",   .utf8 = "\xCE\xBA" },
	{ .name = "lambda",  .latex = "\\lambda ",  .utf8 = "\xCE\xBB" },
	{ .name = "mu",      .latex = "\\mu ",      .utf8 = "\xCE\xBC" },
	{ .name = "nu",      .latex = "\\nu ",      .utf8 = "\xCE\xBD" },
	{ .name = "xi",      .latex = "\\xi ",      .utf8 = "\xCE\xBE" },
	{ .name = "pi",      .latex = "\\pi ",      .utf8 = "\xCF\x80" },
	{ .name = "rho",     .latex = "\\rho ",     .utf8 = "\xCF\x81" },
	{ .name = "sigma",   .latex = "\\sigma ",   .utf8 = "\xCF\x83" },
	{ .name = "tau",     .latex = "\\tau ",     .utf8 = "\xCF\x84" },
	{ .name = "upsilon", .latex = "\\upsilon ", .utf8 = "\xCF\x85" },
	{ .name = "phi",     .latex = "\\phi ",     .utf8 = "\xCF\x86" },
	{ .name = "chi",     .latex = "\\chi ",     .utf8 = "\xCF\x87" },
	{ .name = "psi",     .latex = "\\psi ",     .utf8 = "\xCF\x88" },
	{ .name = "omega",   .latex = "\\omega ",   .utf8 = "\xCF\x89" },
	{ .name = "Gamma",   .latex = "\\Gamma ",   .utf8 = "\xCE\x93" },
	{ .name = "Delta",   .latex = "\\Delta ",   .utf8 = "\xCE\x94" },
	{ .name = "Theta",   .latex = "\\Theta ",   .utf8 = "\xCE\x98" },
	{ .name = "Lambda",  .latex = "\\Lambda ",  .utf8 = "\xCE\x9B" },
	{ .name = "Xi",      .latex = "\\Xi ",      .utf8 = "\xCE\x9E" },
	{ .name = "Pi",      .latex = "\\Pi ",      .utf8 = "\xCE\xA0" },
	{ .name = "Sigma",   .latex = "\\Sigma ",   .utf8 = "\xCE\xA3" },
	{ .name = "Phi",     .latex = "\\Phi ",     .utf8 = "\xCE\xA6" },
	{ .name = "Psi",     .latex = "\\Psi ",     .utf8 = "\xCE\xA8" },
	{ .name = "Omega",   .latex = "\\Omega ",   .utf8 = "\xCE\xA9" },
	{ .name = "ohm",     .latex = "\\Omega ",   .utf8 = "\xCE\xA9" },
	{ .name = "micro",   .latex = "\\mu ",      .utf8 = "\xCE\xBC" },
	/* the micro sign and the ohm sign are letters of their own in unicode */
	{ .name = "\xC2\xB5", .latex = "\\mu ", .utf8 = "\xCE\xBC" },
	{ .name = "\xE2\x84\xA6", .latex = "\\Omega ", .utf8 = "\xCE\xA9" }
};

/* The Greek letter a name stands for (by name or written as the letter). */
static const Greek *find_greek (MlSlice name)
{
	const Greek *found = NULL;

	for (uint64_t i = 0; i < ml_count (greek) && found == NULL; i++)
	{
		if (
			   ml_slice_equal (name, ml_slice_cstr (greek [i].name))
			|| ml_slice_equal (name, ml_slice_cstr (greek [i].utf8))
		)
		{
			found = &greek [i];
		}
	}

	return found;
}

/* Functions that LaTeX has a command for. */
static const char *const latex_functions [] = {
	"sin", "cos", "tan", "ln", "exp"
};

static uint64_t code_points (MlSlice text)
{
	uint64_t count = 0;
	uint64_t pos   = 0;

	while (pos < text.len)
	{
		pos = ml_utf8_next (text, pos);
		count++;
	}

	return count;
}

/* A name without a subscript: a letter, a Greek letter, a word or a function.
 */
static MlResult name_core (
	MlStr          *out,
	MlMathNotation n,
	MlSlice        name,
	int32_t        func
)
{
	MlResult     r  = ML_OK;
	const Greek *g  = find_greek (name);
	int32_t      known = 0;

	for (uint64_t i = 0; i < ml_count (latex_functions); i++)
	{
		known = known
				|| ml_slice_equal (name, ml_slice_cstr (latex_functions [i]));
	}

	if (g != NULL)
	{
		if (n == ML_MATH_LATEX)
		{
			r = put (out, g->latex);
		}

		else
		{
			r = put (out, "<mi>");

			if (r == ML_OK)
			{
				r = put (out, g->utf8);
			}

			if (r == ML_OK)
			{
				r = put (out, "</mi>");
			}
		}
	}

	else if (n == ML_MATH_LATEX && func && known)
	{
		r = put (out, "\\");

		if (r == ML_OK)
		{
			r = ml_str_append (out, name);
		}

		if (r == ML_OK)
		{
			r = put (out, " ");
		}
	}

	else if (n == ML_MATH_LATEX)
	{
		int32_t word = code_points (name) > 1;

		r = put (
			out,
			ml_choose (
				word && func,
				"\\operatorname{",
				ml_choose (word, "\\mathrm{", "")
			)
		);

		if (r == ML_OK)
		{
			r = ml_math_escape (out, n, name);
		}

		if (r == ML_OK && word)
		{
			r = put (out, "}");
		}
	}

	else
	{
		r = put (out, "<mi>");

		if (r == ML_OK)
		{
			r = ml_math_escape (out, n, name);
		}

		if (r == ML_OK)
		{
			r = put (out, "</mi>");
		}
	}

	return r;
}

/* A name token: x, kg, alpha, x_1, sin. */
static MlResult put_name (
	MlStr          *out,
	MlMathNotation n,
	MlSlice        name,
	int32_t        func
)
{
	MlResult r     = ML_OK;
	uint64_t under = 0;

	while (under < name.len && name.data [under] != '_')
	{
		under++;
	}

	if (under > 0 && under + 1 < name.len)
	{
		MlSlice base = ml_slice_sub (name, 0, under);
		MlSlice sub  = ml_slice_sub (name, under + 1, name.len - under - 1);
		int32_t digits = sub.data [0] >= '0' && sub.data [0] <= '9';

		if (n == ML_MATH_LATEX)
		{
			r = name_core (out, n, base, func);

			if (r == ML_OK)
			{
				r = put (out, "_{");
			}

			if (r == ML_OK)
			{
				r = ml_math_escape (out, n, sub);
			}

			if (r == ML_OK)
			{
				r = put (out, "}");
			}
		}

		else
		{
			r = put (out, "<msub>");

			if (r == ML_OK)
			{
				r = name_core (out, n, base, func);
			}

			if (r == ML_OK)
			{
				r = put (out, ml_choose (digits, "<mn>", "<mi>"));
			}

			if (r == ML_OK)
			{
				r = ml_math_escape (out, n, sub);
			}

			if (r == ML_OK)
			{
				r = put (
					out,
					ml_choose (digits, "</mn></msub>", "</mi></msub>")
				);
			}
		}
	}

	else
	{
		r = name_core (out, n, name, func);
	}

	return r;
}

static MlResult put_number (MlStr *out, MlMathNotation n, MlSlice text)
{
	MlResult r = ML_OK;

	if (n == ML_MATH_MATHML)
	{
		r = put (out, "<mn>");
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, text);
	}

	if (r == ML_OK && n == ML_MATH_MATHML)
	{
		r = put (out, "</mn>");
	}

	return r;
}

/* ---- the walk ----------------------------------------------------------- */

typedef enum OpKind
{
	OP_NODE = 0, /* write a node, in brackets when parens is set */
	OP_FUNC,     /* write a node that is a function name */
	OP_TOK
} OpKind;

typedef struct Op
{
	OpKind   what;
	uint64_t node;
	Tok      tok;
	int32_t  parens;
} Op;

ML_VEC_DEFINE (OpVec, Op)

static Op op_node (uint64_t node, int32_t parens)
{
	Op o = {
		.what   = OP_NODE,
		.node   = node,
		.tok    = T_GOPEN,
		.parens = parens
	};

	return o;
}

static Op op_tok (Tok t)
{
	Op o = {
		.what   = OP_TOK,
		.node   = 0,
		.tok    = t,
		.parens = 0
	};

	return o;
}

static int32_t is_sum (MlNodeKind k)
{
	return k == ML_NODE_ADD || k == ML_NODE_SUB;
}

static int32_t is_comparison (MlNodeKind k)
{
	return k >= ML_NODE_LT && k <= ML_NODE_NE;
}

static int32_t is_logic (MlNodeKind k)
{
	return k == ML_NODE_AND || k == ML_NODE_OR;
}

/* Does the child c of a node p need brackets (right: it is the second one)? */
static int32_t wants_parens (MlNodeKind p, MlNodeKind c, int32_t right)
{
	int32_t wrap = 0;

	if (is_comparison (p) || is_logic (p))
	{
		/* a comparison inside a comparison, or an or inside an and */
		wrap = (
			   (is_comparison (p) && (is_comparison (c) || is_logic (c)))
			|| (p == ML_NODE_AND && c == ML_NODE_OR)
		);
	}

	else if (is_comparison (c) || is_logic (c))
	{
		wrap = 1;
	}

	else if (p == ML_NODE_SUB)
	{
		wrap = right && is_sum (c);
	}

	else if (p == ML_NODE_MUL)
	{
		wrap = is_sum (c) || (right && c == ML_NODE_NEG);
	}

	else if (p == ML_NODE_NEG)
	{
		wrap = is_sum (c);
	}

	else if (p == ML_NODE_POW && !right)
	{
		wrap = (
			   is_sum (c)
			|| c == ML_NODE_MUL
			|| c == ML_NODE_DIV
			|| c == ML_NODE_NEG
			|| c == ML_NODE_POW
		);
	}

	else if (p == ML_NODE_FACT)
	{
		wrap = (
			   is_sum (c)
			|| c == ML_NODE_MUL
			|| c == ML_NODE_DIV
			|| c == ML_NODE_NEG
			|| c == ML_NODE_POW
		);
	}

	return wrap;
}

/* Does the left end of the node, as written, start with a number? */
static int32_t starts_with_number (const MlAst *ast, uint64_t node)
{
	uint64_t at = node;
	int32_t  go = 1;

	while (go)
	{
		MlNodeKind k = ml_ast_node (ast, at).kind;

		go = (
			   k == ML_NODE_MUL
			|| k == ML_NODE_POW
			|| k == ML_NODE_FACT
			|| is_sum (k)
		);

		if (go)
		{
			at = ml_ast_node (ast, at).first;
		}
	}

	return ml_ast_node (ast, at).kind == ML_NODE_NUMBER;
}

/* Is a list a matrix: lists of the same length, at least one? */
static int32_t is_matrix (const MlAst *ast, uint64_t list)
{
	uint64_t rows = ml_ast_child_count (ast, list);
	uint64_t cols = ml_ast_child_count (ast, ml_ast_child (ast, list, 0));
	int32_t  ok   = rows > 0 && cols > 0;

	for (uint64_t i = 0; i < rows && ok; i++)
	{
		uint64_t row = ml_ast_child (ast, list, i);

		ok = (
			   ml_ast_node (ast, row).kind == ML_NODE_LIST
			&& ml_ast_child_count (ast, row) == cols
		);
	}

	return ok;
}

/*
 * The pieces of a node, in the order they are written. Leaves are written at
 * once and give no pieces.
 */
static MlResult expand (
	OpVec          *seq,
	MlStr          *out,
	MlMathNotation n,
	MlSlice        source,
	const MlAst    *ast,
	uint64_t       at
)
{
	MlResult r    = ML_OK;
	MlNode   node = ml_ast_node (ast, at);
	uint64_t a    = node.first;
	uint64_t b    = ml_ast_node (ast, a).next;
	MlNodeKind ak = ml_ast_node (ast, a).kind;
	MlNodeKind bk = ml_ast_node (ast, b).kind;

	if (node.kind == ML_NODE_NUMBER)
	{
		r = put_number (out, n, ml_ast_text (ast, source, at));
	}

	else if (node.kind == ML_NODE_NAME)
	{
		r = put_name (out, n, ml_ast_text (ast, source, at), 0);
	}

	else if (node.kind == ML_NODE_SUPERSCRIPT)
	{
		MlStr digits;

		ml_str_init (&digits);
		r = ml_superscript_to_ascii (&digits, ml_ast_text (ast, source, at));

		if (r == ML_OK)
		{
			r = put_number (out, n, ml_str_view (digits));
		}

		ml_str_free (&digits);
	}

	else if (node.kind == ML_NODE_NEG)
	{
		r = OpVec_push (seq, op_tok (T_MINUS));
		r = ml_choose (
			r == ML_OK,
			OpVec_push (seq, op_node (a, wants_parens (node.kind, ak, 0))),
			r
		);
	}

	else if (is_sum (node.kind) || node.kind == ML_NODE_TO)
	{
		Tok t = ml_choose (
			node.kind == ML_NODE_ADD,
			T_PLUS,
			ml_choose (node.kind == ML_NODE_SUB, T_MINUS, T_TO)
		);

		r = OpVec_push (seq, op_node (a, 0));

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (t));
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_node (b, wants_parens (node.kind, bk, 1)));
		}
	}

	else if (node.kind == ML_NODE_INDEX)
	{
		r = OpVec_push (seq, op_node (a, 0));

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (T_LB));
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_node (b, 0));
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (T_RB));
		}
	}

	else if (node.kind == ML_NODE_LAMBDA)
	{
		int32_t several = ml_ast_child_count (ast, a) > 1;

		if (several)
		{
			r = OpVec_push (seq, op_tok (T_LP));
		}

		for (
			uint64_t c = ml_ast_node (ast, a).first;
			c != 0 && r == ML_OK;
			c = ml_ast_node (ast, c).next
		)
		{
			r = OpVec_push (seq, op_node (c, 0));

			if (r == ML_OK && ml_ast_node (ast, c).next != 0)
			{
				r = OpVec_push (seq, op_tok (T_COMMA));
			}
		}

		if (r == ML_OK && several)
		{
			r = OpVec_push (seq, op_tok (T_RP));
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (T_MAPSTO));
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_node (b, 0));
		}
	}

	else if (node.kind == ML_NODE_SEQ)
	{
		for (
			uint64_t c = a;
			c != 0 && r == ML_OK;
			c = ml_ast_node (ast, c).next
		)
		{
			r = OpVec_push (seq, op_node (c, 0));

			if (r == ML_OK && ml_ast_node (ast, c).next != 0)
			{
				r = OpVec_push (seq, op_tok (T_SEMI));
			}
		}
	}

	else if (node.kind == ML_NODE_LET)
	{
		r = OpVec_push (seq, op_tok (T_LET));

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_node (a, 0));
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (T_EQ));
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_node (b, 0));
		}
	}

	else if (is_comparison (node.kind) || is_logic (node.kind))
	{
		/* the tokens T_LT ... T_OR in the order of the nodes */
		Tok t = (Tok)(T_LT + (node.kind - ML_NODE_LT));

		r = OpVec_push (seq, op_node (a, wants_parens (node.kind, ak, 0)));

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (t));
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_node (b, wants_parens (node.kind, bk, 1)));
		}
	}

	else if (node.kind == ML_NODE_MUL)
	{
		r = OpVec_push (seq, op_node (a, wants_parens (node.kind, ak, 0)));

		if (r == ML_OK)
		{
			r = OpVec_push (
				seq,
				op_tok (ml_choose (starts_with_number (ast, b), T_CDOT, T_SPACE)
				)
			);
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_node (b, wants_parens (node.kind, bk, 1)));
		}
	}

	else if (node.kind == ML_NODE_DIV)
	{
		Op ops [8] = {
			op_tok (T_FRAC_OPEN), op_tok (T_GOPEN), op_node (a, 0),
			op_tok (T_GCLOSE), op_tok (T_GOPEN), op_node (b, 0),
			op_tok (T_GCLOSE), op_tok (T_FRAC_CLOSE)
		};

		for (uint64_t i = 0; i < ml_count (ops) && r == ML_OK; i++)
		{
			r = OpVec_push (seq, ops [i]);
		}
	}

	else if (node.kind == ML_NODE_POW)
	{
		Op ops [9] = {
			op_tok (T_SUP_OPEN), op_tok (T_GOPEN),
			op_node (a, wants_parens (node.kind, ak, 0)),
			op_tok (T_GCLOSE), op_tok (T_SUP_MID), op_tok (T_GOPEN),
			op_node (b, 0), op_tok (T_GCLOSE), op_tok (T_SUP_CLOSE)
		};

		for (uint64_t i = 0; i < ml_count (ops) && r == ML_OK; i++)
		{
			r = OpVec_push (seq, ops [i]);
		}
	}

	else if (node.kind == ML_NODE_FACT)
	{
		r = OpVec_push (seq, op_node (a, wants_parens (node.kind, ak, 0)));
		r = ml_choose (r == ML_OK, OpVec_push (seq, op_tok (T_BANG)), r);
	}

	else if (node.kind == ML_NODE_SQRT)
	{
		r = OpVec_push (seq, op_tok (T_SQRT_OPEN));

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_node (a, 0));
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (T_SQRT_CLOSE));
		}
	}

	else if (
		   node.kind == ML_NODE_CALL
		&& ml_ast_child_count (ast, at) == 2
		&& ml_slice_equal (ml_ast_text (ast, source, a), ML_WORD_NAME (STEPS))
	)
	{
		/* steps (e) is e: the call only asks for the steps of the line */
		r = OpVec_push (seq, op_node (b, 0));
	}

	else if (node.kind == ML_NODE_CALL && ml_ast_child_count (ast, at) == 2
			 && (ml_slice_equal (
					 ml_ast_text (ast, source, a),
					 ML_WORD_NAME (SQRT)
				 )
				 || ml_slice_equal (
					 ml_ast_text (ast, source, a),
					 ML_WORD_NAME (ABS)
				 )))
	{
		/* sqrt(x) is a root and abs(x) is |x|, as √x would be */
		int32_t root = ml_slice_equal (
			ml_ast_text (ast, source, a),
			ML_WORD_NAME (SQRT)
		);

		r = OpVec_push (
			seq,
			op_tok (ml_choose (root, T_SQRT_OPEN, T_ABS_OPEN))
		);

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_node (b, 0));
		}

		if (r == ML_OK)
		{
			r = OpVec_push (
				seq,
				op_tok (ml_choose (root, T_SQRT_CLOSE, T_ABS_CLOSE))
			);
		}
	}

	else if (node.kind == ML_NODE_CALL || node.kind == ML_NODE_FUNC_DEF)
	{
		/* the name, then the arguments (or the parameters) in brackets */
		uint64_t first = ml_choose (
			node.kind == ML_NODE_CALL,
			ml_ast_node (ast, a).next,
			ml_ast_node (ast, b).first
		);
		Op name = {
			.what   = OP_FUNC,
			.node   = a,
			.tok    = T_GOPEN,
			.parens = 0
		};

		r = OpVec_push (seq, name);

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (T_LP));
		}

		for (uint64_t c = first; c != 0 && r == ML_OK;
			 c          = ml_ast_node (ast, c).next)
		{
			r = OpVec_push (seq, op_node (c, 0));

			if (r == ML_OK && ml_ast_node (ast, c).next != 0)
			{
				r = OpVec_push (seq, op_tok (T_COMMA));
			}
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (T_RP));
		}

		if (r == ML_OK && node.kind == ML_NODE_FUNC_DEF)
		{
			r = OpVec_push (seq, op_tok (T_EQ));
		}

		if (r == ML_OK && node.kind == ML_NODE_FUNC_DEF)
		{
			r = OpVec_push (seq, op_node (ml_ast_node (ast, b).next, 0));
		}
	}

	else if (node.kind == ML_NODE_ASSIGN)
	{
		r = OpVec_push (seq, op_node (a, 0));

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (T_EQ));
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_node (b, 0));
		}
	}

	else if (node.kind == ML_NODE_LIST && is_matrix (ast, at))
	{
		r = OpVec_push (seq, op_tok (T_MAT_OPEN));

		for (uint64_t row = a; row != 0 && r == ML_OK;
			 row          = ml_ast_node (ast, row).next)
		{
			r = OpVec_push (seq, op_tok (T_ROW_OPEN));

			for (
				uint64_t cell = ml_ast_node (ast, row).first;
				cell != 0 && r == ML_OK;
				cell = ml_ast_node (ast, cell).next
			)
			{
				Op ops [3] = { op_tok (T_CELL_OPEN),
							   op_node (cell, 0),
							   op_tok (T_CELL_CLOSE) };

				for (uint64_t i = 0; i < ml_count (ops) && r == ML_OK; i++)
				{
					r = OpVec_push (seq, ops [i]);
				}

				if (r == ML_OK && ml_ast_node (ast, cell).next != 0)
				{
					r = OpVec_push (seq, op_tok (T_CELL_SEP));
				}
			}

			if (r == ML_OK)
			{
				r = OpVec_push (seq, op_tok (T_ROW_CLOSE));
			}

			if (r == ML_OK && ml_ast_node (ast, row).next != 0)
			{
				r = OpVec_push (seq, op_tok (T_ROW_SEP));
			}
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (T_MAT_CLOSE));
		}
	}

	else if (node.kind == ML_NODE_LIST)
	{
		r = OpVec_push (seq, op_tok (T_LB));

		for (uint64_t c = a; c != 0 && r == ML_OK;
			 c          = ml_ast_node (ast, c).next)
		{
			r = OpVec_push (seq, op_node (c, 0));

			if (r == ML_OK && ml_ast_node (ast, c).next != 0)
			{
				r = OpVec_push (seq, op_tok (T_COMMA));
			}
		}

		if (r == ML_OK)
		{
			r = OpVec_push (seq, op_tok (T_RB));
		}
	}

	return r;
}

/* Writes the tree below root; the stack holds what is still to be written. */
static MlResult typeset_tree (
	MlStr          *out,
	MlMathNotation n,
	MlSlice        source,
	const MlAst    *ast
)
{
	MlResult r = ML_OK;
	OpVec    stack;
	OpVec    seq;

	OpVec_init (&stack);
	OpVec_init (&seq);
	r = OpVec_push (&stack, op_node (ast->root, 0));

	while (r == ML_OK && stack.len > 0)
	{
		Op top = stack.data [stack.len - 1];

		stack.len--;

		if (top.what == OP_TOK)
		{
			r = put_tok (out, n, top.tok);
		}

		else if (top.what == OP_FUNC)
		{
			r = put_name (out, n, ml_ast_text (ast, source, top.node), 1);
		}

		else if (top.parens)
		{
			/* the brackets around it, written before and after the node */
			r = OpVec_push (&stack, op_tok (T_RP));
			r = ml_choose (
				r == ML_OK,
				OpVec_push (&stack, op_node (top.node, 0)),
				r
			);
			r = ml_choose (r == ML_OK, put_tok (out, n, T_LP), r);
		}

		else
		{
			seq.len = 0;
			r       = expand (&seq, out, n, source, ast, top.node);

			/* the pieces go on the stack backwards, the first one on top */
			for (uint64_t i = seq.len; i > 0 && r == ML_OK; i--)
			{
				r = OpVec_push (&stack, seq.data [i - 1]);
			}
		}
	}

	OpVec_free (&seq);
	OpVec_free (&stack);
	return r;
}

MlResult ml_math_typeset (MlStr *out, MlMathNotation n, MlSlice source)
{
	MlResult      r = ML_OK;
	/* typesetting does not evaluate, so "to" is the operator it looks like */
	MlParseResult p = ml_parse_units (source, 1);
	MlStr         text;

	ml_str_init (&text);

	if (p.error.code != ML_PARSE_OK || p.ast.root == 0)
	{
		r = ML_ERR_PARSE;
	}

	if (r == ML_OK)
	{
		r = typeset_tree (&text, n, source, &p.ast);
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_str_view (text));
	}

	ml_str_free (&text);
	ml_ast_free (&p.ast);
	return r;
}

MlResult ml_math_relation (MlStr *out, MlMathNotation n, int32_t approx)
{
	return put_tok (out, n, ml_choose (approx, T_APPROX, T_EQ));
}
