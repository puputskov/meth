#include <matlog/parse/parser.h>

#define NIL ML_NODE_NIL_INDEX

/*
 * The parser does not recurse (see the machine below). The grammar functions
 * are modes of one loop, and what a call would keep on the C stack is a frame
 * on a stack of its own.
 */
typedef enum ParseMode
{
	MODE_BINARY,       /* binary operators of the level in Parser.level and up */
	MODE_UNARY,        /* {'-' | '+'} factor */
	MODE_FACTOR,       /* primary {'!' | superscript} ['^' unary] */
	MODE_PRIMARY,      /* atom | '√' operand */
	MODE_SQRT_OPERAND, /* the operand of '√' */
	MODE_ATOM,         /* number | name [call] | '(' expression ')' */
	MODE_RETURN        /* a node (Parser.val) is ready for the top frame */
} ParseMode;

/* What was going on when a mode started another one: where to go on. */
typedef enum FrameKind
{
	FRAME_BIN_LHS,        /* waiting for the first operand */
	FRAME_BIN_RHS,        /* waiting for the right operand of an operator */
	FRAME_UNARY,          /* waiting for the operand of '-' or '+' */
	FRAME_FACTOR_PRIMARY, /* waiting for the primary of a factor */
	FRAME_FACTOR_EXP,     /* waiting for the exponent after '^' */
	FRAME_PRIMARY_SQRT,   /* waiting for the operand of '√' */
	FRAME_SQRT_PREFIX,    /* waiting for the operand inside a '√' operand */
	FRAME_SQRT_ATOM,      /* waiting for the atom of a '√' operand */
	FRAME_PAREN,          /* waiting for the expression inside '(' ')' */
	FRAME_CALL_ARG        /* waiting for an argument of a call */
} FrameKind;

typedef struct ParseFrame
{
	FrameKind   kind;
	uint64_t    node;  /* left operand, base of a power, call */
	uint64_t    start; /* byte where a prefix operator starts */
	MlTokenKind token; /* the operator */
	uint64_t    level; /* minimum operator level */
} ParseFrame;

ML_VEC_DEFINE (ParseFrameVec, ParseFrame)

typedef struct Parser
{
	MlSlice        source;
	MlTokenVec     tokens;
	uint64_t       pos;
	MlAst          ast;
	MlParseError   error;
	int32_t        failed;
	uint64_t       depth;
	ParseFrameVec  stack;
	ParseMode      mode;
	uint64_t       level; /* argument of MODE_BINARY */
	uint64_t       val;   /* the node that MODE_RETURN hands over */
} Parser;

static const char *const error_texts [ML_PARSE_CODE_COUNT] =
{
	[ML_PARSE_OK]               = "no error",
	[ML_PARSE_UNEXPECTED_END]   = "the expression ends too early",
	[ML_PARSE_UNEXPECTED_TOKEN] = "unexpected symbol",
	[ML_PARSE_EXPECTED_EXPR]    = "an expression is expected here",
	[ML_PARSE_BAD_CHARACTER]    = "unknown character",
	[ML_PARSE_BAD_SUPERSCRIPT]  = "a superscript must be an integer",
	[ML_PARSE_ASSIGN_TARGET]    = "only a name or f(x, ...) can be defined",
	[ML_PARSE_DUPLICATE_PARAM]  = "the same parameter name twice",
	[ML_PARSE_TOO_DEEP]         = "the expression is nested too deeply",
	[ML_PARSE_NO_MEMORY]        = "out of memory"
};

const char *ml_parse_error_text (MlParseCode code)
{
	return ml_choose (
		(int32_t)code >= 0 && code < ML_PARSE_CODE_COUNT,
		error_texts [code],
		"unknown error"
	);
}

MlNode ml_ast_node (const MlAst *ast, uint64_t index)
{
	return MlNodeVec_get (&ast->nodes, index);
}

uint64_t ml_ast_child (const MlAst *ast, uint64_t index, uint64_t n)
{
	uint64_t child = ml_ast_node (ast, index).first;

	for (uint64_t i = 0; i < n && child != NIL; i++)
	{
		child = ml_ast_node (ast, child).next;
	}

	return child;
}

