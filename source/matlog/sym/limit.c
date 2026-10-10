#include "internal.h"

/*
 * Limits. An expression is evaluated for x going to a point, bottom up, in an
 * extended arithmetic: a value is a finite expression with the side it is
 * approached from (so that 1/x at 0 from above is +inf), an infinity, or
 * unknown (0/0, inf - inf, sin of an infinity ...). When the whole expression
 * is unknown, l'Hopital's rule is applied to the quotient (or to a product of
 * a zero and an infinite factor, written as a quotient) until a value comes
 * out. Nothing recurses: the evaluation has a stack on the heap, and the rule
 * is a loop with a limit.
 */

#define MAX_LHOPITAL 12

typedef enum EvKind
{
	EV_FIN = 0,
	EV_PINF,
	EV_NINF,
	EV_UNK
} EvKind;

/* value + side * (a small positive amount): side is -1, 0 (not known) or 1 */
typedef struct Ev
{
	EvKind      kind;
	const MlSym *value;
	int32_t     side;
} Ev;

ML_VEC_DEFINE (EvVec, Ev)

typedef struct LimFrame
{
	const MlSym *node;
	uint64_t    next;
} LimFrame;

ML_VEC_DEFINE (LimFrameVec, LimFrame)

typedef struct Lim
{
	MlSymCtx    *ctx;
	const MlSym *x;
	Ev          at;
} Lim;

static const MlSym *int_node (Lim *L, int64_t n)
{
	return ml_sym_int (L->ctx, n);
}

static Ev ev_fin (const MlSym *value, int32_t side)
{
	Ev e = {
		.kind  = EV_FIN,
		.value = value,
		.side  = side
	};

	return e;
}

static Ev ev_inf (int32_t sign)
{
	Ev e = {
		.kind  = ml_choose (sign > 0, EV_PINF, EV_NINF),
		.value = NULL,
		.side  = 0
	};

	return e;
}

static Ev ev_unk (void)
{
	Ev e = {
		.kind  = EV_UNK,
		.value = NULL,
		.side  = 0
	};

	return e;
}

static int32_t is_inf (Ev e)
{
	return e.kind == EV_PINF || e.kind == EV_NINF;
}

static int32_t inf_sign (Ev e)
{
	return ml_choose (e.kind == EV_PINF, 1, -1);
}

static int32_t is_zero_ev (Ev e)
{
	return e.kind == EV_FIN && ml_sym_is_zero (e.value);
}

/* -1, 0 or 1: the sign of a finite value, 0 when it cannot be told. */
static int32_t sign_of (Lim *L, const MlSym *v)
{
	int32_t sign = 0;
	double  d;

	if (v->kind == ML_SYM_NUM)
	{
		sign = ml_rat_sign (v->num);
	}

	else if (ml_sym_to_double (L->ctx->arena, v, &d))
	{
		sign = ml_choose (
			d > 1e-12,
			1,
			ml_choose (d < -1e-12, -1, 0)
		);
	}

	return sign;
}

/* The side two summands together are approached from. */
static int32_t join_sides (int32_t a, int32_t b)
{
	return ml_choose (
		a == b,
		a,
		ml_choose (a == 0, b, ml_choose (b == 0, a, 0))
	);
}

static Ev ev_add (Lim *L, Ev a, Ev b)
{
	Ev r = ev_unk ();

	if (a.kind == EV_FIN && b.kind == EV_FIN)
	{
		r = ev_fin (
			ml_sym_add (L->ctx, a.value, b.value),
			join_sides (a.side, b.side)
		);
	}

	else if (a.kind == EV_FIN && is_inf (b))
	{
		r = b;
	}

	else if (is_inf (a) && b.kind == EV_FIN)
	{
		r = a;
	}

	else if (is_inf (a) && is_inf (b) && a.kind == b.kind)
	{
		r = a;
	}

	return r;
}

