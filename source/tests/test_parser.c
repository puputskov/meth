#include "test.h"

#include <matlog/parse/parser.h>

#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* Is every link of the tree consistent with every other one? */
static int32_t links_are_consistent (const MlAst *ast)
{
	MlNode  nil = ml_ast_node (ast, 0);
	int32_t ok  = (
		   ast->nodes.len >= 1
		&& nil.kind == ML_NODE_NIL
		&& nil.parent == 0
		&& nil.first == 0
		&& nil.next == 0
		&& nil.prev == 0
	);

	for (uint64_t i = 1; i < ast->nodes.len && ok; i++)
	{
		MlNode   node  = ml_ast_node (ast, i);
		uint64_t last  = ml_ast_last_child (ast, i);
		uint64_t count = 0;
		uint64_t prev  = 0;

		ok = node.kind != ML_NODE_NIL;

		for (
			uint64_t c = node.first;
			c != 0 && ok;
			c = ml_ast_node (ast, c).next
		)
		{
			MlNode child = ml_ast_node (ast, c);

			ok = (
				   child.parent == i
				&& c < ast->nodes.len
				&& ml_choose (
					c == node.first,
					child.prev == last,
					child.prev == prev
				)
				&& ml_choose (child.next == 0, c == last, 1)
			);
			prev = c;
			count++;
		}

		ok = ok && count == ml_ast_child_count (ast, i);
		ok = ok && ml_choose (node.first == 0, last == 0, last == prev);
	}

	return ok;
}

/* The dump of a statement, or "error" when it does not parse. */
/* units 1: the physics domain, where "to" is the conversion operator. */
static int32_t dumps_units (
	const char *source,
	const char *expected,
	int32_t    units
)
{
	MlParseResult parsed = ml_parse_units (lit (source), units);
	MlStr         s;
	int32_t       same;

	ml_str_init (&s);

	if (parsed.error.code == ML_PARSE_OK)
	{
		ml_ast_dump (&s, lit (source), &parsed.ast);
	}

	else
	{
		ml_str_append (&s, lit ("error"));
	}

	same = (
		   ml_slice_equal (ml_str_view (s), lit (expected))
		&& (
			   parsed.error.code != ML_PARSE_OK
			|| links_are_consistent (&parsed.ast)
		)
	);

	if (!same)
	{
		printf (
			"  \"%s\" gives \"%s\", expected \"%s\"\n",
			source,
			ml_str_cstr (s),
			expected
		);
	}

	ml_str_free (&s);
	ml_ast_free (&parsed.ast);
	return same;
}

static int32_t dumps_as (const char *source, const char *expected)
{
	return dumps_units (source, expected, 1);
}

/* The algebra domain: "to" is a name. */
static int32_t dumps_alg (const char *source, const char *expected)
{
	return dumps_units (source, expected, 0);
}

static int32_t fails_with (
	const char  *source,
	MlParseCode code,
	uint64_t    start,
	uint64_t    len,
	int32_t     at_end
)
{
	MlParseResult parsed = ml_parse (lit (source));
	int32_t       same   = (
		   parsed.error.code == code
		&& parsed.error.start == start
		&& parsed.error.len == len
		&& parsed.error.at_end == at_end
		&& parsed.ast.nodes.len == 0
		&& parsed.ast.root == 0
	);

	if (!same)
	{
		printf (
			"  \"%s\": code %d at [%u, +%u) end %d, "
			"expected %d at [%u, +%u) end %d\n",
			source,
			(int32_t)parsed.error.code,
			(uint32_t)parsed.error.start,
			(uint32_t)parsed.error.len,
			parsed.error.at_end,
			(int32_t)code,
			(uint32_t)start,
			(uint32_t)len,
			at_end
		);
	}

	ml_ast_free (&parsed.ast);
	return same;
}

static void test_conversion (void)
{
	CHECK (dumps_as ("3 ft to m", "(to (* 3 ft) m)"));
	CHECK (dumps_as ("3ft to m", "(to (* 3 ft) m)"));
	CHECK (dumps_as ("x to m", "(to x m)"));
	CHECK (dumps_as ("5 to m", "(to 5 m)"));
	CHECK (dumps_as ("(2+3) to m", "(to (+ 2 3) m)"));
	CHECK (dumps_as ("5 N*m to J", "(to (* (* 5 N) m) J)"));
	CHECK (dumps_as ("60 km/h to m/s", "(to (/ (* 60 km) h) (/ m s))"));
	CHECK (dumps_as ("1 km + 5 m to m", "(to (+ (* 1 km) (* 5 m)) m)"));
	CHECK (dumps_as ("a to b to c", "(to (to a b) c)"));
	CHECK (dumps_as ("d = 3 ft to m", "(= d (to (* 3 ft) m))"));
	CHECK (dumps_as ("f(x) = x to m", "(def f (x) (to x m))"));

	/* "to" is only the operator after an operand */
	CHECK (dumps_as ("to", "to"));
	CHECK (dumps_as ("to + 1", "(+ to 1)"));
	CHECK (dumps_as ("1 + to", "(+ 1 to)"));
	CHECK (dumps_as ("tom", "tom"));
	CHECK (dumps_as ("3 tom", "(* 3 tom)"));

	CHECK (dumps_as ("3 to", "error"));

	/* in the algebra domain "to" is a name, so none of these is a conversion */
	CHECK (dumps_alg ("to", "to"));
	CHECK (dumps_alg ("to + 1", "(+ to 1)"));
	CHECK (dumps_alg ("3 to", "(* 3 to)"));
	CHECK (dumps_alg ("3 ft to m", "error"));
	CHECK (dumps_alg ("a to b", "error"));
	CHECK (dumps_as ("to m", "error"));
}