uint64_t ml_ast_child_count (const MlAst *ast, uint64_t index)
{
	uint64_t count = 0;

	for (
		uint64_t c = ml_ast_node (ast, index).first;
		c != NIL;
		c = ml_ast_node (ast, c).next
	)
	{
		count++;
	}

	return count;
}

uint64_t ml_ast_last_child (const MlAst *ast, uint64_t index)
{
	uint64_t first = ml_ast_node (ast, index).first;

	return ml_choose (
		first != NIL,
		ml_ast_node (ast, first).prev,
		NIL
	);
}

MlSlice ml_ast_text (const MlAst *ast, MlSlice source, uint64_t index)
{
	MlNode node = ml_ast_node (ast, index);

	return ml_slice_sub (source, node.start, node.len);
}

static MlToken peek_at (const Parser *p, uint64_t offset)
{
	return MlTokenVec_get (&p->tokens, p->pos + offset);
}

static MlToken peek (const Parser *p)
{
	return peek_at (p, 0);
}

/* The kind of the token that was consumed last. */
static MlTokenKind previous_kind (const Parser *p)
{
	return ml_choose (
		p->pos > 0,
		MlTokenVec_get (&p->tokens, p->pos - 1).kind,
		ML_TOK_END
	);
}

static MlToken advance (Parser *p)
{
	MlToken t = peek (p);

	if (t.kind != ML_TOK_END)
	{
		p->pos++;
	}

	return t;
}

/* Records the first problem; later ones are consequences of it. Returns nil. */
static uint64_t fail (Parser *p, MlParseCode code, MlToken at)
{
	if (!p->failed)
	{
		p->failed       = 1;
		p->error.code   = code;
		p->error.start  = at.start;
		p->error.len    = at.len;
		p->error.at_end = at.kind == ML_TOK_END;
	}

	return NIL;
}

/* A token that cannot continue what was read so far. */
static uint64_t fail_unexpected (Parser *p, MlToken t)
{
	return fail (
		p,
		ml_choose (
			t.kind == ML_TOK_END,
			ML_PARSE_UNEXPECTED_END,
			ml_choose (
				t.kind == ML_TOK_ERROR,
				ML_PARSE_BAD_CHARACTER,
				ML_PARSE_UNEXPECTED_TOKEN
			)
		),
		t
	);
}

/* A token where an expression has to start. */
static uint64_t fail_expected (Parser *p, MlToken t)
{
	return fail (
		p,
		ml_choose (
			t.kind == ML_TOK_END,
			ML_PARSE_UNEXPECTED_END,
			ml_choose (
				t.kind == ML_TOK_ERROR,
				ML_PARSE_BAD_CHARACTER,
				ML_PARSE_EXPECTED_EXPR
			)
		),
		t
	);
}

static MlNode node_at (const Parser *p, uint64_t index)
{
	return ml_ast_node (&p->ast, index);
}

/* The end of a node's text. */
static uint64_t node_end (const Parser *p, uint64_t index)
{
	MlNode node = node_at (p, index);

	return node.start + node.len;
}

/* A new unlinked node covering the byte range [start, end). */
static uint64_t add_node (
	Parser     *p,
	MlNodeKind kind,
	uint64_t   start,
	uint64_t   end
)
{
	MlNode node =
	{
		.kind   = kind,
		.parent = NIL,
		.first  = NIL,
		.next   = NIL,
		.prev   = NIL,
		.height = 1,
		.start  = start,
		.len    = end - start
	};
	uint64_t index = p->ast.nodes.len;
	MlResult r     = ml_choose (
		p->failed,
		ML_ERR_INVALID,
		MlNodeVec_push (&p->ast.nodes, node)
	);

	return ml_choose (
		r == ML_OK,
		index,
		fail (p, ML_PARSE_NO_MEMORY, peek (p))
	);
}

static uint64_t add_leaf (Parser *p, MlNodeKind kind, MlToken t)
{
	return add_node (p, kind, t.start, t.start + t.len);
}

/*
 * Appends child to the children of parent (the first child's prev is the
 * last child) and keeps the height of parent. A tree that gets higher than
 * ML_PARSE_MAX_TREE_DEPTH is refused.
 */
