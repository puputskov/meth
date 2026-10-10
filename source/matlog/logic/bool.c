#include <matlog/logic/bool.h>

#include <matlog/core/utf8.h>
#include <matlog/session/session.h>

#include <string.h>

typedef enum TokKind
{
	TK_END = 0,
	TK_NAME,
	TK_CONST,
	TK_NOT,
	TK_AND,
	TK_OR,
	TK_XOR,
	TK_IMPL,
	TK_EQUIV,
	TK_LPAREN,
	TK_RPAREN,
	TK_ASSIGN,
	TK_BAD
} TokKind;

typedef struct Token
{
	TokKind  kind;
	uint64_t start;
	uint64_t len;
	int32_t  value; /* TK_CONST: 0 or 1 */
} Token;

typedef struct Symbol
{
	const char *text;
	TokKind    kind;
	int32_t    value;
} Symbol;

/* Longer spellings first, so that `<->` is not `<` and `->`. */
static const Symbol symbols [] =
{
	{ .text = "\xE2\x88\xA7", .kind = TK_AND,    .value = 0 },
	{ .text = "\xE2\x88\xA8", .kind = TK_OR,     .value = 0 },
	{ .text = "\xC2\xAC",     .kind = TK_NOT,    .value = 0 },
	{ .text = "\xE2\x86\x92", .kind = TK_IMPL,   .value = 0 },
	{ .text = "\xE2\x86\x94", .kind = TK_EQUIV,  .value = 0 },
	{ .text = "\xE2\x8A\x95", .kind = TK_XOR,    .value = 0 },
	{ .text = "\xE2\x8A\xA4", .kind = TK_CONST,  .value = 1 },
	{ .text = "\xE2\x8A\xA5", .kind = TK_CONST,  .value = 0 },
	{ .text = "<->",          .kind = TK_EQUIV,  .value = 0 },
	{ .text = "<=>",          .kind = TK_EQUIV,  .value = 0 },
	{ .text = "==",           .kind = TK_EQUIV,  .value = 0 },
	{ .text = "->",           .kind = TK_IMPL,   .value = 0 },
	{ .text = "=>",           .kind = TK_IMPL,   .value = 0 },
	{ .text = "&&",           .kind = TK_AND,    .value = 0 },
	{ .text = "||",           .kind = TK_OR,     .value = 0 },
	{ .text = "&",            .kind = TK_AND,    .value = 0 },
	{ .text = "|",            .kind = TK_OR,     .value = 0 },
	{ .text = "^",            .kind = TK_XOR,    .value = 0 },
	{ .text = "~",            .kind = TK_NOT,    .value = 0 },
	{ .text = "!",            .kind = TK_NOT,    .value = 0 },
	{ .text = "(",            .kind = TK_LPAREN, .value = 0 },
	{ .text = ")",            .kind = TK_RPAREN, .value = 0 },
	{ .text = "=",            .kind = TK_ASSIGN, .value = 0 }
};

/* The symbol that starts at pos, or NULL. */
static const Symbol *symbol_at (MlSlice s, uint64_t pos)
{
	const Symbol *found = NULL;

	for (uint64_t i = 0; i < ml_count (symbols) && found == NULL; i++)
	{
		uint64_t n = strlen (symbols [i].text);

		if (
			   pos + n <= s.len
			&& memcmp (s.data + pos, symbols [i].text, n) == 0
		)
		{
			found = &symbols [i];
		}
	}

	return found;
}

static MlBoolSymbolKind symbol_kind (TokKind kind)
{
	MlBoolSymbolKind k = ML_BSYM_NONE;

	k = ml_choose (kind == TK_NOT, ML_BSYM_NOT, k);
	k = ml_choose (kind == TK_AND, ML_BSYM_AND, k);
	k = ml_choose (kind == TK_OR, ML_BSYM_OR, k);
	k = ml_choose (kind == TK_XOR, ML_BSYM_XOR, k);
	k = ml_choose (kind == TK_IMPL, ML_BSYM_IMPL, k);
	k = ml_choose (kind == TK_EQUIV, ML_BSYM_EQUIV, k);
	k = ml_choose (kind == TK_CONST, ML_BSYM_CONST, k);
	k = ml_choose (kind == TK_LPAREN, ML_BSYM_LPAREN, k);
	k = ml_choose (kind == TK_RPAREN, ML_BSYM_RPAREN, k);
	k = ml_choose (kind == TK_ASSIGN, ML_BSYM_ASSIGN, k);
	return k;
}