static void test_precedence (void)
{
	CHECK (dumps_as ("1", "1"));
	CHECK (dumps_as ("x", "x"));
	CHECK (dumps_as ("1+2*3", "(+ 1 (* 2 3))"));
	CHECK (dumps_as ("1*2+3", "(+ (* 1 2) 3)"));
	CHECK (dumps_as ("1+2+3", "(+ (+ 1 2) 3)"));
	CHECK (dumps_as ("1-2-3", "(- (- 1 2) 3)"));
	CHECK (dumps_as ("1-(2-3)", "(- 1 (- 2 3))"));
	CHECK (dumps_as ("a/b/c", "(/ (/ a b) c)"));
	CHECK (dumps_as ("a*b/c", "(/ (* a b) c)"));
	CHECK (dumps_as ("(1+2)*3", "(* (+ 1 2) 3)"));
	CHECK (dumps_as ("((1))", "1"));

	/* powers are right associative and tighter than a sign on the left */
	CHECK (dumps_as ("2^3^2", "(^ 2 (^ 3 2))"));
	CHECK (dumps_as ("-2^2", "(neg (^ 2 2))"));
	CHECK (dumps_as ("2^-2", "(^ 2 (neg 2))"));
	CHECK (dumps_as ("-2^-2", "(neg (^ 2 (neg 2)))"));
	CHECK (dumps_as ("2^-2^2", "(^ 2 (neg (^ 2 2)))"));
	CHECK (dumps_as ("2*3^2", "(* 2 (^ 3 2))"));
	CHECK (dumps_as ("(2*3)^2", "(^ (* 2 3) 2)"));

	/* signs */
	CHECK (dumps_as ("-x", "(neg x)"));
	CHECK (dumps_as ("+x", "x"));
	CHECK (dumps_as ("--x", "(neg (neg x))"));
	CHECK (dumps_as ("-(-x)", "(neg (neg x))"));
	CHECK (dumps_as ("1--2", "(- 1 (neg 2))"));
	CHECK (dumps_as ("1+-2", "(+ 1 (neg 2))"));
	CHECK (dumps_as ("-1+2", "(+ (neg 1) 2)"));

	/* factorial */
	CHECK (dumps_as ("3!", "(! 3)"));
	CHECK (dumps_as ("3!!", "(! (! 3))"));
	CHECK (dumps_as ("-3!", "(neg (! 3))"));
	CHECK (dumps_as ("2^3!", "(^ 2 (! 3))"));
	CHECK (dumps_as ("(1+2)!", "(! (+ 1 2))"));
	CHECK (dumps_as ("3!*2", "(* (! 3) 2)"));
}

static void test_implicit_product (void)
{
	CHECK (dumps_as ("2x", "(* 2 x)"));
	CHECK (dumps_as ("2 x", "(* 2 x)"));
	CHECK (dumps_as ("2pi", "(* 2 pi)"));
	CHECK (dumps_as ("2(x+1)", "(* 2 (+ x 1))"));
	CHECK (dumps_as ("(a)(b)", "(* a b)"));
	CHECK (dumps_as ("(a)(b)(c)", "(* (* a b) c)"));
	CHECK (dumps_as ("2(3)(4)", "(* (* 2 3) 4)"));
	CHECK (dumps_as ("(a)2", "(* a 2)"));
	CHECK (dumps_as ("(a)x", "(* a x)"));
	CHECK (dumps_as ("2x^2", "(* 2 (^ x 2))"));
	CHECK (dumps_as ("2x+3y", "(+ (* 2 x) (* 3 y))"));
	CHECK (dumps_as ("1/2x", "(* (/ 1 2) x)"));
	CHECK (dumps_as ("2\xE2\x88\x9A" "x", "(* 2 (sqrt x))"));

	/* a name followed by ( is a call, and letters are never multiplied */
	CHECK (dumps_as ("x(2)", "(call x 2)"));
	CHECK (dumps_as ("x y", "error"));
	CHECK (dumps_as ("xy", "xy"));
	CHECK (dumps_as ("2 3", "error"));
	CHECK (dumps_as ("x 2", "error"));
	CHECK (dumps_as ("x\xE2\x88\x9A" "2", "error"));
	CHECK (dumps_as ("f(x)(y)", "(* (call f x) y)"));
	CHECK (dumps_as ("f(x)2", "(* (call f x) 2)"));
	CHECK (dumps_as ("2f(x)", "(* 2 (call f x))"));
}