static void adopt (Parser *p, uint64_t parent, uint64_t child)
{
	if (!p->failed && parent != NIL && child != NIL)
	{
		uint64_t first = node_at (p, parent).first;
		uint64_t last  = ml_choose (
			first != NIL,
			node_at (p, first).prev,
			child
		);
		uint64_t height = node_at (p, child).height + 1;

		p->ast.nodes.data [child].parent = parent;
		p->ast.nodes.data [child].prev   = last;
		p->ast.nodes.data [child].next   = NIL;

		if (first != NIL)
		{
			p->ast.nodes.data [last].next  = child;
			p->ast.nodes.data [first].prev = child;
		}

		else
		{
			p->ast.nodes.data [parent].first = child;
		}

		if (height > node_at (p, parent).height)
		{
			p->ast.nodes.data [parent].height = height;
		}

		if (height > ML_PARSE_MAX_TREE_DEPTH)
		{
			MlToken at =
			{
				.kind  = ML_TOK_NAME,
				.start = node_at (p, parent).start,
				.len   = node_at (p, parent).len
			};

			fail (p, ML_PARSE_TOO_DEEP, at);
		}
	}
}

/* A node of the given kind over two children. */
static uint64_t binary (Parser *p, MlNodeKind kind, uint64_t a, uint64_t b)
{
	uint64_t node = NIL;

	if (!p->failed && a != NIL && b != NIL)
	{
		node = add_node (p, kind, node_at (p, a).start, node_end (p, b));
		adopt (p, node, a);
		adopt (p, node, b);
	}

	return node;
}

/* A node of the given kind over one child, covering [start, end). */
static uint64_t unary (
	Parser     *p,
	MlNodeKind kind,
	uint64_t   start,
	uint64_t   end,
	uint64_t   child
)
{
	uint64_t node = NIL;

	if (!p->failed && child != NIL)
	{
		node = add_node (p, kind, start, end);
		adopt (p, node, child);
	}

	return node;
}

/* A prefix operator at byte start over operand (neg or sqrt). */
static uint64_t prefix (
	Parser     *p,
	MlNodeKind kind,
	uint64_t   start,
	uint64_t   operand
)
{
	return unary (p, kind, start, node_end (p, operand), operand);
}

/* Counts nesting; returns 1 when there is room, which leave must give back. */
static int32_t enter (Parser *p)
{
	int32_t room = p->depth < ML_PARSE_MAX_DEPTH;

	if (room)
	{
		p->depth++;
	}

	else
	{
		fail (p, ML_PARSE_TOO_DEEP, peek (p));
	}

	return room;
}

static void leave (Parser *p)
{
	p->depth--;
}

/* Does an implicit product start between the last token and this one? */
static int32_t implicit_product (MlTokenKind before, MlTokenKind next)
{
	int32_t after_factor = (
		   before == ML_TOK_NUMBER
		|| before == ML_TOK_RPAREN
		|| before == ML_TOK_SUPERSCRIPT
	);

	return (
		   after_factor
		&& (
			   next == ML_TOK_NAME
			|| next == ML_TOK_LPAREN
			|| next == ML_TOK_SQRT
			|| (before == ML_TOK_RPAREN && next == ML_TOK_NUMBER)
		)
	);
}

/* The node kind of a binary operator token (a product for anything else). */
static MlNodeKind binary_kind (MlTokenKind kind)
{
	return ml_choose (
		kind == ML_TOK_PLUS,
		ML_NODE_ADD,
		ml_choose (
			kind == ML_TOK_MINUS,
			ML_NODE_SUB,
			ml_choose (kind == ML_TOK_SLASH, ML_NODE_DIV, ML_NODE_MUL)
		)
	);
}

/*
 * The superscript text must be an integer.
 */
static int32_t superscript_is_integer (const Parser *p, MlToken t)
{
	MlStr   digits;
	int32_t ok;

	ml_str_init (&digits);
	ok = ml_superscript_to_ascii (
		&digits,
		ml_token_text (p->source, t)
	) == ML_OK;
	ml_str_free (&digits);
	return ok;
}

