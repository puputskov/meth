#include <matlog/logic/lambda.h>

#include <matlog/session/session.h>

#include <string.h>

#define MAX_NODES 2000000

typedef enum LKind
{
	LK_VAR = 0, /* a = de Bruijn index */
	LK_FREE,    /* a = name; also a name not yet resolved */
	LK_ABS,     /* a = body, b = name of the binder (a hint once resolved) */
	LK_APP      /* a = function, b = argument */
} LKind;

typedef struct LNode
{
	LKind    kind;
	uint64_t a;
	uint64_t b;
} LNode;

ML_VEC_DEFINE (LNodeVec, LNode)
ML_VEC_DEFINE (LIdxVec, uint64_t)
ML_VEC_DEFINE (LNameVec, MlStr)

/* Names are kept once; a node refers to them by number. */
static MlResult intern (LNameVec *names, MlSlice name, uint64_t *id)
{
	MlResult r     = ML_OK;
	int32_t  found = 0;

	for (uint64_t i = 0; i < names->len && !found; i++)
	{
		if (ml_slice_equal (ml_str_view (names->data [i]), name))
		{
			found = 1;
			*id   = i;
		}
	}

	if (!found)
	{
		MlStr copy;

		ml_str_init (&copy);
		r = ml_str_append (&copy, name);

		if (r == ML_OK)
		{
			r   = LNameVec_push (names, copy);
			*id = names->len - 1;
		}

		if (r != ML_OK)
		{
			ml_str_free (&copy);
		}
	}

	return r;
}

static void names_free (LNameVec *names)
{
	for (uint64_t i = 0; i < names->len; i++)
	{
		ml_str_free (&names->data [i]);
	}

	LNameVec_free (names);
}

typedef enum TokKind
{
	TK_END = 0,
	TK_NAME,
	TK_NUMBER,
	TK_LAMBDA,
	TK_DOT,
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
} Token;

#define LAMBDA_UTF8 "\xCE\xBB"

static int32_t is_lambda_at (MlSlice s, uint64_t pos)
{
	return (
		   (pos + 2 <= s.len && memcmp (s.data + pos, LAMBDA_UTF8, 2) == 0)
		|| (pos < s.len && s.data [pos] == '\\')
	);
}