static void test_unicode_forms (void)
{
	CHECK (
		dumps_as (
			"2\xC3\x97" "3\xC3\xB7" "4\xE2\x88\x92" "5\xC2\xB7" "6",
			"(- (/ (* 2 3) 4) (* 5 6))"
		)
	);
	CHECK (dumps_as ("2*3/4-5*6", "(- (/ (* 2 3) 4) (* 5 6))"));
	CHECK (dumps_as ("2\xCF\x80", "(* 2 \xCF\x80)"));
	CHECK (dumps_as ("\xCE\xB1+\xCE\xB2", "(+ \xCE\xB1 \xCE\xB2)"));

	/* superscripts */
	CHECK (dumps_as ("x\xC2\xB2", "(^ x 2)"));
	CHECK (dumps_as ("x^2", "(^ x 2)"));
	CHECK (dumps_as ("x\xE2\x81\xBB\xC2\xB9", "(^ x -1)"));
	CHECK (dumps_as ("x\xC2\xB9\xE2\x81\xB0", "(^ x 10)"));
	CHECK (dumps_as ("(x+1)\xC2\xB2", "(^ (+ x 1) 2)"));
	CHECK (dumps_as ("2x\xC2\xB2", "(* 2 (^ x 2))"));
	CHECK (dumps_as ("x\xC2\xB2y", "(* (^ x 2) y)"));
	CHECK (dumps_as ("x\xC2\xB2^3", "(^ (^ x 2) 3)"));
	CHECK (dumps_as ("f(x)\xC2\xB2", "(^ (call f x) 2)"));
	CHECK (dumps_as ("x\xC2\xB3!", "(! (^ x 3))"));

	/* square roots */
	CHECK (dumps_as ("\xE2\x88\x9A" "x", "(sqrt x)"));
	CHECK (dumps_as ("\xE2\x88\x9A" "2", "(sqrt 2)"));
	CHECK (dumps_as ("\xE2\x88\x9A(x+1)", "(sqrt (+ x 1))"));
	CHECK (dumps_as ("\xE2\x88\x9A\xE2\x88\x9A" "x", "(sqrt (sqrt x))"));
	CHECK (dumps_as ("\xE2\x88\x9A-x", "(sqrt (neg x))"));
	CHECK (dumps_as ("\xE2\x88\x9A" "f(x)", "(sqrt (call f x))"));
	CHECK (dumps_as ("\xE2\x88\x9A" "x\xC2\xB2", "(^ (sqrt x) 2)"));
	CHECK (dumps_as ("\xE2\x88\x9A" "x^2", "(^ (sqrt x) 2)"));
	CHECK (dumps_as ("\xE2\x88\x9A" "2x", "(* (sqrt 2) x)"));
	CHECK (dumps_as ("-\xE2\x88\x9A" "x", "(neg (sqrt x))"));
	CHECK (dumps_as ("1+\xE2\x88\x9A" "x", "(+ 1 (sqrt x))"));
}

static void test_calls (void)
{
	CHECK (dumps_as ("f(x)", "(call f x)"));
	CHECK (dumps_as ("f(x, y)", "(call f x y)"));
	CHECK (dumps_as ("f()", "(call f)"));
	CHECK (dumps_as ("f(g(x))", "(call f (call g x))"));
	CHECK (dumps_as ("f(1+2, 3*4)", "(call f (+ 1 2) (* 3 4))"));
	CHECK (dumps_as ("sqrt(x)", "(call sqrt x)"));
	CHECK (dumps_as ("max(1, 2, 3)", "(call max 1 2 3)"));
	CHECK (dumps_as ("f(x)+g(y)", "(+ (call f x) (call g y))"));
	CHECK (dumps_as ("f((1, 2))", "error"));
}

static void test_statements (void)
{
	CHECK (dumps_as ("x = 3", "(= x 3)"));
	CHECK (dumps_as ("x=3", "(= x 3)"));
	CHECK (dumps_as ("area = 3.14 r^2", "(= area (* 3.14 (^ r 2)))"));
	CHECK (dumps_as ("f(x) = 2x", "(def f (x) (* 2 x))"));
	CHECK (dumps_as ("f(x, y) = x + y", "(def f (x y) (+ x y))"));
	CHECK (dumps_as ("f() = 1", "(def f () 1)"));
	CHECK (dumps_as ("g(a, b, c) = a*b*c", "(def g (a b c) (* (* a b) c))"));
	CHECK (dumps_as ("f(x) = g(x)", "(def f (x) (call g x))"));
	CHECK (dumps_as ("\xCF\x80" "2 = 1", "(= \xCF\x80" "2 1)"));
}

static void test_tree_shape (void)
{
	MlSlice       source = lit ("1 + 23");
	MlParseResult parsed = ml_parse (source);
	MlAst         *ast   = &parsed.ast;
	MlNode        root;
	MlNode        right;

	CHECK (parsed.error.code == ML_PARSE_OK);

	/*
	 * node 0 is nil, real nodes start at 1: 1 (node 1), 23 (node 2), + (node 3)
	 */
	CHECK (ast->nodes.len == 4);
	CHECK (ast->root == 3);
	root = ml_ast_node (ast, ast->root);
	CHECK (root.kind == ML_NODE_ADD);
	CHECK (root.start == 0 && root.len == 6);
	CHECK (root.parent == 0 && root.next == 0);
	CHECK (root.first == 1);
	CHECK (ml_ast_child       (ast, 3, 0) == 1);
	CHECK (ml_ast_child       (ast, 3, 1) == 2);
	CHECK (ml_ast_child       (ast, 3, 2) == 0);
	CHECK (ml_ast_last_child  (ast, 3) == 2);
	CHECK (ml_ast_child_count (ast, 3) == 2);
	CHECK (ml_ast_node (ast, 1).next == 2 && ml_ast_node (ast, 2).prev == 1);
	CHECK (ml_ast_node        (ast, 1).prev == 2);
	CHECK (ml_ast_node        (ast, 2).next == 0);
	CHECK (
		ml_ast_node (ast, 1).parent == 3 && ml_ast_node (ast, 2).parent == 3
	);

	right = ml_ast_node (ast, 2);
	CHECK (right.kind == ML_NODE_NUMBER && right.start == 4 && right.len == 2);
	CHECK (ml_slice_equal     (ml_ast_text (ast, source, 2), lit ("23")));
	CHECK (ml_slice_equal     (ml_ast_text (ast, source, 3), lit ("1 + 23")));
	CHECK (ml_ast_child_count (ast, 1) == 0);
	CHECK (ml_ast_last_child  (ast, 1) == 0);

	ml_ast_free (&parsed.ast);
}