static Ev ev_mul (Lim *L, Ev a, Ev b)
{
	Ev r = ev_unk ();

	if (a.kind == EV_FIN && b.kind == EV_FIN)
	{
		int32_t za = ml_sym_is_zero (a.value);
		int32_t zb = ml_sym_is_zero (b.value);
		int32_t sa = sign_of (L, a.value);
		int32_t sb = sign_of (L, b.value);
		int32_t side;

		if (za && zb)
		{
			side = a.side * b.side;
		}

		else if (za)
		{
			side = a.side * sb;
		}

		else if (zb)
		{
			side = b.side * sa;
		}

		else
		{
			side = join_sides (sa * b.side, sb * a.side);
		}

		r = ev_fin (ml_sym_mul (L->ctx, a.value, b.value), side);
	}

	else if (is_inf (a) && is_inf (b))
	{
		r = ev_inf (inf_sign (a) * inf_sign (b));
	}

	else if (is_inf (a) || is_inf (b))
	{
		Ev      inf = ml_choose (is_inf (a), a, b);
		Ev      fin = ml_choose (is_inf (a), b, a);
		int32_t sg  = 0;

		if (fin.kind == EV_FIN)
		{
			sg = sign_of (L, fin.value);
		}

		if (sg != 0)
		{
			r = ev_inf (inf_sign (inf) * sg);
		}
	}

	return r;
}

static Ev ev_inv (Lim *L, Ev a)
{
	Ev r = ev_unk ();

	if (a.kind == EV_FIN && ml_sym_is_zero (a.value))
	{
		r = ml_choose (a.side != 0, ev_inf (a.side), ev_unk ());
	}

	else if (a.kind == EV_FIN)
	{
		r = ev_fin (
			ml_sym_pow (L->ctx, a.value, int_node (L, -1)),
			-a.side
		);
	}

	else if (is_inf (a))
	{
		r = ev_fin (int_node (L, 0), inf_sign (a));
	}

	return r;
}

/* 1 when the exponent is an integer; parity says whether it is odd. */
static int32_t exponent_kind (const MlSym *q, int32_t *odd)
{
	int64_t n  = 0;
	int32_t ok = ml_sym_to_i64 (q, &n);

	*odd = ok && (n % 2 != 0);
	return ok;
}

/* a to the number q (a rational): the powers and roots. */
static Ev ev_pow_num (Lim *L, Ev a, const MlSym *q)
{
	Ev      r      = ev_unk ();
	int32_t odd    = 0;
	int32_t whole  = exponent_kind (q, &odd);
	int32_t qsign  = ml_rat_sign (q->num);

	if (ml_sym_is_zero (q))
	{
		r = ev_fin (int_node (L, 1), 0);
	}

	else if (a.kind == EV_FIN && ml_sym_is_zero (a.value))
	{
		if (qsign > 0)
		{
			if (whole)
			{
				r = ev_fin (int_node (L, 0), ml_choose (odd, a.side, 1));
			}

			else
			{
				r = ml_choose (
					a.side > 0,
					ev_fin (int_node (L, 0), 1),
					ev_unk ()
				);
			}
		}

		else if (whole)
		{
			r = ml_choose (
				odd,
				ml_choose (a.side != 0, ev_inf (a.side), ev_unk ()),
				ml_choose (a.side != 0, ev_inf (1), ev_unk ())
			);
		}

		else
		{
			r = ml_choose (a.side > 0, ev_inf (1), ev_unk ());
		}
	}

	else if (a.kind == EV_FIN)
	{
		int32_t cs = sign_of (L, a.value);

		if (cs > 0 || (cs < 0 && whole))
		{
			/* the slope q c^(q-1) has the sign of q, flipped for c < 0, q even
			 */
			int32_t flip = cs < 0 && !odd;
			int32_t side = a.side * qsign * ml_choose (flip, -1, 1);

			r = ev_fin (ml_sym_pow (L->ctx, a.value, q), side);
		}
	}

	else if (a.kind == EV_PINF)
	{
		r = ml_choose (
			qsign > 0,
			ev_inf (1),
			ev_fin (int_node (L, 0), 1)
		);
	}

	else if (a.kind == EV_NINF && whole)
	{
		r = ml_choose (
			qsign > 0,
			ev_inf (ml_choose (odd, -1, 1)),
			ev_fin (int_node (L, 0), ml_choose (odd, -1, 1))
		);
	}

	return r;
}

