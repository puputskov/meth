#include <matlog/parse/parser.h>

#define NIL ML_NODE_NIL_INDEX

/* Longest run of operators of one level (1+1+1...) that is accepted. */
#define ML_PARSE_MAX_CHAIN 5000

typedef struct Parser
{
	MlSlice      source;
	MlTokenVec   tokens;
	uint64_t     pos;
	MlAst        ast;
	MlParseError error;
	int32_t      failed;
	uint64_t     depth;
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
 * Appends child to the children of parent (the first child's prev is the last).
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

/* A node of the given kind over one child, starting at the byte start. */
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

static uint64_t parse_expr (Parser *p);
static uint64_t parse_unary (Parser *p);

/* name '(' [expression {',' expression}] ')' with the name already read. */
static uint64_t parse_call (Parser *p, uint64_t name)
{
	uint64_t call = add_node (
		p,
		ML_NODE_CALL,
		node_at (p, name).start,
		node_end (p, name)
	);

	adopt (p, call, name);
	advance (p);

	if (enter (p))
	{
		int32_t more = peek (p).kind != ML_TOK_RPAREN;
		MlToken close;

		while (more && !p->failed)
		{
			adopt (p, call, parse_expr (p));
			more = !p->failed && peek (p).kind == ML_TOK_COMMA;

			if (more)
			{
				advance (p);
			}
		}

		close = peek (p);

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

		leave (p);
	}

	return ml_choose (p->failed, NIL, call);
}

/* number | name [call] | '(' expression ')' */
static uint64_t parse_atom (Parser *p)
{
	MlToken  t    = peek (p);
	uint64_t node = NIL;

	if (p->failed)
	{
		node = NIL;
	}

	else if (t.kind == ML_TOK_NUMBER)
	{
		advance (p);
		node = add_leaf (p, ML_NODE_NUMBER, t);
	}

	else if (t.kind == ML_TOK_NAME)
	{
		advance (p);
		node = add_leaf (p, ML_NODE_NAME, t);

		if (!p->failed && peek (p).kind == ML_TOK_LPAREN)
		{
			node = parse_call (p, node);
		}
	}

	else if (t.kind == ML_TOK_LPAREN)
	{
		advance (p);

		if (enter (p))
		{
			MlToken close;

			node  = parse_expr (p);
			close = peek (p);

			if (!p->failed && close.kind == ML_TOK_RPAREN)
			{
				advance (p);
			}

			else if (!p->failed)
			{
				fail_unexpected (p, close);
			}

			leave (p);
		}
	}

	else
	{
		fail_expected (p, t);
	}

	return ml_choose (p->failed, NIL, node);
}

/* The operand of a square root sign: one factor, optionally signed. */
static uint64_t parse_sqrt_operand (Parser *p)
{
	MlToken  t       = peek (p);
	uint64_t node    = NIL;
	int32_t  entered = !p->failed && enter (p);

	if (!entered)
	{
		node = NIL;
	}

	else if (t.kind == ML_TOK_MINUS || t.kind == ML_TOK_SQRT)
	{
		uint64_t operand;

		advance (p);
		operand = parse_sqrt_operand (p);
		node    = unary (
			p,
			ml_choose (t.kind == ML_TOK_MINUS, ML_NODE_NEG, ML_NODE_SQRT),
			t.start,
			node_end (p, operand),
			operand
		);
	}

	else
	{
		node = parse_atom (p);
	}

	if (entered)
	{
		leave (p);
	}

	return ml_choose (p->failed, NIL, node);
}

/* atom | '√' operand */
static uint64_t parse_primary (Parser *p)
{
	MlToken  t    = peek (p);
	uint64_t node = NIL;

	if (t.kind == ML_TOK_SQRT)
	{
		uint64_t operand;

		advance (p);
		operand = parse_sqrt_operand (p);
		node    = unary (
			p,
			ML_NODE_SQRT,
			t.start,
			node_end (p, operand),
			operand
		);
	}

	else
	{
		node = parse_atom (p);
	}

	return ml_choose (p->failed, NIL, node);
}

/* primary {'!' | superscript} */
static uint64_t parse_postfix (Parser *p)
{
	uint64_t node  = parse_primary (p);
	uint64_t count = 0;

	while (
		   !p->failed
		&& count < ML_PARSE_MAX_CHAIN
		&& (
			   peek (p).kind == ML_TOK_BANG
			|| peek (p).kind == ML_TOK_SUPERSCRIPT
		)
	)
	{
		MlToken t = advance (p);

		count++;

		if (t.kind == ML_TOK_BANG)
		{
			node = unary (
				p,
				ML_NODE_FACT,
				node_at (p, node).start,
				t.start + t.len,
				node
			);
		}

		else
		{
			MlStr digits;

			ml_str_init (&digits);

			if (
				ml_superscript_to_ascii (&digits, ml_token_text (p->source, t))
				!= ML_OK
			)
			{
				fail (p, ML_PARSE_BAD_SUPERSCRIPT, t);
			}

			ml_str_free (&digits);
			node = binary (
				p,
				ML_NODE_POW,
				node,
				add_leaf (p, ML_NODE_SUPERSCRIPT, t)
			);
		}
	}

	if (!p->failed && count >= ML_PARSE_MAX_CHAIN)
	{
		fail (p, ML_PARSE_TOO_DEEP, peek (p));
	}

	return ml_choose (p->failed, NIL, node);
}

/* postfix ['^' unary]: right associative, the exponent may be signed */
static uint64_t parse_power (Parser *p)
{
	uint64_t base = parse_postfix (p);
	uint64_t node = base;

	if (!p->failed && peek (p).kind == ML_TOK_CARET)
	{
		advance (p);

		if (enter (p))
		{
			node = binary (p, ML_NODE_POW, base, parse_unary (p));
			leave (p);
		}
	}

	return ml_choose (p->failed, NIL, node);
}

/* {'-' | '+'} power */
static uint64_t parse_unary (Parser *p)
{
	MlToken  t    = peek (p);
	uint64_t node = NIL;

	if (!p->failed && (t.kind == ML_TOK_MINUS || t.kind == ML_TOK_PLUS))
	{
		advance (p);

		if (enter (p))
		{
			uint64_t operand = parse_unary (p);

			leave (p);

			node = ml_choose (
				t.kind == ML_TOK_PLUS || operand == NIL,
				operand,
				unary (p, ML_NODE_NEG, t.start, node_end (p, operand), operand)
			);
		}
	}

	else
	{
		node = parse_power (p);
	}

	return ml_choose (p->failed, NIL, node);
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

/* unary {('*' | '/' | implicit) unary} */
static uint64_t parse_mul (Parser *p)
{
	uint64_t node  = parse_unary (p);
	uint64_t count = 0;
	int32_t  more  = 1;

	while (more && !p->failed && count < ML_PARSE_MAX_CHAIN)
	{
		MlToken t           = peek (p);
		int32_t explicit_op = t.kind == ML_TOK_STAR || t.kind == ML_TOK_SLASH;
		int32_t implicit    = implicit_product (previous_kind (p), t.kind);

		more = explicit_op || implicit;

		if (more)
		{
			uint64_t right;

			count++;

			if (explicit_op)
			{
				advance (p);
			}

			right = parse_unary (p);
			node  = binary (
				p,
				ml_choose (t.kind == ML_TOK_SLASH, ML_NODE_DIV, ML_NODE_MUL),
				node,
				right
			);
		}
	}

	if (!p->failed && count >= ML_PARSE_MAX_CHAIN)
	{
		fail (p, ML_PARSE_TOO_DEEP, peek (p));
	}

	return ml_choose (p->failed, NIL, node);
}

/* mul {('+' | '-') mul} */
static uint64_t parse_add (Parser *p)
{
	uint64_t node  = parse_mul (p);
	uint64_t count = 0;

	while (
		   !p->failed
		&& count < ML_PARSE_MAX_CHAIN
		&& (peek (p).kind == ML_TOK_PLUS || peek (p).kind == ML_TOK_MINUS)
	)
	{
		MlToken  t = advance (p);
		uint64_t right;

		count++;
		right = parse_mul (p);
		node  = binary (
			p,
			ml_choose (t.kind == ML_TOK_PLUS, ML_NODE_ADD, ML_NODE_SUB),
			node,
			right
		);
	}

	if (!p->failed && count >= ML_PARSE_MAX_CHAIN)
	{
		fail (p, ML_PARSE_TOO_DEEP, peek (p));
	}

	return ml_choose (p->failed, NIL, node);
}

static uint64_t parse_expr (Parser *p)
{
	return parse_add (p);
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
		.start  = 0,
		.len    = 0
	};

	p.source       = source;
	p.tokens       = lexed.tokens;
	p.pos          = 0;
	p.failed       = 0;
	p.depth        = 0;
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

static MlResult dump_node (
	MlStr       *out,
	MlSlice     source,
	const MlAst *ast,
	uint64_t    index
)
{
	MlNode   node  = ml_ast_node (ast, index);
	MlResult r     = ML_OK;
	int32_t  group = 0;

	if (node.kind == ML_NODE_NIL)
	{
		r = ml_str_append (out, ml_slice_cstr ("nil"));
	}

	else if (node.kind == ML_NODE_NUMBER || node.kind == ML_NODE_NAME)
	{
		r = ml_str_append (out, ml_ast_text (ast, source, index));
	}

	else if (node.kind == ML_NODE_SUPERSCRIPT)
	{
		r = ml_superscript_to_ascii (out, ml_ast_text (ast, source, index));
	}

	else
	{
		const char *label = ml_choose (
			(int32_t)node.kind < ML_NODE_KIND_COUNT,
			node_labels [node.kind],
			""
		);

		group = 1;
		r     = ml_str_appendf (out, "(%s", label);

		for (
			uint64_t c = node.first;
			c != NIL && r == ML_OK;
			c = ml_ast_node (ast, c).next
		)
		{
			r = ml_str_append (
				out,
				ml_slice_cstr (
					ml_choose (
						c == node.first && label [0] == '\0',
						"",
						" "
					)
				)
			);

			if (r == ML_OK)
			{
				r = dump_node (out, source, ast, c);
			}
		}
	}

	if (r == ML_OK && group)
	{
		r = ml_str_append (out, ml_slice_cstr (")"));
	}

	return r;
}

MlResult ml_ast_dump (MlStr *out, MlSlice source, const MlAst *ast)
{
	return dump_node (out, source, ast, ast->root);
}