/* '!' or a superscript after node. */
static uint64_t postfix (Parser *p, uint64_t node, MlToken t)
{
	uint64_t result = NIL;

	if (t.kind == ML_TOK_BANG)
	{
		result = unary (
			p,
			ML_NODE_FACT,
			node_at (p, node).start,
			t.start + t.len,
			node
		);
	}

	else if (superscript_is_integer (p, t))
	{
		result = binary (
			p,
			ML_NODE_POW,
			node,
			add_leaf (p, ML_NODE_SUPERSCRIPT, t)
		);
	}

	else
	{
		fail (p, ML_PARSE_BAD_SUPERSCRIPT, t);
	}

	return result;
}

/* The ')' that ends a parenthesized expression. */
static void close_paren (Parser *p)
{
	MlToken close = peek (p);

	if (!p->failed && close.kind == ML_TOK_RPAREN)
	{
		advance (p);
	}

	else if (!p->failed)
	{
		fail_unexpected (p, close);
	}
}

/* The ')' that ends a call; it becomes part of the text of the call. */
static void close_call (Parser *p, uint64_t call)
{
	MlToken close = peek (p);

	if (!p->failed && close.kind == ML_TOK_RPAREN)
	{
		advance (p);
		p->ast.nodes.data [call].len = close.start + close.len
			- node_at (p, call).start;
	}

	else if (!p->failed)
	{
		fail_unexpected (p, close);
	}
}

/*
 * The expression grammar, without recursion.
 *
 * Every function of a recursive descent parser is a mode. Where such a parser
 * would call a function and later continue after the call, this one pushes a
 * frame that says where to continue and switches to the mode of the callee;
 * where the callee would return a node it sets Parser.val and switches to
 * MODE_RETURN, which pops a frame and continues there. The loop in parse_expr
 * runs until the frame it started with is the only one left. The stack of
 * frames lives on the heap, and the nesting limit (enter, ML_PARSE_MAX_DEPTH)
 * keeps it short.
 */

static void push (
	Parser      *p,
	FrameKind   kind,
	uint64_t    node,
	uint64_t    start,
	MlTokenKind token,
	uint64_t    level
)
{
	ParseFrame f =
	{
		.kind  = kind,
		.node  = node,
		.start = start,
		.token = token,
		.level = level
	};

	if (ParseFrameVec_push (&p->stack, f) != ML_OK)
	{
		fail (p, ML_PARSE_NO_MEMORY, peek (p));
	}
}

/* Hands a node to the frame on top of the stack. */
static void give (Parser *p, uint64_t node)
{
	p->val  = node;
	p->mode = MODE_RETURN;
}

/* Starts an expression: binary operators of every level. */
static void call_expr (Parser *p)
{
	p->mode  = MODE_BINARY;
	p->level = 1;
}

/*
 * The level of the binary operator at the cursor: 1 for + -, 2 for * / and
 * an implicit product, 0 when the next token is no operator.
 */
static uint64_t operator_level (const Parser *p)
{
	MlTokenKind next = peek (p).kind;

	return ml_choose (
		next == ML_TOK_PLUS || next == ML_TOK_MINUS,
		1,
		ml_choose (
			   next == ML_TOK_STAR
			|| next == ML_TOK_SLASH
			|| implicit_product (previous_kind (p), next),
			2,
			0
		)
	);
}

/* ---- modes ---- */

static void mode_binary (Parser *p)
{
	push (p, FRAME_BIN_LHS, NIL, 0, ML_TOK_END, p->level);
	p->mode = MODE_UNARY;
}

static void mode_unary (Parser *p)
{
	MlTokenKind kind  = peek (p).kind;
	uint64_t    start = peek (p).start;

	if (kind == ML_TOK_MINUS || kind == ML_TOK_PLUS)
	{
		advance (p);

		if (enter (p))
		{
			push (p, FRAME_UNARY, NIL, start, kind, 0);
		}

		else
		{
			give (p, NIL);
		}
	}

	else
	{
		p->mode = MODE_FACTOR;
	}
}

static void mode_factor (Parser *p)
{
	push (p, FRAME_FACTOR_PRIMARY, NIL, 0, ML_TOK_END, 0);
	p->mode = MODE_PRIMARY;
}

static void mode_primary (Parser *p)
{
	uint64_t start = peek (p).start;

	if (peek (p).kind == ML_TOK_SQRT)
	{
		advance (p);
		push (p, FRAME_PRIMARY_SQRT, NIL, start, ML_TOK_END, 0);
		p->mode = MODE_SQRT_OPERAND;
	}

	else
	{
		p->mode = MODE_ATOM;
	}
}