static int32_t is_name_byte (MlSlice s, uint64_t pos)
{
	uint8_t c = (uint8_t)s.data [pos];

	return (
		   !is_lambda_at (s, pos)
		&& (
			   (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9')
			|| c == '_'
			|| c == '\''
			|| c >= 0x80
		)
	);
}

static Token lex (MlSlice s, uint64_t pos)
{
	Token    t  =
	{
		.kind  = TK_END,
		.start = s.len,
		.len   = 0
	};

	uint64_t at = pos;

	while (at < s.len && (s.data [at] == ' ' || s.data [at] == '\t'))
	{
		at++;
	}

	if (at >= s.len)
	{
		t.start = s.len;
	}

	else if (is_lambda_at (s, at))
	{
		t.kind  = TK_LAMBDA;
		t.start = at;
		t.len   = ml_choose (s.data [at] == '\\', 1, 2);
	}

	else if (strchr (".()=", s.data [at]) != NULL)
	{
		t.kind  = ml_choose (
			s.data [at] == '.',
			TK_DOT,
			ml_choose (
				s.data [at] == '(',
				TK_LPAREN,
				ml_choose (s.data [at] == ')', TK_RPAREN, TK_ASSIGN)
			)
		);
		t.start = at;
		t.len   = 1;
	}

	else if (is_name_byte (s, at))
	{
		uint64_t end    = at;
		int32_t  digits = 1;

		while (end < s.len && is_name_byte (s, end))
		{
			digits = digits && s.data [end] >= '0' && s.data [end] <= '9';
			end++;
		}

		t.kind  = ml_choose (digits, TK_NUMBER, TK_NAME);
		t.start = at;
		t.len   = end - at;
	}

	else
	{
		t.kind  = TK_BAD;
		t.start = at;
		t.len   = 1;
	}

	return t;
}

typedef struct Parser
{
	int32_t      bad;
	MlLogicError error;
	uint64_t     err_start;
	uint64_t     err_len;
	LNodeVec     nodes; /* the named tree, children first */
	LNameVec     names;
} Parser;

static void parser_init (Parser *p)
{
	p->bad       = 0;
	p->error     = ML_LOGIC_OK;
	p->err_start = 0;
	p->err_len   = 0;
	LNodeVec_init (&p->nodes);
	LNameVec_init (&p->names);
}

static void parser_free (Parser *p)
{
	LNodeVec_free (&p->nodes);
	names_free (&p->names);
}

static void fail (Parser *p, MlLogicError error, Token t)
{
	if (!p->bad)
	{
		p->bad       = 1;
		p->error     = error;
		p->err_start = t.start;
		p->err_len   = ml_choose (t.len > 0, t.len, 1);
	}
}

static MlResult add_node (
	Parser   *p,
	LKind    kind,
	uint64_t a,
	uint64_t b,
	uint64_t *id
)
{
	LNode  n =
	{
		.kind = kind,
		.a    = a,
		.b    = b
	};

	MlResult r = LNodeVec_push (&p->nodes, n);

	*id = p->nodes.len - 1;
	return r;
}

/* The Church numeral n as a named term. */
static MlResult numeral (Parser *p, uint64_t n, uint64_t *root)
{
	MlResult r = ML_OK;
	uint64_t f = 0;
	uint64_t x = 0;
	uint64_t v = 0;

	r = intern (&p->names, ml_slice_cstr ("f"), &f);

	if (r == ML_OK)
	{
		r = intern (&p->names, ml_slice_cstr ("x"), &x);
	}

	if (r == ML_OK)
	{
		r = add_node (p, LK_FREE, x, 0, &v);
	}

	for (uint64_t i = 0; i < n && r == ML_OK; i++)
	{
		uint64_t fv = 0;

		r = add_node (p, LK_FREE, f, 0, &fv);

		if (r == ML_OK)
		{
			r = add_node (p, LK_APP, fv, v, &v);
		}
	}

	if (r == ML_OK)
	{
		r = add_node (p, LK_ABS, v, x, &v);
	}

	if (r == ML_OK)
	{
		r = add_node (p, LK_ABS, v, f, root);
	}

	return r;
}

typedef enum CtxKind
{
	CX_TOP = 0,
	CX_PAREN,
	CX_LAMBDA
} CtxKind;

typedef struct Ctx
{
	CtxKind  kind;
	uint64_t base;   /* the first of its items */
	uint64_t params; /* CX_LAMBDA: the first of its names in the name list */
	uint64_t count;  /* and how many */
	Token    token;  /* the ( or the λ, for the messages */
} Ctx;

ML_VEC_DEFINE (CtxVec, Ctx)

/* The items from base on become one application, left associative. */
static MlResult fold (Parser *p, LIdxVec *items, uint64_t base, uint64_t *root)
{
	MlResult r   = ML_OK;
	uint64_t cur = items->data [base];

	for (uint64_t i = base + 1; i < items->len && r == ML_OK; i++)
	{
		r = add_node (p, LK_APP, cur, items->data [i], &cur);
	}

	items->len = base;
	*root      = cur;
	return r;
}

/* Closes the context on top: its term is an item of the one below. */
static MlResult close_top (
	Parser   *p,
	CtxVec   *ctxs,
	LIdxVec  *items,
	LIdxVec  *params
)
{
	MlResult r   = ML_OK;
	Ctx      c   = ctxs->data [ctxs->len - 1];
	uint64_t t   = 0;

	if (items->len == c.base)
	{
		fail (p, ML_LOGIC_LAMBDA_EXPECTED_TERM, c.token);
	}

	else
	{
		r = fold (p, items, c.base, &t);
	}

	for (uint64_t i = c.count; i > 0 && r == ML_OK && !p->bad; i--)
	{
		r = add_node (p, LK_ABS, t, params->data [c.params + i - 1], &t);
	}

	params->len = c.params;
	ctxs->len--;

	if (r == ML_OK && !p->bad)
	{
		r = LIdxVec_push (items, t);
	}

	return r;
}

/* Parses the term that starts at from; *root is the last node of p->nodes. */
static MlResult parse (Parser *p, MlSlice src, uint64_t from, uint64_t *root)
{
	MlResult r    = ML_OK;
	CtxVec   ctxs;
	LIdxVec  items;
	LIdxVec  params;
	uint64_t pos  = from;
	int32_t  done = 0;
	Ctx      top  =
	{
		.kind   = CX_TOP,
		.base   = 0,
		.params = 0,
		.count  = 0,
		.token  = { .kind = TK_END, .start = 0, .len = 0 }
	};

	CtxVec_init (&ctxs);
	LIdxVec_init (&items);
	LIdxVec_init (&params);
	r = CtxVec_push (&ctxs, top);

	while (r == ML_OK && !p->bad && !done)
	{
		Token t = lex (src, pos);

		pos = t.start + t.len;

		if (t.kind == TK_BAD)
		{
			fail (p, ML_LOGIC_LAMBDA_UNEXPECTED_CHARACTER, t);
		}

		else if (t.kind == TK_NAME || t.kind == TK_NUMBER)
		{
			uint64_t id   = 0;
			uint64_t node = 0;
			uint64_t n    = 0;

			for (uint64_t i = 0; t.kind == TK_NUMBER && i < t.len; i++)
			{
				n = ml_choose (
					n > ML_LAMBDA_MAX_NUMBER,
					n,
					n * 10 + (uint64_t)(src.data [t.start + i] - '0')
				);
			}

			if (t.kind == TK_NUMBER && n > ML_LAMBDA_MAX_NUMBER)
			{
				fail (p, ML_LOGIC_LAMBDA_NUMBER_TOO_BIG, t);
			}

			else if (t.kind == TK_NUMBER)
			{
				r = numeral (p, n, &node);
			}

			else
			{
				r = intern (&p->names, ml_slice_sub (src, t.start, t.len), &id);

				if (r == ML_OK)
				{
					r = add_node (p, LK_FREE, id, 0, &node);
				}
			}

			if (r == ML_OK && !p->bad)
			{
				r = LIdxVec_push (&items, node);
			}
		}

		else if (t.kind == TK_LPAREN)
		{
			Ctx c =
			{
				.kind   = CX_PAREN,
				.base   = items.len,
				.params = params.len,
				.count  = 0,
				.token  = t
			};

			r = CtxVec_push (&ctxs, c);
		}

		else if (t.kind == TK_LAMBDA)
		{
			Ctx      c     =
			{
				.kind   = CX_LAMBDA,
				.base   = items.len,
				.params = params.len,
				.count  = 0,
				.token  = t
			};

			Token    n     = lex (src, pos);

			while (r == ML_OK && n.kind == TK_NAME)
			{
				uint64_t id = 0;

				r = intern (&p->names, ml_slice_sub (src, n.start, n.len), &id);

				if (r == ML_OK)
				{
					r = LIdxVec_push (&params, id);
				}

				c.count++;
				pos = n.start + n.len;
				n   = lex (src, pos);
			}

			if (r == ML_OK && (c.count == 0 || n.kind != TK_DOT))
			{
				fail (
					p,
					ML_LOGIC_LAMBDA_EXPECTED_PARAMETER,
					ml_choose (n.kind == TK_END, t, n)
				);
			}

			else if (r == ML_OK)
			{
				pos = n.start + n.len;
				r   = CtxVec_push (&ctxs, c);
			}
		}

		else if (t.kind == TK_RPAREN)
		{
			while (
				   r == ML_OK
				&& !p->bad
				&& ctxs.len > 1
				&& ctxs.data [ctxs.len - 1].kind == CX_LAMBDA
			)
			{
				r = close_top (p, &ctxs, &items, &params);
			}

			if (
				   r == ML_OK
				&& !p->bad
				&& ctxs.data [ctxs.len - 1].kind != CX_PAREN
			)
			{
				fail (p, ML_LOGIC_LAMBDA_UNBALANCED_PARENTHESIS, t);
			}

			else if (r == ML_OK && !p->bad)
			{
				r = close_top (p, &ctxs, &items, &params);
			}
		}

		else if (t.kind == TK_END)
		{
			while (r == ML_OK && !p->bad && ctxs.len > 1)
			{
				if (ctxs.data [ctxs.len - 1].kind == CX_PAREN)
				{
					fail (
						p,
						ML_LOGIC_LAMBDA_UNBALANCED_PARENTHESIS,
						ctxs.data [ctxs.len - 1].token
					);
				}

				else
				{
					r = close_top (p, &ctxs, &items, &params);
				}
			}

			if (r == ML_OK && !p->bad && items.len == 0)
			{
				fail (p, ML_LOGIC_LAMBDA_EXPECTED_TERM, t);
			}

			else if (r == ML_OK && !p->bad)
			{
				r = fold (p, &items, 0, root);
			}

			done = 1;
		}

		else
		{
			fail (p, ML_LOGIC_LAMBDA_UNEXPECTED_CHARACTER, t);
		}
	}

	CtxVec_free (&ctxs);
	LIdxVec_free (&items);
	LIdxVec_free (&params);
	return r;
}

typedef struct Frame
{
	uint64_t node;   /* in the named tree */
	uint64_t floor;  /* binders below it are not seen from here */
	int32_t  phase;
	int32_t  fixed;  /* names are not replaced by definitions */
} Frame;

ML_VEC_DEFINE (FrameVec, Frame)

static MlResult push_db (
	LNodeVec *db,
	LKind    kind,
	uint64_t a,
	uint64_t b,
	uint64_t *id
)
{
	LNode    n =
	{
		.kind = kind,
		.a    = a,
		.b    = b
	};

	MlResult r = ML_OK;

	if (db->len >= MAX_NODES)
	{
		r = ML_ERR_RANGE;
	}

	else
	{
		r   = LNodeVec_push (db, n);
		*id = db->len - 1;
	}

	return r;
}

/*
 * The named tree becomes a de Bruijn term. A name no binder owns is replaced by
 * its definition, parsed into the same tree, when it has one. *root is the
 * term. ML_ERR_RANGE: the term got too big.
 */
static MlResult resolve (
	Parser              *p,
	LNodeVec            *db,
	uint64_t            named_root,
	const MlLogicDefVec *defs,
	uint64_t            count,
	uint64_t            *root
)
{
	MlResult r = ML_OK;
	FrameVec st;
	LIdxVec  res;
	LIdxVec  binders;
	Frame    start =
	{
		.node  = named_root,
		.floor = 0,
		.phase = 0,
		.fixed = 0
	};

	FrameVec_init (&st);
	LIdxVec_init (&res);
	LIdxVec_init (&binders);
	r = FrameVec_push (&st, start);

	while (r == ML_OK && st.len > 0)
	{
		Frame f = st.data [st.len - 1];
		LNode n = p->nodes.data [f.node];

		st.len--;

		if (n.kind == LK_FREE && f.phase == 0)
		{
			uint64_t found = 0;
			int32_t  hit   = 0;
			uint64_t id    = 0;

			for (uint64_t i = binders.len; i > f.floor && !hit; i--)
			{
				if (binders.data [i - 1] == n.a)
				{
					hit   = 1;
					found = binders.len - i;
				}
			}

			if (hit)
			{
				r = push_db (db, LK_VAR, found, 0, &id);

				if (r == ML_OK)
				{
					r = LIdxVec_push (&res, id);
				}
			}

			else
			{
				MlLogicFound d = ml_logic_find (
					defs,
					count,
					ML_DOMAIN_LAMBDA,
					ml_str_view (p->names.data [n.a])
				);

				if (d.found && !f.fixed)
				{
					uint64_t sub = 0;
					Parser   q;
					uint64_t base = p->nodes.len;
					Frame    next =
					{
						.node  = 0,
						.floor = binders.len,
						.phase = 0,
						.fixed = 1
					};

					/* the definition is parsed into the tree of p */
					parser_init (&q);
					q.nodes = p->nodes;
					q.names = p->names;
					r       = parse (&q, d.text, 0, &sub);
					p->nodes = q.nodes;
					p->names = q.names;
					next.node = sub;

					if (r == ML_OK && !q.bad && p->nodes.len > base)
					{
						r = FrameVec_push (&st, next);
					}

					else if (r == ML_OK)
					{
						r = ML_ERR_RANGE;
					}
				}

				else
				{
					r = push_db (db, LK_FREE, n.a, 0, &id);

					if (r == ML_OK)
					{
						r = LIdxVec_push (&res, id);
					}
				}
			}
		}

		else if (n.kind == LK_ABS && f.phase == 0)
		{
			Frame again = f;
			Frame body  =
			{
				.node  = n.a,
				.floor = f.floor,
				.phase = 0,
				.fixed = f.fixed
			};

			again.phase = 1;
			r = LIdxVec_push (&binders, n.b);

			if (r == ML_OK)
			{
				r = FrameVec_push (&st, again);
			}

			if (r == ML_OK)
			{
				r = FrameVec_push (&st, body);
			}
		}

		else if (n.kind == LK_ABS)
		{
			uint64_t id = 0;
			uint64_t b  = res.data [res.len - 1];

			res.len--;
			binders.len--;
			r = push_db (db, LK_ABS, b, n.b, &id);

			if (r == ML_OK)
			{
				r = LIdxVec_push (&res, id);
			}
		}

		else if (n.kind == LK_APP && f.phase == 0)
		{
			Frame again = f;
			Frame fun   = f;
			Frame arg   = f;

			again.phase = 1;
			fun.node    = n.a;
			fun.phase   = 0;
			arg.node    = n.b;
			arg.phase   = 0;
			r = FrameVec_push (&st, again);

			if (r == ML_OK)
			{
				r = FrameVec_push (&st, arg);
			}

			if (r == ML_OK)
			{
				r = FrameVec_push (&st, fun);
			}
		}

		else
		{
			uint64_t id = 0;
			uint64_t a  = res.data [res.len - 2];
			uint64_t b  = res.data [res.len - 1];

			res.len -= 2;
			r = push_db (db, LK_APP, a, b, &id);

			if (r == ML_OK)
			{
				r = LIdxVec_push (&res, id);
			}
		}
	}

	if (r == ML_OK)
	{
		*root = res.data [0];
	}

	FrameVec_free (&st);
	LIdxVec_free (&res);
	LIdxVec_free (&binders);
	return r;
}

typedef struct TFrame
{
	uint64_t node;
	uint64_t depth;
	int32_t  phase;
} TFrame;

ML_VEC_DEFINE (TFrameVec, TFrame)

/*
 * The term with its free indices raised by `by`, as a new term: one pass
 * without recursion, the nodes wait on a stack until their children are
 * done.
 */
static MlResult shift_term (
	LNodeVec *db,
	uint64_t t,
	uint64_t by,
	uint64_t *out
)
{
	MlResult  r = ML_OK;
	TFrameVec st;
	LIdxVec   res;
	TFrame    start =
	{
		.node  = t,
		.depth = 0,
		.phase = 0
	};

	TFrameVec_init (&st);
	LIdxVec_init (&res);
	*out = t;

	if (by > 0)
	{
		r = TFrameVec_push (&st, start);
	}

	while (r == ML_OK && st.len > 0)
	{
		TFrame f = st.data [st.len - 1];
		LNode  n = db->data [f.node];

		st.len--;

		if (n.kind == LK_VAR || n.kind == LK_FREE)
		{
			uint64_t id = f.node;

			if (n.kind == LK_VAR && n.a >= f.depth)
			{
				r = push_db (db, LK_VAR, n.a + by, 0, &id);
			}

			if (r == ML_OK)
			{
				r = LIdxVec_push (&res, id);
			}
		}

		else if (n.kind == LK_ABS && f.phase == 0)
		{
			TFrame again = f;
			TFrame body  =
			{
				.node  = n.a,
				.depth = f.depth + 1,
				.phase = 0
			};

			again.phase = 1;
			r = TFrameVec_push (&st, again);

			if (r == ML_OK)
			{
				r = TFrameVec_push (&st, body);
			}
		}

		else if (n.kind == LK_ABS)
		{
			uint64_t id = 0;
			uint64_t b  = res.data [res.len - 1];

			res.len--;
			r = push_db (db, LK_ABS, b, n.b, &id);

			if (r == ML_OK)
			{
				r = LIdxVec_push (&res, id);
			}
		}

		else if (f.phase == 0)
		{
			TFrame again = f;
			TFrame fun   = f;
			TFrame argf  = f;

			again.phase = 1;
			fun.node    = n.a;
			argf.node   = n.b;
			r = TFrameVec_push (&st, again);

			if (r == ML_OK)
			{
				r = TFrameVec_push (&st, argf);
			}

			if (r == ML_OK)
			{
				r = TFrameVec_push (&st, fun);
			}
		}

		else
		{
			uint64_t id = 0;
			uint64_t a  = res.data [res.len - 2];
			uint64_t b  = res.data [res.len - 1];

			res.len -= 2;
			r = push_db (db, LK_APP, a, b, &id);

			if (r == ML_OK)
			{
				r = LIdxVec_push (&res, id);
			}
		}
	}

	if (r == ML_OK && by > 0)
	{
		*out = res.data [0];
	}

	TFrameVec_free (&st);
	LIdxVec_free (&res);
	return r;
}

/*
 * The body of a binder with `arg` put in place of its variable: index 0 of
 * the body becomes arg (shifted to the depth it stands at), the free
 * indices above come down by one.
 */
static MlResult substitute (
	LNodeVec *db,
	uint64_t body,
	uint64_t arg,
	uint64_t *out
)
{
	MlResult  r = ML_OK;
	TFrameVec st;
	LIdxVec   res;
	TFrame    start =
	{
		.node  = body,
		.depth = 0,
		.phase = 0
	};

	TFrameVec_init (&st);
	LIdxVec_init (&res);
	r = TFrameVec_push (&st, start);

	while (r == ML_OK && st.len > 0)
	{
		TFrame f = st.data [st.len - 1];
		LNode  n = db->data [f.node];

		st.len--;

		if (n.kind == LK_VAR || n.kind == LK_FREE)
		{
			uint64_t id = f.node;

			if (n.kind == LK_VAR && n.a == f.depth)
			{
				r = shift_term (db, arg, f.depth, &id);
			}

			else if (n.kind == LK_VAR && n.a > f.depth)
			{
				r = push_db (db, LK_VAR, n.a - 1, 0, &id);
			}

			if (r == ML_OK)
			{
				r = LIdxVec_push (&res, id);
			}
		}

		else if (n.kind == LK_ABS && f.phase == 0)
		{
			TFrame again = f;
			TFrame inner =
			{
				.node  = n.a,
				.depth = f.depth + 1,
				.phase = 0
			};

			again.phase = 1;
			r = TFrameVec_push (&st, again);

			if (r == ML_OK)
			{
				r = TFrameVec_push (&st, inner);
			}
		}

		else if (n.kind == LK_ABS)
		{
			uint64_t id = 0;
			uint64_t b  = res.data [res.len - 1];

			res.len--;
			r = push_db (db, LK_ABS, b, n.b, &id);

			if (r == ML_OK)
			{
				r = LIdxVec_push (&res, id);
			}
		}

		else if (f.phase == 0)
		{
			TFrame again = f;
			TFrame fun   = f;
			TFrame argf  = f;

			again.phase = 1;
			fun.node    = n.a;
			argf.node   = n.b;
			r = TFrameVec_push (&st, again);

			if (r == ML_OK)
			{
				r = TFrameVec_push (&st, argf);
			}

			if (r == ML_OK)
			{
				r = TFrameVec_push (&st, fun);
			}
		}

		else
		{
			uint64_t id = 0;
			uint64_t a  = res.data [res.len - 2];
			uint64_t b  = res.data [res.len - 1];

			res.len -= 2;
			r = push_db (db, LK_APP, a, b, &id);

			if (r == ML_OK)
			{
				r = LIdxVec_push (&res, id);
			}
		}
	}

	if (r == ML_OK)
	{
		*out = res.data [0];
	}

	TFrameVec_free (&st);
	LIdxVec_free (&res);
	return r;
}

typedef struct Step
{
	uint64_t node;
	int32_t  which; /* 0: the function (or the body), 1: the argument */
} Step;

ML_VEC_DEFINE (StepVec, Step)

/*
 * One step of normal order reduction: the leftmost outermost redex. *root is
 * replaced by the new term; *reduced is 0 when there is no redex.
 */
static MlResult reduce_once (LNodeVec *db, uint64_t *root, int32_t *reduced)
{
	MlResult r      = ML_OK;
	StepVec  path;
	uint64_t cur    = *root;
	int32_t  found  = 0;
	int32_t  ended  = 0;

	StepVec_init (&path);
	*reduced = 0;

	while (r == ML_OK && !found && !ended)
	{
		LNode n = db->data [cur];

		if (n.kind == LK_APP && db->data [n.a].kind == LK_ABS)
		{
			found = 1;
		}

		else if (n.kind == LK_APP || n.kind == LK_ABS)
		{
			Step s =
			{
				.node  = cur,
				.which = 0
			};

			r   = StepVec_push (&path, s);
			cur = n.a;
		}

		else
		{
			/* a leaf: on to the next argument that is still to be visited */
			int32_t moved = 0;

			while (path.len > 0 && !moved)
			{
				Step *top = &path.data [path.len - 1];

				if (db->data [top->node].kind == LK_APP && top->which == 0)
				{
					top->which = 1;
					cur        = db->data [top->node].b;
					moved      = 1;
				}

				else
				{
					path.len--;
				}
			}

			ended = !moved;
		}
	}

	if (r == ML_OK && found)
	{
		LNode    redex = db->data [cur];
		LNode    lam   = db->data [redex.a];
		uint64_t now   = 0;

		r = substitute (db, lam.a, redex.b, &now);

		for (uint64_t i = path.len; i > 0 && r == ML_OK; i--)
		{
			Step  s = path.data [i - 1];
			LNode a = db->data [s.node];

			if (a.kind == LK_ABS)
			{
				r = push_db (db, LK_ABS, now, a.b, &now);
			}

			else if (s.which == 0)
			{
				r = push_db (db, LK_APP, now, a.b, &now);
			}

			else
			{
				r = push_db (db, LK_APP, a.a, now, &now);
			}
		}

		if (r == ML_OK)
		{
			*root    = now;
			*reduced = 1;
		}
	}

	StepVec_free (&path);
	return r;
}

typedef enum PKind
{
	PK_NODE = 0,
	PK_TEXT,
	PK_POP
} PKind;

typedef struct PTask
{
	PKind      kind;
	uint64_t   node; /* PK_NODE; PK_POP: how many binders */
	int32_t    ctx;  /* 0 top, 1 function of an application, 2 argument */
	const char *text;
} PTask;

ML_VEC_DEFINE (PTaskVec, PTask)

static PTask task_node (uint64_t node, int32_t ctx)
{
	PTask t = { .kind = PK_NODE, .node = node, .ctx = ctx, .text = "" };

	return t;
}

static PTask task_text (const char *text)
{
	PTask t = { .kind = PK_TEXT, .node = 0, .ctx = 0, .text = text };

	return t;
}

static PTask task_pop (uint64_t count)
{
	PTask t = { .kind = PK_POP, .node = count, .ctx = 0, .text = "" };

	return t;
}

/* 1 when the name cand is used in body (as a free name, or an outer binder). */
static MlResult mentions (
	const LNodeVec  *db,
	const LNameVec  *names,
	const LNameVec  *scope,
	uint64_t        body,
	MlSlice         cand,
	int32_t         *found
)
{
	MlResult  r = ML_OK;
	TFrameVec st;
	TFrame    start =
	{
		.node  = body,
		.depth = 0,
		.phase = 0
	};

	*found = 0;
	TFrameVec_init (&st);
	r = TFrameVec_push (&st, start);

	while (r == ML_OK && st.len > 0 && !*found)
	{
		TFrame f = st.data [st.len - 1];
		LNode  n = db->data [f.node];

		st.len--;

		if (n.kind == LK_FREE)
		{
			*found = ml_slice_equal (ml_str_view (names->data [n.a]), cand);
		}

		else if (n.kind == LK_VAR && n.a > f.depth)
		{
			uint64_t outer = n.a - f.depth - 1;

			if (outer < scope->len)
			{
				*found = ml_slice_equal (
					ml_str_view (scope->data [scope->len - 1 - outer]),
					cand
				);
			}
		}

		else if (n.kind == LK_ABS)
		{
			TFrame body_f =
			{
				.node  = n.a,
				.depth = f.depth + 1,
				.phase = 0
			};

			r = TFrameVec_push (&st, body_f);
		}

		else if (n.kind == LK_APP)
		{
			TFrame a =
			{
				.node  = n.a,
				.depth = f.depth,
				.phase = 0
			};

			TFrame b =
			{
				.node  = n.b,
				.depth = f.depth,
				.phase = 0
			};

			r = TFrameVec_push (&st, a);

			if (r == ML_OK)
			{
				r = TFrameVec_push (&st, b);
			}
		}
	}

	TFrameVec_free (&st);
	return r;
}

/* The name for a binder: its hint, with ' added while it would capture. */
static MlResult binder_name (
	MlStr          *name,
	const LNodeVec *db,
	const LNameVec *names,
	const LNameVec *scope,
	LNode          abs
)
{
	MlResult r     = ml_str_append (name, ml_str_view (names->data [abs.b]));
	int32_t  found = 1;

	while (r == ML_OK && found)
	{
		r = mentions (db, names, scope, abs.a, ml_str_view (*name), &found);

		if (r == ML_OK && found)
		{
			r = ml_str_append (name, ml_slice_cstr ("'"));
		}
	}

	return r;
}

/* Writes the term as text, with as few parentheses as it needs. */
static MlResult print_term (
	MlStr          *out,
	const LNodeVec *db,
	const LNameVec *names,
	uint64_t       root
)
{
	MlResult r = ML_OK;
	PTaskVec st;
	LNameVec scope;
	PTask    start =
	{
		.kind = PK_NODE,
		.node = root,
		.ctx  = 0,
		.text = ""
	};

	PTaskVec_init (&st);
	LNameVec_init (&scope);
	r = PTaskVec_push (&st, start);

	while (r == ML_OK && st.len > 0)
	{
		PTask t = st.data [st.len - 1];

		st.len--;

		if (t.kind == PK_TEXT)
		{
			r = ml_str_append (out, ml_slice_cstr (t.text));
		}

		else if (t.kind == PK_POP)
		{
			for (uint64_t i = 0; i < t.node; i++)
			{
				ml_str_free (&scope.data [scope.len - 1]);
				scope.len--;
			}
		}

		else
		{
			LNode n = db->data [t.node];

			if (n.kind == LK_FREE)
			{
				r = ml_str_append (out, ml_str_view (names->data [n.a]));
			}

			else if (n.kind == LK_VAR)
			{
				r = ml_choose (
					n.a < scope.len,
					ml_str_append (
						out,
						ml_str_view (scope.data [scope.len - 1 - n.a])
					),
					ml_str_append (out, ml_slice_cstr ("?"))
				);
			}

			else if (n.kind == LK_APP)
			{
				int32_t parens = t.ctx == 2;
				PTask   close  = task_text (")");
				PTask   space  = task_text (" ");
				PTask   arg    = task_node (n.b, 2);
				PTask   fun    = task_node (n.a, 1);

				if (parens)
				{
					r = ml_str_append (out, ml_slice_cstr ("("));
				}

				if (r == ML_OK && parens)
				{
					r = PTaskVec_push (&st, close);
				}

				if (r == ML_OK)
				{
					r = PTaskVec_push (&st, arg);
				}

				if (r == ML_OK)
				{
					r = PTaskVec_push (&st, space);
				}

				if (r == ML_OK)
				{
					r = PTaskVec_push (&st, fun);
				}
			}

			else
			{
				int32_t  parens = t.ctx >= 1;
				PTask    close  = task_text (")");
				PTask    pop    = task_pop (0);
				PTask    body   = task_node (0, 0);
				uint64_t cur    = t.node;
				uint64_t count  = 0;

				if (parens)
				{
					r = ml_str_append (out, ml_slice_cstr ("("));
				}

				if (r == ML_OK)
				{
					r = ml_str_append (out, ml_slice_cstr ("\xCE\xBB"));
				}

				while (r == ML_OK && db->data [cur].kind == LK_ABS)
				{
					MlStr name;

					ml_str_init (&name);
					r = binder_name (&name, db, names, &scope, db->data [cur]);

					if (r == ML_OK)
					{
						r = ml_str_append (
							out,
							ml_slice_cstr (ml_choose (count == 0, "", " "))
						);
					}

					if (r == ML_OK)
					{
						r = ml_str_append (out, ml_str_view (name));
					}

					if (r == ML_OK)
					{
						r = LNameVec_push (&scope, name);
					}

					if (r != ML_OK)
					{
						ml_str_free (&name);
					}

					count++;
					body.node = db->data [cur].a;
					cur       = db->data [cur].a;
				}

				if (r == ML_OK)
				{
					r = ml_str_append (out, ml_slice_cstr (". "));
				}

				pop.node = count;

				if (r == ML_OK && parens)
				{
					r = PTaskVec_push (&st, close);
				}

				if (r == ML_OK)
				{
					r = PTaskVec_push (&st, pop);
				}

				if (r == ML_OK)
				{
					r = PTaskVec_push (&st, body);
				}
			}
		}
	}

	for (uint64_t i = 0; i < scope.len; i++)
	{
		ml_str_free (&scope.data [i]);
	}

	LNameVec_free (&scope);
	PTaskVec_free (&st);
	return r;
}

/* The terms of the reduction: the first one and then one for each step. */
static MlResult normalize (
	MlStr          *trace,
	LNodeVec       *db,
	const LNameVec *names,
	uint64_t       *root,
	int32_t        want_trace,
	int32_t        *normal
)
{
	MlResult r     = ML_OK;
	uint64_t steps = 0;
	int32_t  more  = 1;

	*normal = 0;

	if (want_trace)
	{
		r = print_term (trace, db, names, *root);
	}

	while (r == ML_OK && more && steps < ML_LAMBDA_MAX_STEPS)
	{
		int32_t reduced = 0;

		r = reduce_once (db, root, &reduced);

		if (r == ML_OK && reduced)
		{
			steps++;

			if (want_trace && steps <= ML_LAMBDA_MAX_TRACE)
			{
				r = ml_str_append (trace, ml_slice_cstr ("\n\xE2\x86\x92 "));

				if (r == ML_OK)
				{
					r = print_term (trace, db, names, *root);
				}
			}

			else if (want_trace && steps == ML_LAMBDA_MAX_TRACE + 1)
			{
				r = ml_str_append (
					trace,
					ml_slice_cstr ("\n\xE2\x86\x92 \xE2\x80\xA6")
				);
			}
		}

		else
		{
			more    = 0;
			*normal = r == ML_OK;
		}
	}

	return r;
}

static int32_t starts_term (Token t)
{
	return (
		   t.kind == TK_NAME
		|| t.kind == TK_NUMBER
		|| t.kind == TK_LAMBDA
		|| t.kind == TK_LPAREN
	);
}

MlResult ml_lambda_run (
	const MlLogicDefVec *defs,
	uint64_t            count,
	MlSlice             source,
	MlLogicOut          *out
)
{
	MlResult r      = ML_OK;
	Token    first  = lex (source, 0);
	Token    second = lex (source, first.start + first.len);
	uint64_t from   = 0;
	int32_t  define = 0;
	int32_t  trace  = 0;
	Parser   p;
	LNodeVec db;
	uint64_t root   = 0;
	uint64_t term   = 0;

	parser_init (&p);
	LNodeVec_init (&db);

	if (first.kind == TK_END)
	{
		out->value = 0;
	}

	else if (second.kind == TK_ASSIGN && first.kind != TK_NAME)
	{
		r = ml_logic_out_error (
			out,
			ML_LOGIC_LAMBDA_BAD_DEFINITION,
			first.start,
			first.len
		);
	}

	else
	{
		define = first.kind == TK_NAME && second.kind == TK_ASSIGN;
		trace  = (
			   first.kind == TK_NAME
			&& ml_slice_equal (
				ml_slice_sub (source, first.start, first.len),
				ml_slice_cstr ("trace")
			)
			&& starts_term (second)
		);

		if (define)
		{
			from = second.start + second.len;
		}

		else if (trace)
		{
			from = first.start + first.len;
		}

		r = parse (&p, source, from, &root);

		if (r == ML_OK && p.bad)
		{
			r = ml_logic_out_error (out, p.error, p.err_start, p.err_len);
		}

		else if (r == ML_OK)
		{
			r = resolve (&p, &db, root, defs, count, &term);

			if (r == ML_ERR_RANGE)
			{
				r = ml_logic_out_error (
					out,
					ML_LOGIC_LAMBDA_TOO_BIG,
					0,
					source.len
				);
			}
		}

		if (r == ML_OK && out->error == ML_LOGIC_OK && !p.bad)
		{
			int32_t normal = 0;

			/* the definition keeps the term as it is, not reduced */
			if (define)
			{
				r = print_term (&out->def_text, &db, &p.names, term);

				if (r == ML_OK)
				{
					r = ml_str_append (
						&out->def_name,
						ml_slice_sub (source, first.start, first.len)
					);
				}
			}

			if (r == ML_OK)
			{
				r = normalize (
					&out->block,
					&db,
					&p.names,
					&term,
					trace,
					&normal
				);
			}

			if (r == ML_ERR_RANGE)
			{
				r = ml_logic_out_error (
					out,
					ML_LOGIC_LAMBDA_TOO_BIG,
					0,
					source.len
				);
			}

			else if (r == ML_OK && !normal)
			{
				ml_str_clear (&out->block);
				r = ml_logic_out_error (
					out,
					ML_LOGIC_LAMBDA_NO_NORMAL_FORM,
					0,
					source.len
				);
			}

			else if (r == ML_OK)
			{
				r = print_term (&out->text, &db, &p.names, term);

				if (r == ML_OK)
				{
					out->value = 1;
				}
			}
		}
	}

	LNodeVec_free (&db);
	parser_free (&p);
	return r;
}