MlBoolSymbol ml_bool_symbol_at (MlSlice text, uint64_t pos)
{
	const Symbol *sym = symbol_at (text, pos);
	MlBoolSymbol  out =
	{
		.kind  = ML_BSYM_NONE,
		.len   = 0,
		.value = 0
	};

	if (sym != NULL)
	{
		out.kind  = symbol_kind (sym->kind);
		out.len   = strlen (sym->text);
		out.value = sym->value;
	}

	return out;
}

static int32_t is_name_start (uint8_t c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int32_t is_name_char (uint8_t c)
{
	return is_name_start (c) || (c >= '0' && c <= '9') || c == '\'';
}

static int32_t is_word (MlSlice w, const char *text)
{
	return ml_slice_equal (w, ml_slice_cstr (text));
}

/* A byte of a name: a letter, a digit (not first) or part of a character. */
static int32_t is_name_byte (uint8_t c, int32_t inner)
{
	return (
		   (inner && is_name_char (c))
		|| is_name_start (c)
		|| c >= 0x80
	);
}

/* The word a name token spells: and, or, not, xor, true, false. */
static Token word_token (MlSlice s, uint64_t start, uint64_t len)
{
	MlSlice w = ml_slice_sub (s, start, len);
	Token   t =
	{
		.kind  = TK_NAME,
		.start = start,
		.len   = len,
		.value = 0
	};

	t.kind = ml_choose (is_word (w, "and"), TK_AND, t.kind);
	t.kind = ml_choose (is_word (w, "or"), TK_OR, t.kind);
	t.kind = ml_choose (is_word (w, "not"), TK_NOT, t.kind);
	t.kind = ml_choose (is_word (w, "xor"), TK_XOR, t.kind);

	if (is_word (w, "true"))
	{
		t.kind  = TK_CONST;
		t.value = 1;
	}

	else if (is_word (w, "false"))
	{
		t.kind = TK_CONST;
	}

	return t;
}

MlBoolSymbol ml_bool_word (MlSlice word)
{
	Token        t   = word_token (word, 0, word.len);
	MlBoolSymbol out =
	{
		.kind  = ML_BSYM_NONE,
		.len   = 0,
		.value = 0
	};

	if (t.kind != TK_NAME)
	{
		out.kind  = symbol_kind (t.kind);
		out.len   = word.len;
		out.value = t.value;
	}

	return out;
}

static Token lex (MlSlice s, uint64_t pos)
{
	Token         t =
	{
		.kind  = TK_END,
		.start = s.len,
		.len   = 0,
		.value = 0
	};

	uint64_t      at = pos;
	const Symbol  *sym;

	while (at < s.len && (s.data [at] == ' ' || s.data [at] == '\t'))
	{
		at++;
	}

	sym = ml_choose (at < s.len, symbol_at (s, at), NULL);

	if (at >= s.len)
	{
		t.start = s.len;
	}

	else if (sym != NULL)
	{
		t.kind  = sym->kind;
		t.start = at;
		t.len   = strlen (sym->text);
		t.value = sym->value;
	}

	else if (s.data [at] == '0' || s.data [at] == '1')
	{
		t.kind  = TK_CONST;
		t.start = at;
		t.len   = 1;
		t.value = s.data [at] == '1';
	}

	else if (is_name_byte ((uint8_t)s.data [at], 0))
	{
		uint64_t end = at;

		while (
			   end < s.len
			&& symbol_at (s, end) == NULL
			&& is_name_byte ((uint8_t)s.data [end], 1)
		)
		{
			end++;
		}

		t = word_token (s, at, end - at);
	}

	else
	{
		t.kind  = TK_BAD;
		t.start = at;
		t.len   = 1;
	}

	return t;
}

typedef enum NodeKind
{
	NK_CONST = 0,
	NK_VAR,
	NK_NOT,
	NK_AND,
	NK_OR,
	NK_XOR,
	NK_IMPL,
	NK_EQUIV
} NodeKind;

typedef struct Node
{
	NodeKind kind;
	uint64_t a;     /* operands, or the index of the name for NK_VAR */
	uint64_t b;
	uint64_t start; /* NK_VAR: the name in the text */
	uint64_t len;
	int32_t  value; /* NK_CONST */
} Node;

ML_VEC_DEFINE (NodeVec, Node)
ML_VEC_DEFINE (IdxVec, uint64_t)

typedef struct Op
{
	TokKind  kind;
	uint64_t start;
	uint64_t len;
} Op;

ML_VEC_DEFINE (OpVec, Op)

typedef struct Parsed
{
	int32_t      bad;
	MlLogicError error;
	uint64_t     err_start;
	uint64_t    err_len;
	NodeVec     nodes; /* children before parents; the root is the last */
} Parsed;

static void parsed_init (Parsed *p)
{
	p->bad       = 0;
	p->error     = ML_LOGIC_OK;
	p->err_start = 0;
	p->err_len   = 0;
	NodeVec_init (&p->nodes);
}

static void fail (Parsed *p, MlLogicError error, Token t)
{
	if (!p->bad)
	{
		p->bad       = 1;
		p->error     = error;
		p->err_start = t.start;
		p->err_len   = ml_choose (t.len > 0, t.len, 1);
	}
}

static int32_t precedence (TokKind k)
{
	int32_t p = 0;

	p = ml_choose (k == TK_NOT, 6, p);
	p = ml_choose (k == TK_AND, 5, p);
	p = ml_choose (k == TK_XOR, 4, p);
	p = ml_choose (k == TK_OR, 3, p);
	p = ml_choose (k == TK_IMPL, 2, p);
	p = ml_choose (k == TK_EQUIV, 1, p);
	return p;
}

static int32_t is_binary (TokKind k)
{
	return (
		   k == TK_AND
		|| k == TK_OR
		|| k == TK_XOR
		|| k == TK_IMPL
		|| k == TK_EQUIV
	);
}

static NodeKind node_of (TokKind k)
{
	NodeKind n = NK_AND;

	n = ml_choose (k == TK_NOT, NK_NOT, n);
	n = ml_choose (k == TK_OR, NK_OR, n);
	n = ml_choose (k == TK_XOR, NK_XOR, n);
	n = ml_choose (k == TK_IMPL, NK_IMPL, n);
	n = ml_choose (k == TK_EQUIV, NK_EQUIV, n);
	return n;
}

/* The top operator is applied to the operands on the stack. */
static MlResult apply_top (NodeVec *nodes, OpVec *ops, IdxVec *out)
{
	MlResult r  = ML_OK;
	Op       op = ops->data [ops->len - 1];
	Node     n  =
	{
		.kind  = node_of (op.kind),
		.a     = 0,
		.b     = 0,
		.start = op.start,
		.len   = op.len,
		.value = 0
	};

	ops->len--;

	if (op.kind == TK_NOT && out->len >= 1)
	{
		n.a = out->data [out->len - 1];
		out->len--;
	}

	else if (out->len >= 2)
	{
		n.b = out->data [out->len - 1];
		n.a = out->data [out->len - 2];
		out->len -= 2;
	}

	r = NodeVec_push (nodes, n);

	if (r == ML_OK)
	{
		r = IdxVec_push (out, nodes->len - 1);
	}

	return r;
}

/* Parses the expression that starts at from; the nodes go to p. */
static MlResult parse (Parsed *p, MlSlice src, uint64_t from)
{
	MlResult r       = ML_OK;
	OpVec    ops;
	IdxVec   out;
	uint64_t pos     = from;
	int32_t  operand = 1;
	int32_t  done    = 0;

	OpVec_init (&ops);
	IdxVec_init (&out);

	while (r == ML_OK && !p->bad && !done)
	{
		Token t = lex (src, pos);

		pos = t.start + t.len;

		if (t.kind == TK_BAD)
		{
			fail (p, ML_LOGIC_BOOL_UNEXPECTED_CHARACTER, t);
		}

		else if (operand && (t.kind == TK_NAME || t.kind == TK_CONST))
		{
			Node n =
			{
				.kind  = ml_choose (t.kind == TK_NAME, NK_VAR, NK_CONST),
				.a     = 0,
				.b     = 0,
				.start = t.start,
				.len   = t.len,
				.value = t.value
			};

			r = NodeVec_push (&p->nodes, n);

			if (r == ML_OK)
			{
				r = IdxVec_push (&out, p->nodes.len - 1);
			}

			operand = 0;
		}

		else if (operand && (t.kind == TK_NOT || t.kind == TK_LPAREN))
		{
			Op op =
			{
				.kind  = t.kind,
				.start = t.start,
				.len   = t.len
			};

			r = OpVec_push (&ops, op);
		}

		else if (operand)
		{
			fail (p, ML_LOGIC_BOOL_EXPECTED_OPERAND, t);
		}

		else if (is_binary (t.kind))
		{
			int32_t right = t.kind == TK_IMPL;
			Op      op    =
			{
				.kind  = t.kind,
				.start = t.start,
				.len   = t.len
			};

			while (
				   r == ML_OK
				&& ops.len > 0
				&& ops.data [ops.len - 1].kind != TK_LPAREN
				&& (
					   precedence (ops.data [ops.len - 1].kind)
					>  precedence (t.kind)
					|| (
						   precedence (ops.data [ops.len - 1].kind)
						== precedence (t.kind)
						&& !right
					)
				)
			)
			{
				r = apply_top (&p->nodes, &ops, &out);
			}

			if (r == ML_OK)
			{
				r = OpVec_push (&ops, op);
			}

			operand = 1;
		}

		else if (t.kind == TK_RPAREN)
		{
			while (
				   r == ML_OK
				&& ops.len > 0
				&& ops.data [ops.len - 1].kind != TK_LPAREN
			)
			{
				r = apply_top (&p->nodes, &ops, &out);
			}

			if (r == ML_OK && ops.len == 0)
			{
				fail (p, ML_LOGIC_BOOL_UNBALANCED_PARENTHESIS, t);
			}

			else if (r == ML_OK)
			{
				ops.len--;
			}
		}

		else if (t.kind == TK_END)
		{
			while (r == ML_OK && ops.len > 0 && !p->bad)
			{
				if (ops.data [ops.len - 1].kind == TK_LPAREN)
				{
					Token lp =
					{
						.kind  = TK_LPAREN,
						.start = ops.data [ops.len - 1].start,
						.len   = 1,
						.value = 0
					};

					fail (p, ML_LOGIC_BOOL_UNBALANCED_PARENTHESIS, lp);
				}

				else
				{
					r = apply_top (&p->nodes, &ops, &out);
				}
			}

			done = 1;
		}

		else
		{
			fail (p, ML_LOGIC_BOOL_EXPECTED_OPERATOR, t);
		}

		/* the text ends where an operand is still expected */
		if (!p->bad && t.kind == TK_END && operand)
		{
			fail (p, ML_LOGIC_BOOL_EXPECTED_OPERAND, t);
		}
	}

	OpVec_free (&ops);
	IdxVec_free (&out);
	return r;
}

ML_VEC_DEFINE (WordVec, uint64_t)

typedef struct Truth
{
	uint64_t n;      /* names */
	uint64_t bits;   /* 2^n assignments */
	uint64_t words;
	WordVec  vals;   /* words per node */
} Truth;

static int32_t slice_before (MlSlice a, MlSlice b)
{
	uint64_t n   = ml_choose (a.len < b.len, a.len, b.len);
	int32_t  cmp = memcmp (a.data, b.data, n);

	return cmp < 0 || (cmp == 0 && a.len < b.len);
}

#define NAME_CAP 16

/*
 * The names of the nodes, sorted; each name node gets the index of its name
 * in `a`. *count is above ML_BOOL_MAX_VARS when there are too many.
 */
static void collect_names (
	Parsed   *p,
	MlSlice  src,
	MlSlice  names [NAME_CAP],
	uint64_t *count
)
{
	*count = 0;

	for (uint64_t i = 0; i < p->nodes.len; i++)
	{
		Node    *n    = &p->nodes.data [i];
		MlSlice name  = ml_slice_sub (src, n->start, n->len);
		int32_t known = 0;

		for (uint64_t k = 0; n->kind == NK_VAR && k < *count; k++)
		{
			known = known || ml_slice_equal (names [k], name);
		}

		if (n->kind == NK_VAR && !known)
		{
			if (*count < NAME_CAP)
			{
				names [*count] = name;
			}

			(*count)++;
		}
	}

	if (*count <= NAME_CAP)
	{
		for (uint64_t i = 1; i < *count; i++)
		{
			MlSlice cur = names [i];
			uint64_t k  = i;

			while (k > 0 && slice_before (cur, names [k - 1]))
			{
				names [k] = names [k - 1];
				k--;
			}

			names [k] = cur;
		}

		for (uint64_t i = 0; i < p->nodes.len; i++)
		{
			Node *n = &p->nodes.data [i];

			for (uint64_t k = 0; n->kind == NK_VAR && k < *count; k++)
			{
				if (
					ml_slice_equal (
						names [k],
						ml_slice_sub (src, n->start, n->len)
					)
				)
				{
					n->a = k;
				}
			}
		}
	}
}

/* The value of every node for all assignments, as bits. */
static MlResult truth_build (Truth *t, const Parsed *p, uint64_t n)
{
	MlResult r    = ML_OK;
	uint64_t mask;

	t->n     = n;
	t->bits  = (uint64_t)1 << n;
	t->words = ml_choose (t->bits < 64, 1, t->bits / 64);
	mask     = ml_choose (
		t->bits < 64,
		((uint64_t)1 << t->bits) - 1,
		~(uint64_t)0
	);
	WordVec_init (&t->vals);
	r = WordVec_reserve (&t->vals, p->nodes.len * t->words);

	for (uint64_t i = 0; i < p->nodes.len * t->words && r == ML_OK; i++)
	{
		r = WordVec_push (&t->vals, 0);
	}

	for (uint64_t i = 0; i < p->nodes.len && r == ML_OK; i++)
	{
		const Node *nd = &p->nodes.data [i];
		uint64_t   *v  = t->vals.data + i * t->words;

		for (uint64_t w = 0; w < t->words; w++)
		{
			uint64_t a  = 0;
			uint64_t b  = 0;

			if (nd->kind == NK_NOT)
			{
				a = t->vals.data [nd->a * t->words + w];
			}

			else if (nd->kind >= NK_AND)
			{
				a = t->vals.data [nd->a * t->words + w];
				b = t->vals.data [nd->b * t->words + w];
			}

			if (nd->kind == NK_CONST)
			{
				v [w] = ml_choose (nd->value != 0, mask, 0);
			}

			else if (nd->kind == NK_VAR)
			{
				uint64_t word = 0;

				for (uint64_t k = 0; k < 64 && w * 64 + k < t->bits; k++)
				{
					uint64_t assign = w * 64 + k;

					word |= ((assign >> (n - 1 - nd->a)) & 1) << k;
				}

				v [w] = word;
			}

			else if (nd->kind == NK_NOT)
			{
				v [w] = ~a & mask;
			}

			else if (nd->kind == NK_AND)
			{
				v [w] = a & b;
			}

			else if (nd->kind == NK_OR)
			{
				v [w] = a | b;
			}

			else if (nd->kind == NK_XOR)
			{
				v [w] = a ^ b;
			}

			else if (nd->kind == NK_IMPL)
			{
				v [w] = (~a | b) & mask;
			}

			else
			{
				v [w] = ~(a ^ b) & mask;
			}
		}
	}

	return r;
}

static int32_t truth_bit (const Truth *t, uint64_t node, uint64_t assign)
{
	uint64_t word = t->vals.data [node * t->words + assign / 64];

	return (int32_t)((word >> (assign % 64)) & 1);
}

typedef struct Imp
{
	uint32_t value; /* the bits that are 1 */
	uint32_t mask;  /* the bits that do not matter */
	int32_t  used;
} Imp;

ML_VEC_DEFINE (ImpVec, Imp)

static int32_t imp_has (const ImpVec *v, Imp x)
{
	int32_t found = 0;

	for (uint64_t i = 0; i < v->len && !found; i++)
	{
		found = v->data [i].value == x.value && v->data [i].mask == x.mask;
	}

	return found;
}

static int32_t one_bit (uint32_t x)
{
	return x != 0 && (x & (x - 1)) == 0;
}

static int32_t imp_covers (Imp p, uint32_t minterm)
{
	return (minterm & ~p.mask) == (p.value & ~p.mask);
}

/* Quine-McCluskey: the prime implicants of the minterms. */
static MlResult prime_implicants (ImpVec *primes, const ImpVec *minterms)
{
	MlResult r = ML_OK;
	ImpVec   cur;
	ImpVec   next;

	ImpVec_init (&cur);
	ImpVec_init (&next);

	for (uint64_t i = 0; i < minterms->len && r == ML_OK; i++)
	{
		r = ImpVec_push (&cur, minterms->data [i]);
	}

	while (r == ML_OK && cur.len > 0)
	{
		ImpVec_clear (&next);

		for (uint64_t i = 0; i < cur.len && r == ML_OK; i++)
		{
			for (uint64_t j = i + 1; j < cur.len && r == ML_OK; j++)
			{
				uint32_t diff = cur.data [i].value ^ cur.data [j].value;

				if (cur.data [i].mask == cur.data [j].mask && one_bit (diff))
				{
					Imp merged =
					{
						.value = cur.data [i].value & ~diff,
						.mask  = cur.data [i].mask | diff,
						.used  = 0
					};

					cur.data [i].used = 1;
					cur.data [j].used = 1;

					if (!imp_has (&next, merged))
					{
						r = ImpVec_push (&next, merged);
					}
				}
			}
		}

		for (uint64_t i = 0; i < cur.len && r == ML_OK; i++)
		{
			if (!cur.data [i].used && !imp_has (primes, cur.data [i]))
			{
				r = ImpVec_push (primes, cur.data [i]);
			}
		}

		ImpVec t = cur;

		cur  = next;
		next = t;
	}

	ImpVec_free (&cur);
	ImpVec_free (&next);
	return r;
}

/* The essential primes, then the prime that covers most of the rest. */
static MlResult choose_cover (
	ImpVec       *chosen,
	const ImpVec *primes,
	const ImpVec *minterms
)
{
	MlResult r       = ML_OK;
	uint8_t  *taken  = calloc (primes->len + 1, 1);
	uint8_t  *done   = calloc (minterms->len + 1, 1);
	int32_t  finished = 0;

	if (taken == NULL || done == NULL)
	{
		r = ML_ERR_NOMEM;
	}

	for (uint64_t m = 0; m < minterms->len && r == ML_OK; m++)
	{
		uint64_t count = 0;
		uint64_t which = 0;

		for (uint64_t p = 0; p < primes->len; p++)
		{
			if (imp_covers (primes->data [p], minterms->data [m].value))
			{
				count++;
				which = p;
			}
		}

		if (count == 1)
		{
			taken [which] = 1;
		}
	}

	for (uint64_t p = 0; p < primes->len && r == ML_OK; p++)
	{
		for (uint64_t m = 0; m < minterms->len; m++)
		{
			if (
				   taken [p]
				&& imp_covers (primes->data [p], minterms->data [m].value)
			)
			{
				done [m] = 1;
			}
		}
	}

	while (r == ML_OK && !finished)
	{
		uint64_t best  = 0;
		uint64_t score = 0;

		for (uint64_t p = 0; p < primes->len; p++)
		{
			uint64_t gain = 0;

			for (uint64_t m = 0; m < minterms->len && !taken [p]; m++)
			{
				gain += (
					   !done [m]
					&& imp_covers (primes->data [p], minterms->data [m].value)
				);
			}

			if (gain > score)
			{
				score = gain;
				best  = p;
			}
		}

		if (score == 0)
		{
			finished = 1;
		}

		else
		{
			taken [best] = 1;

			for (uint64_t m = 0; m < minterms->len; m++)
			{
				if (imp_covers (primes->data [best], minterms->data [m].value))
				{
					done [m] = 1;
				}
			}
		}
	}

	for (uint64_t p = 0; p < primes->len && r == ML_OK; p++)
	{
		if (taken [p])
		{
			r = ImpVec_push (chosen, primes->data [p]);
		}
	}

	/* a stable order: by the bits */
	for (uint64_t i = 1; i < chosen->len; i++)
	{
		Imp      cur = chosen->data [i];
		uint64_t k   = i;

		while (
			   k > 0
			&& (
				   chosen->data [k - 1].value > cur.value
				|| (
					   chosen->data [k - 1].value == cur.value
					&& chosen->data [k - 1].mask > cur.mask
				)
			)
		)
		{
			chosen->data [k] = chosen->data [k - 1];
			k--;
		}

		chosen->data [k] = cur;
	}

	free (taken);
	free (done);
	return r;
}

/* The sum of products of the chosen implicants, written with ∧ ∨ ¬. */
static MlResult sop_text (
	MlStr        *out,
	const ImpVec *chosen,
	const MlSlice *names,
	uint64_t     n,
	uint64_t     *literals
)
{
	MlResult r = ML_OK;

	*literals = 0;

	for (uint64_t i = 0; i < chosen->len && r == ML_OK; i++)
	{
		int32_t first = 1;

		if (i > 0)
		{
			r = ml_str_append (out, ml_slice_cstr (" \xE2\x88\xA8 "));
		}

		for (uint64_t v = 0; v < n && r == ML_OK; v++)
		{
			uint32_t bit = (uint32_t)1 << (n - 1 - v);

			if (!(chosen->data [i].mask & bit))
			{
				if (!first)
				{
					r = ml_str_append (out, ml_slice_cstr (" \xE2\x88\xA7 "));
				}

				if (r == ML_OK && !(chosen->data [i].value & bit))
				{
					r = ml_str_append (out, ml_slice_cstr ("\xC2\xAC"));
				}

				if (r == ML_OK)
				{
					r = ml_str_append (out, names [v]);
				}

				first = 0;
				(*literals)++;
			}
		}
	}

	return r;
}

/* The truth table: a header, then a row for each assignment. */
static MlResult table_text (
	MlStr         *out,
	const Truth   *t,
	const MlSlice *names,
	uint64_t      root
)
{
	MlResult r = ML_OK;

	for (uint64_t v = 0; v < t->n && r == ML_OK; v++)
	{
		r = ml_str_append (out, names [v]);

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_slice_cstr (" "));
		}
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr ("\xE2\x94\x82 f"));
	}

	for (uint64_t a = 0; a < t->bits && r == ML_OK; a++)
	{
		r = ml_str_append (out, ml_slice_cstr ("\n"));

		for (uint64_t v = 0; v < t->n && r == ML_OK; v++)
		{
			uint64_t width = ml_utf8_width (names [v]);
			int32_t  bit   = (int32_t)((a >> (t->n - 1 - v)) & 1);

			for (uint64_t k = 1; k < width && r == ML_OK; k++)
			{
				r = ml_str_append (out, ml_slice_cstr (" "));
			}

			if (r == ML_OK)
			{
				r = ml_str_append (
					out,
					ml_slice_cstr (ml_choose (bit, "1 ", "0 "))
				);
			}
		}

		if (r == ML_OK)
		{
			r = ml_str_append (
				out,
				ml_slice_cstr (
					ml_choose (
						truth_bit (t, root, a),
						"\xE2\x94\x82 1",
						"\xE2\x94\x82 0"
					)
				)
			);
		}
	}

	return r;
}