static void test_ranges (void)
{
	MlSlice       source = lit ("f(x, y) = (1 + 2)!");
	MlParseResult parsed = ml_parse (source);
	MlAst         *ast   = &parsed.ast;
	uint64_t      params;
	uint64_t      body;

	CHECK (parsed.error.code == ML_PARSE_OK);
	CHECK (ml_ast_node        (ast, ast->root).kind == ML_NODE_FUNC_DEF);
	CHECK (ml_ast_child_count (ast, ast->root) == 3);

	CHECK (
		ml_slice_equal (
			ml_ast_text (ast, source, ml_ast_child (ast, ast->root, 0)),
			lit ("f")
		)
	);
	params = ml_ast_child (ast, ast->root, 1);
	CHECK (ml_ast_node        (ast, params).kind == ML_NODE_PARAMS);
	CHECK (ml_slice_equal (ml_ast_text (ast, source, params), lit ("(x, y)")));
	CHECK (ml_ast_child_count (ast, params) == 2);
	CHECK (
		ml_slice_equal (
			ml_ast_text (ast, source, ml_ast_child (ast, params, 1)),
			lit ("y")
		)
	);

	body = ml_ast_child (ast, ast->root, 2);
	CHECK (ml_ast_node (ast, body).kind == ML_NODE_FACT);
	CHECK (
		ml_slice_equal (ml_ast_text (ast, source, body), lit ("(1 + 2)!")) == 0
	);
	CHECK (
		ml_ast_node (
			ast,
			body
		).start == 10 || ml_ast_node (ast, body).start == 11
	);
	CHECK (ml_ast_node (ast, body).start + ml_ast_node (ast, body).len == 18);

	CHECK (
		ml_slice_equal (
			ml_ast_text (ast, source, ast->root),
			lit ("f(x, y) = (1 + 2)!")
		)
	);
	CHECK (ml_ast_node (ast, ast->root).start == 0);
	CHECK (ml_ast_node (ast, ast->root).len == 18);
	ml_ast_free (&parsed.ast);

	/* a call covers the name, the arguments and the closing parenthesis */
	source = lit ("1 + f(2, 3) * 4");
	parsed = ml_parse (source);
	CHECK (parsed.error.code == ML_PARSE_OK);
	{
		uint64_t mul  = ml_ast_child (&parsed.ast, parsed.ast.root, 1);
		uint64_t call = ml_ast_child (&parsed.ast, mul, 0);

		CHECK (ml_ast_node (&parsed.ast, call).kind == ML_NODE_CALL);
		CHECK (
			ml_slice_equal (
				ml_ast_text (&parsed.ast, source, call),
				lit ("f(2, 3)")
			)
		);
		CHECK (ml_ast_child_count (&parsed.ast, call) == 3);
	}

	ml_ast_free (&parsed.ast);

	/* a superscript exponent is a leaf with the digits as text */
	source = lit ("x\xC2\xB2");
	parsed = ml_parse (source);
	CHECK (parsed.error.code == ML_PARSE_OK);
	CHECK (
		ml_ast_node (
			&parsed.ast,
			ml_ast_child (&parsed.ast, parsed.ast.root, 1)
		).kind == ML_NODE_SUPERSCRIPT
	);
	ml_ast_free (&parsed.ast);
}

static void test_nil_node (void)
{
	MlParseResult parsed = ml_parse (lit ("1+2"));
	MlStr         s;

	ml_str_init (&s);

	/* a link that leads nowhere is safe to follow and shows as the nil node */
	CHECK (ml_ast_node        (&parsed.ast, 0).kind == ML_NODE_NIL);
	CHECK (ml_ast_node        (&parsed.ast, 999).kind == ML_NODE_NIL);
	CHECK (ml_ast_node        (&parsed.ast, 999).first == 0);
	CHECK (ml_ast_child       (&parsed.ast, 0, 0) == 0);
	CHECK (ml_ast_child       (&parsed.ast, 999, 3) == 0);
	CHECK (ml_ast_last_child  (&parsed.ast, 0) == 0);
	CHECK (ml_ast_child_count (&parsed.ast, 0) == 0);
	CHECK (ml_ast_text        (&parsed.ast, lit ("1+2"), 0).len == 0);

	/* the first real node is 1 */
	CHECK (ml_ast_node (&parsed.ast, 1).kind != ML_NODE_NIL);
	CHECK (parsed.ast.root >= 1);

	/* dumping nil, or a tree whose root is nil, prints "nil" */
	parsed.ast.root = 0;
	CHECK (ml_ast_dump (&s, lit ("1+2"), &parsed.ast) == ML_OK);
	CHECK (strcmp      (ml_str_cstr (s), "nil") == 0);

	ml_str_free (&s);
	ml_ast_free (&parsed.ast);

	/* an empty tree is safe as well */
	CHECK (ml_ast_node  (&parsed.ast, 0).kind == ML_NODE_NIL);
	CHECK (ml_ast_child (&parsed.ast, 5, 1) == 0);
}