/* The values of the elementary functions, with the side they come from. */
static Ev ev_func (Lim *L, MlSlice name, Ev a)
{
	Ev r = ev_unk ();

	if (a.kind == EV_UNK)
	{
		r = ev_unk ();
	}

	else if (ML_WORD_IS (name, EXP))
	{
		if (a.kind == EV_FIN)
		{
			r = ev_fin (ml_sym_call (L->ctx, name, a.value), a.side);
		}

		else
		{
			r = ml_choose (
				a.kind == EV_PINF,
				ev_inf (1),
				ev_fin (int_node (L, 0), 1)
			);
		}
	}

	else if (ML_WORD_IS (name, LN))
	{
		int32_t cs = ml_choose (a.kind == EV_FIN, sign_of (L, a.value), 0);

		if (a.kind == EV_PINF)
		{
			r = ev_inf (1);
		}

		else if (a.kind == EV_FIN && ml_sym_is_zero (a.value))
		{
			r = ml_choose (a.side > 0, ev_inf (-1), ev_unk ());
		}

		else if (a.kind == EV_FIN && cs > 0)
		{
			r = ev_fin (ml_sym_call (L->ctx, name, a.value), a.side);
		}
	}

	else if (ML_WORD_IS (name, ATAN) && is_inf (a))
	{
		const MlSym *pi = ml_sym_var (L->ctx, ML_WORD_NAME (PI));

		r = ev_fin (
			ml_sym_mul (
				L->ctx,
				ml_sym_num (
					L->ctx,
					ml_rat_make (
						L->ctx->arena,
						ml_big_from_i64 (L->ctx->arena, inf_sign (a)),
						ml_big_from_u64 (L->ctx->arena, 2)
					)
				),
				pi
			),
			-inf_sign (a)
		);
	}

	else if (ML_WORD_IS (name, ABS) && is_inf (a))
	{
		r = ev_inf (1);
	}

	else if (a.kind == EV_FIN)
	{
		int32_t cs    = sign_of (L, a.value);
		int32_t zero  = ml_sym_is_zero (a.value);
		int32_t range = 1;

		if (ML_WORD_IS (name, ASIN) || ML_WORD_IS (name, ACOS))
		{
			double d;

			range = (
				   ml_sym_to_double (L->ctx->arena, a.value, &d)
				&& d >= -1.0
				&& d <= 1.0
			);
		}

		if (
			   range
			&& (
				   ML_WORD_IS (name, SIN)
				|| ML_WORD_IS (name, COS)
				|| ML_WORD_IS (name, TAN)
				|| ML_WORD_IS (name, ASIN)
				|| ML_WORD_IS (name, ACOS)
				|| ML_WORD_IS (name, ATAN)
				|| ML_WORD_IS (name, ABS)
			)
		)
		{
			const MlSym *v    = ml_sym_call (L->ctx, name, a.value);
			int32_t     side  = 0;
			int32_t     moved = a.side != 0;

			if (ML_WORD_IS (name, ABS))
			{
				side = ml_choose (zero, ml_choose (moved, 1, 0), a.side * cs);
			}

			else if (
				   zero
				&& (
					   ML_WORD_IS (name, SIN)
					|| ML_WORD_IS (name, TAN)
					|| ML_WORD_IS (name, ASIN)
					|| ML_WORD_IS (name, ATAN)
				)
			)
			{
				side = a.side;
			}

			r = ev_fin (v, side);
		}
	}

	return r;
}

/* The value of the leaf: a number, the variable, another symbol. */
static Ev ev_leaf (Lim *L, const MlSym *s)
{
	Ev r = ev_fin (s, 0);

	if (s->kind == ML_SYM_VAR && ml_sym_compare (L->ctx, s, L->x) == 0)
	{
		r = L->at;
	}

	return r;
}