/* The expression with its defined names replaced by their definitions. */
static MlResult expand (
	MlStr               *out,
	const MlLogicDefVec *defs,
	uint64_t            count,
	MlSlice             src,
	uint64_t            from
)
{
	MlResult r      = ML_OK;
	uint64_t copied = from;
	uint64_t pos    = from;
	int32_t  done   = 0;

	while (r == ML_OK && !done)
	{
		Token t = lex (src, pos);

		pos = t.start + t.len;

		if (t.kind == TK_END || t.kind == TK_BAD)
		{
			done = 1;
		}

		else if (t.kind == TK_NAME)
		{
			MlLogicFound f = ml_logic_find (
				defs,
				count,
				ML_DOMAIN_BOOLEAN,
				ml_slice_sub (src, t.start, t.len)
			);

			if (f.found)
			{
				r = ml_str_append (
					out,
					ml_slice_sub (src, copied, t.start - copied)
				);

				if (r == ML_OK)
				{
					r = ml_str_append (out, ml_slice_cstr ("("));
				}

				if (r == ML_OK)
				{
					r = ml_str_append (out, f.text);
				}

				if (r == ML_OK)
				{
					r = ml_str_append (out, ml_slice_cstr (")"));
				}

				copied = t.start + t.len;
			}
		}
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_sub (src, copied, src.len - copied));
	}

	return r;
}