/* One factor after a root sign, optionally signed: -x, √x, x, (x). */
static void mode_sqrt_operand (Parser *p)
{
	MlTokenKind kind  = peek (p).kind;
	uint64_t    start = peek (p).start;

	if (!enter (p))
	{
		give (p, NIL);
	}

	else if (kind == ML_TOK_MINUS || kind == ML_TOK_SQRT)
	{
		advance (p);
		push (p, FRAME_SQRT_PREFIX, NIL, start, kind, 0);
	}

	else
	{
		push (p, FRAME_SQRT_ATOM, NIL, 0, ML_TOK_END, 0);
		p->mode = MODE_ATOM;
	}
}

/* name '(' ... with the name read: the call node, then its arguments. */
static void start_call (Parser *p, uint64_t name)
{
	uint64_t call = add_node (
		p,
		ML_NODE_CALL,
		node_at (p, name).start,
		node_end (p, name)
	);

	adopt (p, call, name);
	advance (p);

	if (!p->failed && enter (p))
	{
		if (peek (p).kind != ML_TOK_RPAREN)
		{
			push (p, FRAME_CALL_ARG, call, 0, ML_TOK_END, 0);
			call_expr (p);
		}

		else
		{
			close_call (p, call);
			leave (p);
			give (p, call);
		}
	}

	else
	{
		give (p, NIL);
	}
}

static void mode_atom (Parser *p)
{
	MlToken t = peek (p);

	if (t.kind == ML_TOK_NUMBER)
	{
		advance (p);
		give (p, add_leaf (p, ML_NODE_NUMBER, t));
	}

	else if (t.kind == ML_TOK_NAME)
	{
		uint64_t name;

		advance (p);
		name = add_leaf (p, ML_NODE_NAME, t);

		if (!p->failed && peek (p).kind == ML_TOK_LPAREN)
		{
			start_call (p, name);
		}

		else
		{
			give (p, name);
		}
	}

	else if (t.kind == ML_TOK_LPAREN)
	{
		advance (p);

		if (enter (p))
		{
			push (p, FRAME_PAREN, NIL, 0, ML_TOK_END, 0);
			call_expr (p);
		}

		else
		{
			give (p, NIL);
		}
	}

	else
	{
		fail_expected (p, t);
		give (p, NIL);
	}
}

/* ---- returns ---- */

/* The left operand is known: take the next operator or hand the node on. */
static void binary_continue (Parser *p, uint64_t min_level, uint64_t node)
{
	uint64_t level = operator_level (p);

	if (!p->failed && level >= min_level && level > 0)
	{
		MlTokenKind kind = peek (p).kind;

		/* an implicit product has no token of its own */
		if (
			   kind == ML_TOK_PLUS
			|| kind == ML_TOK_MINUS
			|| kind == ML_TOK_STAR
			|| kind == ML_TOK_SLASH
		)
		{
			advance (p);
		}

		push (p, FRAME_BIN_RHS, node, 0, kind, min_level);
		p->mode  = MODE_BINARY;
		p->level = level + 1;
	}

	else
	{
		give (p, node);
	}
}

/* The primary of a factor is known: postfix operators, then an exponent. */
static void factor_continue (Parser *p, uint64_t node)
{
	while (
		   !p->failed
		&& (
			   peek (p).kind == ML_TOK_BANG
			|| peek (p).kind == ML_TOK_SUPERSCRIPT
		)
	)
	{
		node = postfix (p, node, advance (p));
	}

	if (!p->failed && peek (p).kind == ML_TOK_CARET)
	{
		advance (p);

		if (enter (p))
		{
			push (p, FRAME_FACTOR_EXP, node, 0, ML_TOK_END, 0);
			p->mode = MODE_UNARY;
		}

		else
		{
			give (p, NIL);
		}
	}

	else
	{
		give (p, node);
	}
}

/* An argument of a call is known: the next one, or the end of the call. */
static void call_arg_continue (Parser *p, uint64_t call, uint64_t arg)
{
	int32_t more;

	adopt (p, call, arg);
	more = !p->failed && peek (p).kind == ML_TOK_COMMA;

	if (more)
	{
		advance (p);
		push (p, FRAME_CALL_ARG, call, 0, ML_TOK_END, 0);
		call_expr (p);
	}

	else
	{
		close_call (p, call);
		leave (p);
		give (p, call);
	}
}

