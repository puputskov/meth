#ifndef MATLOG_PARSE_PARSER_H
#define MATLOG_PARSE_PARSER_H

#include <matlog/core/names.h>
#include <matlog/parse/lexer.h>

/*
 * Parser of the algebra syntax. A line is one of
 *
 *	expression           1 + 2x
 *	name = expression    a = 3
 *	f(x, y) = expression a definition of a function (zero or more names)
 *
 * Where an expression is expected at the top of a statement, of the body of a
 * definition or after '=' there can be several statements separated by ';',
 * and a statement can be `let name = expression`, a local name:
 *
 *	f(n) = let a = n^2; let b = a + 1; b * 2
 *
 * Precedence, from loose to tight:
 *
 *	or                            the words, left associative; and binds
 *	and                           tighter than or
 *	< > <= >= == !=               comparisons (also the signs ≤ ≥ ≠); a chain
 *	                              a < b < c is (a < b) < c, a type error
 *	to                            the conversion: 3 ft to m, 5 N*m to J; it
 *	                              is the word "to", so "to" is not a name
 *	                              after an operand; left associative
 *	+ -                           left associative
 *	* / and implicit product      left associative
 *	unary - +                     -2^2 is -(2^2)
 *	^                             right associative, 2^3^2 is 2^(3^2); the
 *	                              exponent may carry a unary sign: 2^-1
 *	! and superscript digits      postfix: 3!, x², x⁻¹; they bind like a
 *	                              factor, so 2^3! is 2^(3!)
 *	( ) calls, names, numbers, √  √ takes one factor: √x, √(x+1), √√x;
 *	[ ] lists                     √x² is (√x)² (use parentheses for more)
 *
 * An implicit product is a number, ')' or a superscript directly followed by
 * a name, '(' , '[' or √ (and ')' by a number): 2x, 2(x+1), (a)(b), 2√x, x²y,
 * 2[1, 2].
 * Names are whole words, so "x y" is an error, and "x(2)" is a call; the
 * evaluator turns a call of a variable into a product.
 *
 * Parsing stops at the first problem and reports where it is.
 *
 * The tree is a vector of nodes that are linked to each other by index, never
 * by pointer. Every node knows its parent, its first child, and its previous
 * and next sibling, so walking in any direction is cheap, and a node becomes
 * a child of another without any extra storage. The siblings form a ring in
 * the backward direction: the prev link of the first child leads to the last
 * child (a single child is its own prev), so the last child is
 * nodes[parent.first].prev and appending is as cheap as prepending.
 *
 * Index 0 is the nil node. It is never part of a tree: it is what every link
 * that leads nowhere points at (a leaf has first = 0, the last sibling has
 * next = 0, the root has parent = 0). Following such a link is safe: it lands
 * on a node of kind ML_NODE_NIL, which has no children and no text, and the
 * code that evaluates the tree reports it as an error instead of crashing.
 * The first real node has index 1.
 *
 *	list                           the elements, in order: [1, x+1] has two
 *	                               children; [[1, 2], [3, 4]] is a list of
 *	                               lists (the evaluator makes vectors and
 *	                               matrices of them)
 *	number, name, superscript      leaves (the text is the node's range)
 *	neg, !, sqrt                   one child
 *	+ - * / ^ to                   two children, left then right
 *	< > <= >= == != and or         the same
 *	seq                            the statements of a body, separated by ';'
 *	                               in the text, the value is the last one
 *	index                          a name, then the position: xs[2]; the
 *	                               bracket must follow the name directly
 *	lambda                         a params node, then the body: the short
 *	                               function map (x -> x^2, xs); one name or
 *	                               (a, b) before the arrow
 *	let                            a local name (a name leaf) and its value;
 *	                               it is visible in the statements after it
 *	call                           the callee (a name), then the arguments
 *	=                              the target (a name), then the expression
 *	def                            the name, a params node holding the
 *	                               parameter names, then the body
 */

#define ML_NODE_NIL_INDEX 0

typedef enum MlNodeKind
{
	ML_NODE_NIL = 0,     /* the node at index 0, and nothing else */
	ML_NODE_NUMBER,      /* decimal literal, the text of the node */
	ML_NODE_NAME,        /* variable, constant or function name */
	/* the exponent of x², an integer in superscript digits */
	ML_NODE_SUPERSCRIPT,
	ML_NODE_NEG,         /* -a */
	ML_NODE_ADD,         /* a + b */
	ML_NODE_SUB,         /* a - b */
	ML_NODE_MUL,         /* a * b */
	ML_NODE_DIV,         /* a / b */
	ML_NODE_POW,         /* a ^ b */
	ML_NODE_FACT,        /* a! */
	ML_NODE_SQRT,        /* √a */
	ML_NODE_CALL,        /* callee (arguments...) */
	ML_NODE_ASSIGN,      /* name = expression */
	ML_NODE_FUNC_DEF,    /* name (params) = body */
	ML_NODE_PARAMS,      /* the parameter names of a definition */
	ML_NODE_LIST,        /* [a, b, c] */
	ML_NODE_TO,          /* a to b: a converted to the unit expression b */
	ML_NODE_LT,          /* a < b */
	ML_NODE_GT,          /* a > b */
	ML_NODE_LE,          /* a <= b */
	ML_NODE_GE,          /* a >= b */
	ML_NODE_EQ,          /* a == b */
	ML_NODE_NE,          /* a != b */
	ML_NODE_AND,         /* a and b */
	ML_NODE_OR,          /* a or b */
	ML_NODE_SEQ,         /* statement; statement; ... */
	ML_NODE_LET,         /* let name = expression */
	ML_NODE_INDEX,       /* name[index] */
	/* x -> body, (x, y) -> body: only as the argument of a call */
	ML_NODE_LAMBDA,
	ML_NODE_KIND_COUNT
} MlNodeKind;