static MlSlice trim (MlSlice s)
{
	uint64_t a = 0;
	uint64_t b = s.len;

	while (a < b && (s.data [a] == ' ' || s.data [a] == '\t'))
	{
		a++;
	}

	while (b > a && (s.data [b - 1] == ' ' || s.data [b - 1] == '\t'))
	{
		b--;
	}

	return ml_slice_sub (s, a, b - a);
}

/* The value of the parsed expression: text, and the table when asked. */
static MlResult show (
	MlLogicOut    *out,
	const Parsed  *p,
	MlSlice       src,
	const MlSlice *names,
	uint64_t      n,
	int32_t       table
)
{
	MlResult r      = ML_OK;
	Truth    t;
	uint64_t root   = p->nodes.len - 1;
	uint64_t ones   = 0;
	uint64_t lits   = 0;

	r = truth_build (&t, p, n);

	for (uint64_t a = 0; a < t.bits && r == ML_OK; a++)
	{
		ones += (uint64_t)truth_bit (&t, root, a);
	}

	if (r == ML_OK && ones == t.bits)
	{
		r = ml_str_append (&out->text, ml_slice_cstr ("true"));
	}

	else if (r == ML_OK && ones == 0)
	{
		r = ml_str_append (&out->text, ml_slice_cstr ("false"));
	}

	else if (r == ML_OK)
	{
		ImpVec  minterms;
		ImpVec  primes;
		ImpVec  chosen;
		MlStr   sop;
		uint64_t written = 0;

		ImpVec_init (&minterms);
		ImpVec_init (&primes);
		ImpVec_init (&chosen);
		ml_str_init (&sop);

		for (uint64_t a = 0; a < t.bits && r == ML_OK; a++)
		{
			Imp m =
			{
				.value = (uint32_t)a,
				.mask  = 0,
				.used  = 0
			};

			if (truth_bit (&t, root, a))
			{
				r = ImpVec_push (&minterms, m);
			}
		}

		if (r == ML_OK)
		{
			r = prime_implicants (&primes, &minterms);
		}

		if (r == ML_OK)
		{
			r = choose_cover (&chosen, &primes, &minterms);
		}

		if (r == ML_OK)
		{
			r = sop_text (&sop, &chosen, names, n, &lits);
		}

		for (uint64_t i = 0; i < p->nodes.len; i++)
		{
			written += p->nodes.data [i].kind == NK_VAR;
		}

		/* what was written is shown when it is not longer */
		if (r == ML_OK && lits > written)
		{
			r = ml_str_append (&out->text, trim (src));
		}

		else if (r == ML_OK)
		{
			r = ml_str_append (&out->text, ml_str_view (sop));
		}

		ImpVec_free (&minterms);
		ImpVec_free (&primes);
		ImpVec_free (&chosen);
		ml_str_free (&sop);
	}

	if (r == ML_OK && table)
	{
		r = table_text (&out->block, &t, names, root);
	}

	WordVec_free (&t.vals);
	return r;
}