static void test_errors (void)
{
	CHECK (fails_with ("", ML_PARSE_UNEXPECTED_END, 0, 0, 1));
	CHECK (fails_with ("   ", ML_PARSE_UNEXPECTED_END, 3, 0, 1));
	CHECK (fails_with ("1 +", ML_PARSE_UNEXPECTED_END, 3, 0, 1));
	CHECK (fails_with ("1 + )", ML_PARSE_EXPECTED_EXPR, 4, 1, 0));
	CHECK (fails_with ("1 + * 2", ML_PARSE_EXPECTED_EXPR, 4, 1, 0));
	CHECK (fails_with (")", ML_PARSE_EXPECTED_EXPR, 0, 1, 0));
	CHECK (fails_with ("(1", ML_PARSE_UNEXPECTED_END, 2, 0, 1));
	CHECK (fails_with ("(1 2", ML_PARSE_UNEXPECTED_TOKEN, 3, 1, 0));
	CHECK (fails_with ("1 2", ML_PARSE_UNEXPECTED_TOKEN, 2, 1, 0));
	CHECK (fails_with ("x y", ML_PARSE_UNEXPECTED_TOKEN, 2, 1, 0));
	CHECK (fails_with ("1 + 2 )", ML_PARSE_UNEXPECTED_TOKEN, 6, 1, 0));
	CHECK (fails_with ("2^", ML_PARSE_UNEXPECTED_END, 2, 0, 1));
	CHECK (fails_with ("-", ML_PARSE_UNEXPECTED_END, 1, 0, 1));
	CHECK (fails_with ("\xE2\x88\x9A", ML_PARSE_UNEXPECTED_END, 3, 0, 1));

	/* characters that are not part of the syntax */
	CHECK (fails_with ("1 $ 2", ML_PARSE_BAD_CHARACTER, 2, 1, 0));
	CHECK (fails_with ("$", ML_PARSE_BAD_CHARACTER, 0, 1, 0));
	CHECK (fails_with ("1 + $", ML_PARSE_BAD_CHARACTER, 4, 1, 0));
	CHECK (fails_with ("a \xE2\x89\x88 b", ML_PARSE_BAD_CHARACTER, 2, 3, 0));
	CHECK (fails_with ("5.", ML_PARSE_BAD_CHARACTER, 1, 1, 0));

	/* calls */
	CHECK (fails_with ("f(1,)", ML_PARSE_EXPECTED_EXPR, 4, 1, 0));
	CHECK (fails_with ("f(,1)", ML_PARSE_EXPECTED_EXPR, 2, 1, 0));
	CHECK (fails_with ("f(1", ML_PARSE_UNEXPECTED_END, 3, 0, 1));
	CHECK (fails_with ("f(1 2)", ML_PARSE_UNEXPECTED_TOKEN, 4, 1, 0));

	/* statements */
	CHECK (fails_with ("x = ", ML_PARSE_UNEXPECTED_END, 4, 0, 1));
	CHECK (fails_with ("= 3", ML_PARSE_EXPECTED_EXPR, 0, 1, 0));
	CHECK (fails_with ("f(x) =", ML_PARSE_UNEXPECTED_END, 6, 0, 1));
	CHECK (fails_with ("3 = 4", ML_PARSE_ASSIGN_TARGET, 2, 1, 0));
	CHECK (fails_with ("x + 1 = 2", ML_PARSE_ASSIGN_TARGET, 6, 1, 0));
	CHECK (fails_with ("x = y = 3", ML_PARSE_ASSIGN_TARGET, 6, 1, 0));
	CHECK (fails_with ("f(1) = 2", ML_PARSE_ASSIGN_TARGET, 5, 1, 0));
	CHECK (fails_with ("f(x+1) = 2", ML_PARSE_ASSIGN_TARGET, 7, 1, 0));
	CHECK (fails_with ("f(x, x) = 1", ML_PARSE_DUPLICATE_PARAM, 5, 1, 0));
	CHECK (fails_with ("f(a, b, a) = 1", ML_PARSE_DUPLICATE_PARAM, 8, 1, 0));

	/* a superscript that is not a plain integer */
	CHECK (
		fails_with (
			"x\xC2\xB2\xE2\x81\xBB\xC2\xB3",
			ML_PARSE_BAD_SUPERSCRIPT,
			1,
			7,
			0
		)
	);
}

/* open n times, then body, then close n times. */
static void wrap (
	MlStr      *s,
	const char *open,
	uint32_t   n,
	const char *body,
	const char *close
)
{
	ml_str_clear (s);

	for (uint32_t i = 0; i < n; i++)
	{
		ml_str_append (s, lit (open));
	}

	ml_str_append (s, lit (body));

	for (uint32_t i = 0; i < n; i++)
	{
		ml_str_append (s, lit (close));
	}
}