typedef struct MlNode
{
	MlNodeKind kind;
	uint64_t   parent;
	uint64_t   first;  /* first child */
	uint64_t   next;   /* next sibling, 0 for the last child */
	uint64_t   prev;   /* previous sibling; for the first child the last one */
	uint64_t   height; /* levels of the subtree: 1 for a leaf, 0 for nil */
	uint64_t   start;  /* byte range of the whole node in the source */
	uint64_t   len;
	MlName     name;   /* ML_NODE_NAME: the interned name (core/names.h) */
} MlNode;

ML_VEC_DEFINE (MlNodeVec, MlNode)

typedef struct MlAst
{
	MlNodeVec nodes;
	uint64_t  root; /* the statement: an expression, = or def; 0 if empty */
	int32_t   units; /* parsed with units: "to" is the conversion operator */
	int32_t   pga;   /* 2 or 3 on a line of a pga domain, else 0 */
} MlAst;

typedef enum MlParseCode
{
	ML_PARSE_OK = 0,
	ML_PARSE_UNEXPECTED_END,   /* the text ends where more is needed */
	ML_PARSE_UNEXPECTED_TOKEN, /* a token that cannot go here */
	/* something that is not the start of an expression */
	ML_PARSE_EXPECTED_EXPR,
	ML_PARSE_BAD_CHARACTER,    /* a character that is not part of the syntax */
	ML_PARSE_BAD_SUPERSCRIPT,  /* superscript digits that are not an integer */
	/* '=' after something that cannot be assigned to */
	ML_PARSE_ASSIGN_TARGET,
	ML_PARSE_DUPLICATE_PARAM,  /* f(x, x) = ... */
	ML_PARSE_TOO_DEEP,         /* parentheses nested too deeply */
	ML_PARSE_NO_MEMORY,
	ML_PARSE_CODE_COUNT
} MlParseCode;

typedef struct MlParseError
{
	MlParseCode code;
	uint64_t    start;  /* byte range of the offending text */
	uint64_t    len;
	int32_t     at_end; /* the problem is that the text ended too early */
} MlParseError;

typedef struct MlParseResult
{
	MlParseError error; /* code ML_PARSE_OK when the parse worked */
	MlAst        ast;   /* empty on failure; free a good one with ml_ast_free */
} MlParseResult;

/*
 * Limits on the input. The parser does not recurse (it is a state machine with
 * a stack of frames on the heap, see parser.c), and neither does anything that
 * walks the tree, so these only keep the work and the memory of one line small:
 * at most ML_PARSE_MAX_DEPTH nested parentheses, calls and prefix operators,
 * and no tree higher than ML_PARSE_MAX_TREE_DEPTH levels, which also bounds
 * long chains such as 1+1+1+...
 */
#define ML_PARSE_MAX_DEPTH      200
#define ML_PARSE_MAX_TREE_DEPTH 1000

/*
 * units is 1 for a line of the physics domain: there the word "to" is the
 * conversion operator (3 ft to m) and the tree is evaluated with unit names.
 * Anywhere else "to" is a name like any other. ml_parse is ml_parse_units with
 * 0. The tree remembers it (MlAst.units), so a function defined on a physics
 * line is evaluated with units wherever it is called from.
 */
MlParseResult ml_parse_units (MlSlice source, int32_t units);

/*
 * The same for a line of a pga domain: pga is 2 or 3 (0 for none), the
 * dimension of the algebra, and the names e0 e1 e12 ... are the basis blades
 * there (pga.h). units is 0 on such a line.
 */
MlParseResult ml_parse_mode (MlSlice source, int32_t units, int32_t pga);
MlParseResult ml_parse (MlSlice source);

void ml_ast_free (MlAst *ast);

/* 1 when source is a whole valid statement (what ghost mode needs to know). */
int32_t ml_parse_complete (MlSlice source);

const char *ml_parse_error_text (MlParseCode code);

/* "parse_unexpected_token" ...: the stable machine readable name. */
const char *ml_parse_code_name (MlParseCode code);

/* The node at an index; an index that does not exist gives the nil node. */
MlNode ml_ast_node (const MlAst *ast, uint64_t index);

/* The n-th child of a node (from 0), 0 (the nil node) if there is none. */
uint64_t ml_ast_child (const MlAst *ast, uint64_t index, uint64_t n);

/* The last child of a node, 0 (the nil node) if it has none. */
uint64_t ml_ast_last_child (const MlAst *ast, uint64_t index);

/* The number of children of a node. */
uint64_t ml_ast_child_count (const MlAst *ast, uint64_t index);

/* The source text of a node. */
MlSlice ml_ast_text (const MlAst *ast, MlSlice source, uint64_t index);

/* The name number of a ML_NODE_NAME node, ML_NAME_NONE for any other. */
MlName ml_ast_name (const MlAst *ast, uint64_t index);

/*
 * The tree as text, e.g. "(+ 1 (* 2 x))": operators as written above,
 * "(neg a)", "(! a)", "(sqrt a)", "(call f a b)", "(= x e)" and
 * "(def f (x y) e)". Superscript exponents print as ASCII integers and the
 * nil node as "nil".
 */
MlResult ml_ast_dump (MlStr *out, MlSlice source, const MlAst *ast);

#endif /* MATLOG_PARSE_PARSER_H */