/* The value of an inner node from the values of its kids. */
static Ev ev_node (Lim *L, const MlSym *s, const Ev *kids)
{
	Ev r = ev_unk ();

	if (s->kind == ML_SYM_ADD)
	{
		r = kids [0];

		for (uint64_t i = 1; i < s->count; i++)
		{
			r = ev_add (L, r, kids [i]);
		}
	}

	else if (s->kind == ML_SYM_MUL)
	{
		r = kids [0];

		for (uint64_t i = 1; i < s->count; i++)
		{
			r = ev_mul (L, r, kids [i]);
		}
	}

	else if (s->kind == ML_SYM_POW)
	{
		if (s->kids [1]->kind == ML_SYM_NUM)
		{
			r = ev_pow_num (L, kids [0], s->kids [1]);
		}

		else if (ml_sym_is_var (s->kids [0], ML_WORD_NAME (E)))
		{
			r = ev_func (L, ML_WORD_NAME (EXP), kids [1]);
		}

		else
		{
			r = ev_func (
				L,
				ML_WORD_NAME (EXP),
				ev_mul (
					L,
					kids [1],
					ev_func (L, ML_WORD_NAME (LN), kids [0])
				)
			);
		}
	}

	else if (s->count == 1)
	{
		r = ev_func (L, s->name, kids [0]);
	}

	return r;
}

/* Evaluates the whole tree with an explicit stack. */
static Ev ev_tree (Lim *L, const MlSym *s)
{
	LimFrameVec frames;
	EvVec       vals;
	Ev          result = ev_unk ();
	int32_t     ok     = 1;
	LimFrame    root   = { .node = s, .next = 0 };

	LimFrameVec_init (&frames);
	EvVec_init (&vals);
	ok = LimFrameVec_push (&frames, root) == ML_OK;

	while (ok && frames.len > 0 && L->ctx->status == ML_OK)
	{
		LimFrame *f = &frames.data [frames.len - 1];

		if (f->node->kind == ML_SYM_NUM || f->node->kind == ML_SYM_VAR)
		{
			Ev leaf = ev_leaf (L, f->node);

			ok = EvVec_push (&vals, leaf) == ML_OK;
			frames.len--;
		}

		else if (f->next < f->node->count)
		{
			LimFrame kid = { .node = f->node->kids [f->next], .next = 0 };

			f->next++;
			ok = LimFrameVec_push (&frames, kid) == ML_OK;
		}

		else
		{
			uint64_t n = f->node->count;
			Ev       r = ev_node (L, f->node, &vals.data [vals.len - n]);

			vals.len -= n;
			ok        = EvVec_push (&vals, r) == ML_OK;
			frames.len--;
		}
	}

	if (ok && vals.len == 1 && L->ctx->status == ML_OK)
	{
		result = vals.data [0];
	}

	else if (!ok)
	{
		L->ctx->status = ML_ERR_NOMEM;
	}

	EvVec_free (&vals);
	LimFrameVec_free (&frames);
	return result;
}

/* ---- the steps ---- */

static void log_text (Lim *L, const char *head, const MlSym *a, const MlSym *b)
{
	MlStepLog *log = L->ctx->log;

	if (log != NULL && log->status == ML_OK)
	{
		MlStr text;

		ml_str_init (&text);
		log->status = ml_str_append (&text, ml_slice_cstr (head));

		if (log->status == ML_OK && a != NULL)
		{
			log->status = ml_str_append (&text, ml_slice_cstr (" "));
		}

		if (log->status == ML_OK && a != NULL)
		{
			log->status = ml_sym_format (&text, a);
		}

		if (log->status == ML_OK && b != NULL)
		{
			log->status = ml_str_append (&text, ml_slice_cstr (" / "));
		}

		if (log->status == ML_OK && b != NULL)
		{
			log->status = ml_sym_format (&text, b);
		}

		if (log->status == ML_OK)
		{
			ml_steplog_line (log, ml_str_view (text));
		}

		ml_str_free (&text);
	}
}