/* Parses s and, when that works, prints the tree (a recursive walk). */
static MlParseCode parse_and_dump (const MlStr *s)
{
	MlParseResult parsed = ml_parse (ml_str_view (*s));
	MlStr         text;

	ml_str_init (&text);

	if (parsed.error.code == ML_PARSE_OK)
	{
		CHECK (ml_ast_dump (&text, ml_str_view (*s), &parsed.ast) == ML_OK);
		CHECK (links_are_consistent (&parsed.ast));
	}

	ml_str_free (&text);
	ml_ast_free (&parsed.ast);
	return parsed.error.code;
}

static void test_limits (void)
{
	MlStr s;

	ml_str_init (&s);

	/* parentheses: the limit is fine, one more is refused */
	wrap (&s, "(", ML_PARSE_MAX_DEPTH, "1", ")");
	CHECK (parse_and_dump (&s) == ML_PARSE_OK);
	wrap (&s, "(", ML_PARSE_MAX_DEPTH + 1, "1", ")");
	CHECK (parse_and_dump     (&s) == ML_PARSE_TOO_DEEP);
	CHECK (!ml_parse_complete (ml_str_view (s)));

	/* prefix signs, calls, roots and powers nest the same way */
	wrap (&s, "-", ML_PARSE_MAX_DEPTH, "x", "");
	CHECK (parse_and_dump (&s) == ML_PARSE_OK);
	wrap (&s, "-", ML_PARSE_MAX_DEPTH + 1, "x", "");
	CHECK (parse_and_dump (&s) == ML_PARSE_TOO_DEEP);

	wrap (&s, "f(", ML_PARSE_MAX_DEPTH, "1", ")");
	CHECK (parse_and_dump (&s) == ML_PARSE_OK);
	wrap (&s, "f(", ML_PARSE_MAX_DEPTH + 1, "1", ")");
	CHECK (parse_and_dump (&s) == ML_PARSE_TOO_DEEP);

	wrap (&s, "\xE2\x88\x9A", 150, "x", "");
	CHECK (parse_and_dump (&s) == ML_PARSE_OK);
	wrap (&s, "\xE2\x88\x9A", 400, "x", "");
	CHECK (parse_and_dump (&s) == ML_PARSE_TOO_DEEP);

	wrap (&s, "2^", 150, "2", "");
	CHECK (parse_and_dump (&s) == ML_PARSE_OK);
	wrap (&s, "2^", 400, "2", "");
	CHECK (parse_and_dump (&s) == ML_PARSE_TOO_DEEP);

	wrap (&s, "(f(-", 60, "x", "))");
	CHECK (parse_and_dump (&s) == ML_PARSE_OK);

	/* long chains of operators make a high tree: the height is limited */
	wrap (&s, "", 1, "1", "");

	for (uint32_t i = 0; i < ML_PARSE_MAX_TREE_DEPTH - 100; i++)
	{
		ml_str_append (&s, lit ("+1"));
	}

	CHECK (parse_and_dump (&s) == ML_PARSE_OK);

	for (uint32_t i = 0; i < 200; i++)
	{
		ml_str_append (&s, lit ("+1"));
	}

	CHECK (parse_and_dump (&s) == ML_PARSE_TOO_DEEP);
	CHECK (ml_parse       (ml_str_view (s)).ast.nodes.len == 0);

	/* the same for products and a very long input */
	wrap (&s, "", 1, "2", "");

	for (uint32_t i = 0; i < 100000; i++)
	{
		ml_str_append (&s, lit ("*2"));
	}

	CHECK (parse_and_dump (&s) == ML_PARSE_TOO_DEEP);

	ml_str_free (&s);
}

/* The height of a node is the number of levels of its subtree. */
static void test_heights (void)
{
	MlParseResult parsed = ml_parse (lit ("1 + 2 * x"));

	CHECK (parsed.error.code == ML_PARSE_OK);
	CHECK (ml_ast_node (&parsed.ast, 0).height == 0);
	CHECK (ml_ast_node (&parsed.ast, 1).height == 1);
	CHECK (ml_ast_node (&parsed.ast, parsed.ast.root).height == 3);
	CHECK (
		ml_ast_node (
			&parsed.ast,
			ml_ast_child (&parsed.ast, parsed.ast.root, 1)
		).height == 2
	);
	ml_ast_free (&parsed.ast);
}

typedef void (*TestFn) (void);

#ifdef _WIN32

static TestFn g_thread_test;

static DWORD WINAPI thread_main (LPVOID unused)
{
	(void)unused;
	g_thread_test ();
	return 0;
}

/*
 * Runs fn on a thread whose stack is 512 KB, half the default: if the parser
 * (or the tree walks of these tests) used too much stack, this crashes.
 */
static void run_on_small_stack (TestFn fn)
{
	HANDLE thread;

	g_thread_test = fn;
	thread        = CreateThread (
		NULL,
		512 * 1024,
		thread_main,
		NULL,
		STACK_SIZE_PARAM_IS_A_RESERVATION,
		NULL
	);

	CHECK (thread != NULL);

	if (thread != NULL)
	{
		WaitForSingleObject (thread, INFINITE);
		CloseHandle (thread);
	}
}

#else

static void run_on_small_stack (TestFn fn)
{
	fn ();
}

#endif