static void mode_return (Parser *p)
{
	ParseFrame f   = p->stack.data [p->stack.len - 1];
	uint64_t   val = p->val;

	p->stack.len--;

	if (f.kind == FRAME_BIN_LHS)
	{
		binary_continue (p, f.level, val);
	}

	else if (f.kind == FRAME_BIN_RHS)
	{
		binary_continue (
			p,
			f.level,
			binary (p, binary_kind (f.token), f.node, val)
		);
	}

	else if (f.kind == FRAME_UNARY)
	{
		leave (p);
		give (
			p,
			ml_choose (
				f.token == ML_TOK_PLUS,
				val,
				prefix (p, ML_NODE_NEG, f.start, val)
			)
		);
	}

	else if (f.kind == FRAME_FACTOR_PRIMARY)
	{
		factor_continue (p, val);
	}

	else if (f.kind == FRAME_FACTOR_EXP)
	{
		leave (p);
		give (p, binary (p, ML_NODE_POW, f.node, val));
	}

	else if (f.kind == FRAME_PRIMARY_SQRT)
	{
		give (p, prefix (p, ML_NODE_SQRT, f.start, val));
	}

	else if (f.kind == FRAME_SQRT_PREFIX)
	{
		leave (p);
		give (
			p,
			prefix (
				p,
				ml_choose (f.token == ML_TOK_MINUS, ML_NODE_NEG, ML_NODE_SQRT),
				f.start,
				val
			)
		);
	}

	else if (f.kind == FRAME_SQRT_ATOM)
	{
		leave (p);
		give (p, val);
	}

	else if (f.kind == FRAME_PAREN)
	{
		close_paren (p);
		leave (p);
		give (p, val);
	}

	else
	{
		call_arg_continue (p, f.node, val);
	}
}

static void parse_step (Parser *p)
{
	if (p->mode == MODE_BINARY)
	{
		mode_binary (p);
	}

	else if (p->mode == MODE_UNARY)
	{
		mode_unary (p);
	}

	else if (p->mode == MODE_FACTOR)
	{
		mode_factor (p);
	}

	else if (p->mode == MODE_PRIMARY)
	{
		mode_primary (p);
	}

	else if (p->mode == MODE_SQRT_OPERAND)
	{
		mode_sqrt_operand (p);
	}

	else if (p->mode == MODE_ATOM)
	{
		mode_atom (p);
	}

	else
	{
		mode_return (p);
	}
}

/* expression: runs the machine until the whole expression is one node. */
static uint64_t parse_expr (Parser *p)
{
	uint64_t base = p->stack.len;

	call_expr (p);

	while (!p->failed && (p->mode != MODE_RETURN || p->stack.len > base))
	{
		parse_step (p);
	}

	p->stack.len = base;
	return ml_choose (p->failed, NIL, p->val);
}

/* name '(' [name {',' name}] ')' '=' at the current position? */
static int32_t is_function_definition (const Parser *p)
{
	uint64_t i  = 2;
	int32_t  ok = (
		   peek_at (p, 0).kind == ML_TOK_NAME
		&& peek_at (p, 1).kind == ML_TOK_LPAREN
	);

	if (ok && peek_at (p, i).kind != ML_TOK_RPAREN)
	{
		int32_t more = 1;

		while (ok && more)
		{
			ok = peek_at (p, i).kind == ML_TOK_NAME;
			i    += 1;
			more = ok && peek_at (p, i).kind == ML_TOK_COMMA;
			i    += ml_choose (more, 1, 0);
		}
	}

	return (
		   ok
		&& peek_at (p, i).kind == ML_TOK_RPAREN
		&& peek_at (p, i + 1).kind == ML_TOK_EQUALS
	);
}

/* Is a parameter with this text already among the children of params? */
static int32_t has_param (const Parser *p, uint64_t params, MlSlice text)
{
	int32_t found = 0;

	for (
		uint64_t c = node_at (p, params).first;
		c != NIL && !found;
		c = node_at (p, c).next
	)
	{
		MlNode other = node_at (p, c);

		found = ml_slice_equal (
			ml_slice_sub (p->source, other.start, other.len),
			text
		);
	}

	return found;
}