/* ---- l'Hopital ---- */

static void push (Lim *L, SymPtrVec *v, const MlSym *s)
{
	if (SymPtrVec_push (v, s) != ML_OK)
	{
		L->ctx->status = ML_ERR_NOMEM;
	}
}

static const MlSym *product_of (Lim *L, const SymPtrVec *v)
{
	return ml_sym_product (L->ctx, v->data, v->len);
}

static int32_t zero_over_zero (Ev n, Ev d)
{
	return (is_zero_ev (n) && is_zero_ev (d)) || (is_inf (n) && is_inf (d));
}

/*
 * Writes the expression as a quotient n / d in which both parts go to zero or
 * both to infinity. 1 when that works.
 */
static int32_t split_quotient (
	Lim         *L,
	const MlSym *cur,
	const MlSym **n,
	const MlSym **d
)
{
	uint64_t  count = ml_choose (cur->kind == ML_SYM_MUL, cur->count, 1);
	SymPtrVec top;
	SymPtrVec bottom;
	SymPtrVec zeros;
	SymPtrVec infs;
	SymPtrVec rest;
	int32_t   found = 0;

	SymPtrVec_init (&top);
	SymPtrVec_init (&bottom);
	SymPtrVec_init (&zeros);
	SymPtrVec_init (&infs);
	SymPtrVec_init (&rest);

	for (uint64_t i = 0; i < count && L->ctx->status == ML_OK; i++)
	{
		const MlSym *f
			= ml_choose (cur->kind == ML_SYM_MUL, cur->kids [i], cur);
		Ev          e  = ev_tree (L, f);

		if (
			   f->kind == ML_SYM_POW
			&& f->kids [1]->kind == ML_SYM_NUM
			&& ml_rat_sign (f->kids [1]->num) < 0
		)
		{
			push (
				L,
				&bottom,
				ml_sym_pow (
					L->ctx,
					f->kids [0],
					ml_sym_neg (L->ctx, f->kids [1])
				)
			);
		}

		else
		{
			push (L, &top, f);
		}

		if (is_zero_ev (e))
		{
			push (L, &zeros, f);
		}

		else if (is_inf (e))
		{
			push (L, &infs, f);
		}

		else
		{
			push (L, &rest, f);
		}
	}

	if (bottom.len > 0 && L->ctx->status == ML_OK)
	{
		*n    = product_of (L, &top);
		*d    = product_of (L, &bottom);
		found = zero_over_zero (ev_tree (L, *n), ev_tree (L, *d));
	}

	if (!found && zeros.len > 0 && infs.len > 0 && L->ctx->status == ML_OK)
	{
		SymPtrVec top2;
		const MlSym *z;

		SymPtrVec_init (&top2);

		for (uint64_t i = 0; i < infs.len; i++)
		{
			push (L, &top2, infs.data [i]);
		}

		for (uint64_t i = 0; i < rest.len; i++)
		{
			push (L, &top2, rest.data [i]);
		}

		z  = product_of (L, &zeros);
		*n = product_of (L, &top2);
		*d = ml_sym_pow (L->ctx, z, int_node (L, -1));
		SymPtrVec_free (&top2);
		found = 1;
	}

	SymPtrVec_free (&rest);
	SymPtrVec_free (&infs);
	SymPtrVec_free (&zeros);
	SymPtrVec_free (&bottom);
	SymPtrVec_free (&top);
	return found;
}

static Ev ev_quotient (Lim *L, Ev n, Ev d)
{
	return ev_mul (L, n, ev_inv (L, d));
}

/*
 * The value of the expression for the point and side set in L. An unknown
 * value is a quotient of two parts that go to zero (or to infinity) together:
 * the rule replaces it by the quotient of the derivatives, which is made
 * canonical (so a quotient that simplifies is seen as it is) and tried again.
 */