static void test_complete (void)
{
	CHECK (ml_parse_complete  (lit ("2+3")));
	CHECK (ml_parse_complete  (lit ("x = 3")));
	CHECK (ml_parse_complete  (lit ("f(x) = x")));
	CHECK (ml_parse_complete  (lit ("  7  ")));
	CHECK (!ml_parse_complete (lit ("")));
	CHECK (!ml_parse_complete (lit ("2+")));
	CHECK (!ml_parse_complete (lit ("(3")));
	CHECK (!ml_parse_complete (lit ("2 3")));
	CHECK (!ml_parse_complete (lit ("f(")));
	CHECK (!ml_parse_complete (lit ("x =")));

	/* incomplete input is told apart from wrong input */
	{
		MlParseResult a = ml_parse (lit ("2+"));
		MlParseResult b = ml_parse (lit ("2 3"));

		CHECK (a.error.at_end && !b.error.at_end);
	}

	CHECK (strlen (ml_parse_error_text (ML_PARSE_UNEXPECTED_END)) > 0);
	CHECK (
		strcmp (ml_parse_error_text ((MlParseCode)99), "unknown error") == 0
	);

	for (uint32_t k = 0; k < ML_PARSE_CODE_COUNT; k++)
	{
		CHECK (strlen (ml_parse_error_text ((MlParseCode)k)) > 0);
	}
}

static void test_lists (void)
{
	CHECK (dumps_as ("[1]", "(list 1)"));
	CHECK (dumps_as ("[1, 2, 3]", "(list 1 2 3)"));
	CHECK (dumps_as ("[1+2, x*3]", "(list (+ 1 2) (* x 3))"));
	CHECK (dumps_as ("[[1,2],[3,4]]", "(list (list 1 2) (list 3 4))"));
	CHECK (
		dumps_as ("[ [1 , 2] , [3 , 4] ]", "(list (list 1 2) (list 3 4))")
	);
	CHECK (dumps_as ("[f(1), (2)]", "(list (call f 1) 2)"));

	/* lists are operands like any other */
	CHECK (dumps_as ("[1,2] + [3,4]", "(+ (list 1 2) (list 3 4))"));
	CHECK (dumps_as ("2[1,2]", "(* 2 (list 1 2))"));
	CHECK (dumps_as ("2 * [1,2]", "(* 2 (list 1 2))"));
	CHECK (
		dumps_as ("A = [[1,2],[3,4]]", "(= A (list (list 1 2) (list 3 4)))")
	);
	CHECK (dumps_as ("A^T", "(^ A T)"));
	CHECK (dumps_as ("A^-1", "(^ A (neg 1))"));
	CHECK (
		dumps_as (
			"[[1,2],[3,4]]^-1",
			"(^ (list (list 1 2) (list 3 4)) (neg 1))"
		)
	);
	CHECK (dumps_as ("[1,2]!", "(! (list 1 2))"));

	/* errors */
	CHECK (fails_with ("[]", ML_PARSE_EXPECTED_EXPR, 1, 1, 0));
	CHECK (fails_with ("[1,", ML_PARSE_UNEXPECTED_END, 3, 0, 1));
	CHECK (fails_with ("[1, 2", ML_PARSE_UNEXPECTED_END, 5, 0, 1));
	CHECK (fails_with ("[1 2]", ML_PARSE_UNEXPECTED_TOKEN, 3, 1, 0));
	CHECK (fails_with ("[1,]", ML_PARSE_EXPECTED_EXPR, 3, 1, 0));
	CHECK (fails_with ("1]", ML_PARSE_UNEXPECTED_TOKEN, 1, 1, 0));
	CHECK (fails_with ("(1]", ML_PARSE_UNEXPECTED_TOKEN, 2, 1, 0));
	CHECK (fails_with ("[1)", ML_PARSE_UNEXPECTED_TOKEN, 2, 1, 0));
	CHECK (fails_with ("x [1]", ML_PARSE_UNEXPECTED_TOKEN, 2, 1, 0));
}

static void test_logic (void)
{
	CHECK (dumps_as ("a < b", "(< a b)"));
	CHECK (dumps_as ("a <= b", "(<= a b)"));
	CHECK (dumps_as ("a == b", "(== a b)"));
	CHECK (dumps_as ("a != b", "(!= a b)"));

	/* arithmetic binds tighter, to is between the two */
	CHECK (dumps_as ("a + 1 > b * 2", "(> (+ a 1) (* b 2))"));
	CHECK (dumps_as ("3 m to cm < 5", "(< (to (* 3 m) cm) 5)"));

	/* and binds tighter than or, comparisons tighter than both */
	CHECK (dumps_as ("a < b and c < d", "(and (< a b) (< c d))"));
	CHECK (dumps_as ("a or b and c", "(or a (and b c))"));
	CHECK (dumps_as ("a and b or c", "(or (and a b) c)"));
	CHECK (dumps_as ("a or b or c", "(or (or a b) c)"));
	CHECK (dumps_as ("(a or b) and c", "(and (or a b) c)"));
	CHECK (dumps_as ("a < b < c", "(< (< a b) c)"));

	/* the words are names where no operator can follow */
	CHECK (dumps_as ("and + or", "(+ and or)"));
	CHECK (dumps_as ("f(a, b) = a and b", "(def f (a b) (and a b))"));
	CHECK (dumps_as ("ok = a > b", "(= ok (> a b))"));
	CHECK (fails_with ("a <", ML_PARSE_UNEXPECTED_END, 3, 0, 1));
	CHECK (fails_with ("a and", ML_PARSE_UNEXPECTED_END, 5, 0, 1));
}