/* 1 when the token after `first` can start an operand. */
static int32_t starts_operand (Token t)
{
	return (
		   t.kind == TK_NAME
		|| t.kind == TK_CONST
		|| t.kind == TK_NOT
		|| t.kind == TK_LPAREN
	);
}

MlResult ml_bool_run (
	const MlLogicDefVec *defs,
	uint64_t            count,
	MlSlice             source,
	MlLogicOut          *out
)
{
	MlResult r        = ML_OK;
	Token    first    = lex (source, 0);
	Token    second   = lex (source, first.start + first.len);
	uint64_t from     = 0;
	int32_t  define   = 0;
	int32_t  table    = 0;
	Parsed   p;
	Parsed   q;
	MlStr    exp;

	parsed_init (&p);
	parsed_init (&q);
	ml_str_init (&exp);

	if (first.kind == TK_END)
	{
		/* an empty line has no result */
		out->value = 0;
	}

	else if (second.kind == TK_ASSIGN && first.kind != TK_NAME)
	{
		r = ml_logic_out_error (
			out,
			ML_LOGIC_BOOL_BAD_DEFINITION,
			first.start,
			first.len
		);
	}

	else
	{
		define = first.kind == TK_NAME && second.kind == TK_ASSIGN;
		table  = (
			   first.kind == TK_NAME
			&& ml_slice_equal (
				ml_slice_sub (source, first.start, first.len),
				ml_slice_cstr ("table")
			)
			&& starts_operand (second)
		);

		if (define)
		{
			from = second.start + second.len;
		}

		else if (table)
		{
			from = first.start + first.len;
		}

		r = parse (&p, source, from);

		if (r == ML_OK && p.bad)
		{
			r = ml_logic_out_error (out, p.error, p.err_start, p.err_len);
		}

		if (r == ML_OK && !p.bad)
		{
			r = expand (&exp, defs, count, source, from);
		}

		if (r == ML_OK && !p.bad)
		{
			r = parse (&q, ml_str_view (exp), 0);
		}

		if (r == ML_OK && !p.bad)
		{
			MlSlice  names [NAME_CAP];
			MlSlice  body  = ml_str_view (exp);
			uint64_t n     = 0;

			collect_names (&q, body, names, &n);

			if (q.bad)
			{
				r = ml_logic_out_error (
					out,
					ML_LOGIC_BOOL_BAD_DEFINITION,
					0,
					source.len
				);
			}

			else if (n > ML_BOOL_MAX_VARS)
			{
				r = ml_logic_out_error (
					out,
					ML_LOGIC_BOOL_TOO_MANY_NAMES,
					0,
					source.len
				);
			}

			else if (table && n > ML_BOOL_MAX_TABLE)
			{
				r = ml_logic_out_error (
					out,
					ML_LOGIC_BOOL_TABLE_TOO_BIG,
					0,
					source.len
				);
			}

			else
			{
				r = show (out, &q, body, names, n, table);

				if (r == ML_OK)
				{
					out->value = 1;
				}
			}
		}

		if (r == ML_OK && define && out->value && out->error == ML_LOGIC_OK)
		{
			r = ml_str_append (
				&out->def_name,
				ml_slice_sub (source, first.start, first.len)
			);

			if (r == ML_OK)
			{
				r = ml_str_append (&out->def_text, ml_str_view (out->text));
			}
		}
	}

	NodeVec_free (&p.nodes);
	NodeVec_free (&q.nodes);
	ml_str_free (&exp);
	return r;
}