static Ev one_sided (Lim *L, const MlSym *f)
{
	const MlSym *cur = ml_sym_simplify (L->ctx, f);
	Ev          r    = ev_tree (L, cur);
	int32_t     go   = r.kind == EV_UNK;

	if (!go)
	{
		log_text (L, "substitute the point:", cur, NULL);
	}

	for (uint64_t i = 0; i < MAX_LHOPITAL && go && L->ctx->status == ML_OK; i++)
	{
		const MlSym *n = NULL;
		const MlSym *d = NULL;
		Ev          en;
		Ev          ed;

		go = split_quotient (L, cur, &n, &d);

		if (go)
		{
			en = ev_tree (L, n);
			ed = ev_tree (L, d);
			r  = ev_quotient (L, en, ed);
			go = r.kind == EV_UNK && zero_over_zero (en, ed);
		}

		if (go)
		{
			const MlSym *dn = ml_sym_diff (L->ctx, n, L->x);
			const MlSym *dd = ml_sym_diff (L->ctx, d, L->x);

			log_text (L, "0/0 or inf/inf, differentiate both:", n, d);
			go = !ml_sym_is_zero (dd) && L->ctx->status == ML_OK;

			if (go)
			{
				cur = ml_sym_div (L->ctx, dn, dd);
				r   = ev_tree (L, cur);
				go  = r.kind == EV_UNK;
			}
		}
	}

	return r;
}

/* The result of a one sided limit as the answer of the module. */
static MlSymLimit as_limit (Ev e)
{
	MlSymLimit r = {
		.kind  = ML_LIMIT_NONE,
		.value = NULL
	};

	if (e.kind == EV_FIN)
	{
		r.kind  = ML_LIMIT_VALUE;
		r.value = e.value;
	}

	else if (e.kind == EV_PINF)
	{
		r.kind = ML_LIMIT_PINF;
	}

	else if (e.kind == EV_NINF)
	{
		r.kind = ML_LIMIT_NINF;
	}

	return r;
}

static int32_t same_limit (MlSymCtx *ctx, MlSymLimit a, MlSymLimit b)
{
	return (
		   a.kind == b.kind
		&& (
			   a.kind != ML_LIMIT_VALUE
			|| ml_sym_compare (ctx, a.value, b.value) == 0
		)
	);
}

MlSymLimit ml_sym_limit (
	MlSymCtx    *ctx,
	const MlSym *f,
	const MlSym *x,
	const MlSym *at,
	int32_t     side
)
{
	Lim        L;
	MlSymLimit r = {
		.kind  = ML_LIMIT_NONE,
		.value = NULL
	};

	int32_t    plus  = ml_sym_is_var (at, ML_WORD_NAME (INF));
	int32_t    minus = (
		   at->kind == ML_SYM_MUL
		&& at->count == 2
		&& ml_sym_is_one (ml_sym_neg (ctx, at->kids [0]))
		&& ml_sym_is_var (at->kids [1], ML_WORD_NAME (INF))
	);

	L.ctx = ctx;
	L.x   = x;

	if (plus || minus)
	{
		L.at = ev_inf (ml_choose (plus, 1, -1));
		r    = as_limit (one_sided (&L, f));
	}

	else if (!ml_sym_has_var (at, x->name))
	{
		MlSymLimit up;
		MlSymLimit down;

		L.at = ev_fin (at, 1);
		up   = as_limit (one_sided (&L, f));
		L.at = ev_fin (at, -1);
		down = ml_choose (side <= 0, as_limit (one_sided (&L, f)), up);

		if (side > 0)
		{
			r = up;
		}

		else if (side < 0)
		{
			r = down;
		}

		else if (up.kind == ML_LIMIT_NONE || down.kind == ML_LIMIT_NONE)
		{
			r.kind = ML_LIMIT_NONE;
		}

		else if (same_limit (ctx, up, down))
		{
			r = up;
		}

		else
		{
			r.kind = ML_LIMIT_DIFFER;
		}
	}

	return r;
}