/* f(a, b) = expression */
static uint64_t parse_definition (Parser *p)
{
	uint64_t name   = add_leaf (p, ML_NODE_NAME, advance (p));
	MlToken  open   = advance (p);
	uint64_t params = add_node (p, ML_NODE_PARAMS, open.start, open.start + 1);
	MlToken  close;
	uint64_t body;
	uint64_t def    = NIL;

	while (!p->failed && peek (p).kind == ML_TOK_NAME)
	{
		MlToken param = advance (p);

		if (has_param (p, params, ml_token_text (p->source, param)))
		{
			fail (p, ML_PARSE_DUPLICATE_PARAM, param);
		}

		adopt (p, params, add_leaf (p, ML_NODE_NAME, param));

		if (peek (p).kind == ML_TOK_COMMA)
		{
			advance (p);
		}
	}

	close = advance (p);
	advance (p);

	if (!p->failed)
	{
		p->ast.nodes.data [params].len = close.start + close.len - open.start;
	}

	body = parse_expr (p);

	if (!p->failed)
	{
		def = add_node (
			p,
			ML_NODE_FUNC_DEF,
			node_at (p, name).start,
			node_end (p, body)
		);
		adopt (p, def, name);
		adopt (p, def, params);
		adopt (p, def, body);
	}

	return ml_choose (p->failed, NIL, def);
}

static uint64_t parse_statement (Parser *p)
{
	MlToken  t    = peek (p);
	uint64_t root = NIL;

	if (t.kind == ML_TOK_NAME && peek_at (p, 1).kind == ML_TOK_EQUALS)
	{
		uint64_t target = add_leaf (p, ML_NODE_NAME, advance (p));
		uint64_t body;

		advance (p);
		body = parse_expr (p);
		root = binary (p, ML_NODE_ASSIGN, target, body);
	}

	else if (is_function_definition (p))
	{
		root = parse_definition (p);
	}

	else
	{
		root = parse_expr (p);
	}

	/* nothing may follow the statement */
	if (!p->failed)
	{
		MlToken rest = peek (p);

		if (rest.kind == ML_TOK_EQUALS)
		{
			fail (p, ML_PARSE_ASSIGN_TARGET, rest);
		}

		else if (rest.kind != ML_TOK_END)
		{
			fail_unexpected (p, rest);
		}
	}

	return ml_choose (p->failed, NIL, root);
}

void ml_ast_free (MlAst *ast)
{
	MlNodeVec_free (&ast->nodes);
	ast->root = NIL;
}

MlParseResult ml_parse (MlSlice source)
{
	MlParseResult result;
	Parser        p;
	MlLexResult   lexed = ml_lex (source);
	MlNode        nil   =
	{
		.kind   = ML_NODE_NIL,
		.parent = NIL,
		.first  = NIL,
		.next   = NIL,
		.prev   = NIL,
		.height = 0,
		.start  = 0,
		.len    = 0
	};

	p.source       = source;
	p.tokens       = lexed.tokens;
	p.pos          = 0;
	p.failed       = 0;
	p.depth        = 0;
	p.mode         = MODE_BINARY;
	p.level        = 1;
	p.val          = NIL;
	ParseFrameVec_init (&p.stack);
	p.error.code   = ML_PARSE_OK;
	p.error.start  = 0;
	p.error.len    = 0;
	p.error.at_end = 0;
	p.ast.root     = NIL;
	MlNodeVec_init (&p.ast.nodes);

	/* index 0 is the nil node, so the first real node is number 1 */
	if (lexed.result != ML_OK || MlNodeVec_push (&p.ast.nodes, nil) != ML_OK)
	{
		MlToken at = { .kind = ML_TOK_END, .start = 0, .len = 0 };

		fail (&p, ML_PARSE_NO_MEMORY, at);
	}

	else
	{
		p.ast.root = parse_statement (&p);
	}

	result.error = p.error;
	result.ast   = p.ast;

	if (p.failed)
	{
		ml_ast_free (&result.ast);
	}

	ParseFrameVec_free (&p.stack);
	MlTokenVec_free (&p.tokens);
	return result;
}