static void test_sequences (void)
{
	/* one statement is itself, several are a seq */
	CHECK (dumps_as ("a + 1", "(+ a 1)"));
	CHECK (dumps_as ("1; 2", "(seq 1 2)"));
	CHECK (dumps_as ("1; 2; 3", "(seq 1 2 3)"));
	CHECK (dumps_as ("let a = 2; a * a", "(seq (let a 2) (* a a))"));
	CHECK (
		dumps_as (
			"let a = 1; let b = a + 1; b",
			"(seq (let a 1) (let b (+ a 1)) b)"
		)
	);
	CHECK (dumps_as ("let a = 2", "(let a 2)"));

	/* in the body of a definition and after = */
	CHECK (
		dumps_as (
			"f(n) = let a = n^2; a + 1",
			"(def f (n) (seq (let a (^ n 2)) (+ a 1)))"
		)
	);
	CHECK (dumps_as ("x = 1; 2", "(= x (seq 1 2))"));

	/* the ranges: a seq covers its statements, a let its name and value */
	CHECK (fails_with ("1;", ML_PARSE_UNEXPECTED_END, 2, 0, 1));
	CHECK (fails_with ("1; ;", ML_PARSE_EXPECTED_EXPR, 3, 1, 0));
	CHECK (fails_with ("let a = ", ML_PARSE_UNEXPECTED_END, 8, 0, 1));
	CHECK (fails_with ("(1; 2)", ML_PARSE_UNEXPECTED_TOKEN, 2, 1, 0));

	/* let is a name where it does not start a let */
	CHECK (dumps_as ("let + 1", "(+ let 1)"));
	CHECK (dumps_as ("let = 3", "(= let 3)"));
}

static void test_index (void)
{
	CHECK (dumps_as ("xs[2]", "(index xs 2)"));
	CHECK (dumps_as ("xs[i + 1]", "(index xs (+ i 1))"));
	CHECK (dumps_as ("xs[1] + xs[2]", "(+ (index xs 1) (index xs 2))"));
	CHECK (dumps_as ("2 xs[1]", "(* 2 (index xs 1))"));
	CHECK (dumps_as ("xs[-1]", "(index xs (neg 1))"));
	CHECK (dumps_as ("xs[ys[1]]", "(index xs (index ys 1))"));
	CHECK (dumps_as ("f(x) = xs[x]", "(def f (x) (index xs x))"));

	/* the bracket must touch the name */
	CHECK (fails_with ("xs [1]", ML_PARSE_UNEXPECTED_TOKEN, 3, 1, 0));
	CHECK (fails_with ("xs[", ML_PARSE_UNEXPECTED_END, 3, 0, 1));
	CHECK (fails_with ("xs[1", ML_PARSE_UNEXPECTED_END, 4, 0, 1));
	CHECK (fails_with ("xs[]", ML_PARSE_EXPECTED_EXPR, 3, 1, 0));
	CHECK (fails_with ("xs[1,2]", ML_PARSE_UNEXPECTED_TOKEN, 4, 1, 0));
}

static void test_lambdas (void)
{
	CHECK (
		dumps_as (
			"map(x -> x^2, xs)",
			"(call map (lambda (x) (^ x 2)) xs)"
		)
	);
	CHECK (
		dumps_as (
			"fold((a, b) -> a + b, 0, xs)",
			"(call fold (lambda (a b) (+ a b)) 0 xs)"
		)
	);
	CHECK (dumps_as ("f(x -> x)", "(call f (lambda (x) x))"));
	CHECK (
		dumps_as (
			"f(1, x -> x + 1, 2)",
			"(call f 1 (lambda (x) (+ x 1)) 2)"
		)
	);
	CHECK (
		dumps_as (
			"f(x -> g(y -> y))",
			"(call f (lambda (x) (call g (lambda (y) y))))"
		)
	);
	CHECK (
		dumps_as (
			"f(x -> x, y -> y)",
			"(call f (lambda (x) x) (lambda (y) y))"
		)
	);

	/* the body is a whole expression up to the comma */
	CHECK (
		dumps_as (
			"f(x -> x < 2 and x > 0, 1)",
			"(call f (lambda (x) (and (< x 2) (> x 0))) 1)"
		)
	);

	/* a lambda is only an argument; a minus and a greater than are not one */
	CHECK (fails_with ("x -> x", ML_PARSE_UNEXPECTED_TOKEN, 2, 2, 0));
	CHECK (fails_with ("f(x ->)", ML_PARSE_EXPECTED_EXPR, 6, 1, 0));
	CHECK (fails_with ("f((a, a) -> a)", ML_PARSE_DUPLICATE_PARAM, 6, 1, 0));
	CHECK (fails_with ("f(() -> 1)", ML_PARSE_EXPECTED_EXPR, 3, 1, 0));
	CHECK (dumps_as ("f((x) -> x)", "(call f (lambda (x) x))"));
}

void run_parser_tests (void)
{
	test_precedence ();
	test_conversion ();
	test_implicit_product ();
	test_unicode_forms ();
	test_calls ();
	test_statements ();
	test_tree_shape ();
	test_ranges ();
	test_nil_node ();
	test_errors ();
	test_lists ();
	test_logic ();
	test_sequences ();
	test_index ();
	test_lambdas ();
	run_on_small_stack (test_limits);
	test_heights ();
	test_complete ();
}