int32_t ml_parse_complete (MlSlice source)
{
	MlParseResult parsed = ml_parse (source);
	int32_t       ok     = parsed.error.code == ML_PARSE_OK;

	ml_ast_free (&parsed.ast);
	return ok;
}

/* The text in front of the children of a node, e.g. "neg" or "+". */
static const char *const node_labels [ML_NODE_KIND_COUNT] =
{
	[ML_NODE_NEG]      = "neg",
	[ML_NODE_ADD]      = "+",
	[ML_NODE_SUB]      = "-",
	[ML_NODE_MUL]      = "*",
	[ML_NODE_DIV]      = "/",
	[ML_NODE_POW]      = "^",
	[ML_NODE_FACT]     = "!",
	[ML_NODE_SQRT]     = "sqrt",
	[ML_NODE_CALL]     = "call",
	[ML_NODE_ASSIGN]   = "=",
	[ML_NODE_FUNC_DEF] = "def",
	[ML_NODE_PARAMS]   = ""
};

static const char *node_label (MlNodeKind kind)
{
	return ml_choose (
		(int32_t)kind < ML_NODE_KIND_COUNT,
		node_labels [kind],
		""
	);
}

/* Leaves are printed as their text; every other node is a group. */
static int32_t is_group (MlNodeKind kind)
{
	return (
		   kind != ML_NODE_NIL
		&& kind != ML_NODE_NUMBER
		&& kind != ML_NODE_NAME
		&& kind != ML_NODE_SUPERSCRIPT
	);
}

/*
 * What is printed when the walk arrives at a node: the separator that belongs
 * between it and what came before, then its text, or for a group the opening
 * parenthesis and the label.
 */
static MlResult dump_enter (
	MlStr       *out,
	MlSlice     source,
	const MlAst *ast,
	uint64_t    root,
	uint64_t    index
)
{
	MlNode   node   = ml_ast_node (ast, index);
	MlNode   parent = ml_ast_node (ast, node.parent);
	MlResult r      = ML_OK;

	if (index != root)
	{
		/* a params group has no label, so its first child needs no space */
		int32_t tight = (
			   index == parent.first
			&& node_label (parent.kind) [0] == '\0'
		);

		r = ml_str_append (out, ml_slice_cstr (ml_choose (tight, "", " ")));
	}

	if (r == ML_OK && node.kind == ML_NODE_NIL)
	{
		r = ml_str_append (out, ml_slice_cstr ("nil"));
	}

	else if (r == ML_OK && node.kind == ML_NODE_SUPERSCRIPT)
	{
		r = ml_superscript_to_ascii (out, ml_ast_text (ast, source, index));
	}

	else if (r == ML_OK && !is_group (node.kind))
	{
		r = ml_str_append (out, ml_ast_text (ast, source, index));
	}

	else if (r == ML_OK)
	{
		r = ml_str_appendf (out, "(%s", node_label (node.kind));
	}

	return r;
}

/*
 * Walks the tree depth first by following the first, next and parent links,
 * so it needs no stack: down to the first child when there is one, otherwise
 * to the next sibling, and when there is none up to the parent, whose group
 * is closed on the way.
 */
MlResult ml_ast_dump (MlStr *out, MlSlice source, const MlAst *ast)
{
	uint64_t root   = ast->root;
	uint64_t cur    = root;
	int32_t  done   = 0;
	MlResult r      = ML_OK;

	while (!done && r == ML_OK)
	{
		MlNode node = ml_ast_node (ast, cur);

		r = dump_enter (out, source, ast, root, cur);

		if (r == ML_OK && is_group (node.kind) && node.first != NIL)
		{
			cur = node.first;
		}

		else
		{
			int32_t climbing = r == ML_OK;

			/* a group without children closes at once */
			if (r == ML_OK && is_group (node.kind))
			{
				r = ml_str_append (out, ml_slice_cstr (")"));
			}

			while (climbing && r == ML_OK)
			{
				MlNode here = ml_ast_node (ast, cur);

				if (cur == root)
				{
					done     = 1;
					climbing = 0;
				}

				else if (here.next != NIL)
				{
					cur      = here.next;
					climbing = 0;
				}

				else
				{
					cur = here.parent;
					r   = ml_str_append (out, ml_slice_cstr (")"));
				}
			}
		}
	}

	return r;
}
