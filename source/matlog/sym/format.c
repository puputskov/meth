#include "internal.h"

#include <string.h>

/*
 * The printer. A tree is written by a loop over tasks on a stack: a task is
 * a piece of text, a part of a number, or a node to print with the
 * precedence its place needs. The parts of a node are pushed in reverse, so
 * they are popped in reading order.
 *
 * Precedences: 1 sum, 2 product, 3 power, 4 atom. A node whose own
 * precedence is lower than the one asked for gets parentheses.
 */

#define F_ABS   1 /* print without the sign of the leading number */
#define F_RECIP 2 /* a power printed with the sign of its exponent flipped */

/* The parts of a number a task prints (Task.flags of a TASK_NUM). */
#define P_ABS   0 /* the whole number without its sign */
#define P_NUMER 1 /* numerator of the fraction, without sign */
#define P_DENOM 2 /* denominator */
#define P_SUPER 3 /* the whole number without sign, in superscript digits */

typedef enum TaskKind
{
	TASK_TEXT = 0,
	TASK_NUM,
	TASK_NODE
} TaskKind;

typedef struct Task
{
	TaskKind    kind;
	const char  *text;
	const MlSym *node;
	int32_t     prec;
	int32_t     flags;
} Task;

ML_VEC_DEFINE (TaskVec, Task)

static MlResult push_task (
	TaskVec     *v,
	TaskKind    kind,
	const char  *text,
	const MlSym *node,
	int32_t     prec,
	int32_t     flags
)
{
	Task t = {
		.kind  = kind,
		.text  = text,
		.node  = node,
		.prec  = prec,
		.flags = flags
	};

	return TaskVec_push (v, t);
}

static MlResult push_text (TaskVec *v, const char *text)
{
	return push_task (v, TASK_TEXT, text, NULL, 0, 0);
}

static MlResult push_node (
	TaskVec     *v,
	const MlSym *node,
	int32_t     prec,
	int32_t     flags
)
{
	return push_task (v, TASK_NODE, "", node, prec, flags);
}

static MlResult push_num (TaskVec *v, const MlSym *node, int32_t part)
{
	return push_task (v, TASK_NUM, "", node, 0, part);
}

/* The number as text without its sign. */
static MlResult magnitude_text (MlStr *out, MlRat x)
{
	MlStr    text;
	MlResult r;

	ml_str_init (&text);
	r = ml_rat_format (&text, x);

	if (r == ML_OK)
	{
		MlSlice v = ml_str_view (text);

		if (v.len > 0 && v.data [0] == '-')
		{
			v = ml_slice_sub (v, 1, v.len - 1);
		}

		r = ml_str_append (out, v);
	}

	ml_str_free (&text);
	return r;
}

/* Where the fraction bar is in a number's text (its length if none). */
static uint64_t bar_of (MlSlice v)
{
	uint64_t i = 0;

	while (i < v.len && v.data [i] != '/')
	{
		i++;
	}

	return i;
}

static const char *const superscripts [10] = {
	"\xE2\x81\xB0", "\xC2\xB9", "\xC2\xB2", "\xC2\xB3", "\xE2\x81\xB4",
	"\xE2\x81\xB5", "\xE2\x81\xB6", "\xE2\x81\xB7", "\xE2\x81\xB8",
	"\xE2\x81\xB9"
};

/* Prints one part of a number. */
static MlResult print_num_part (MlStr *out, const MlSym *n, int32_t part)
{
	MlStr    text;
	MlResult r;

	ml_str_init (&text);
	r = magnitude_text (&text, n->num);

	if (r == ML_OK)
	{
		MlSlice  v   = ml_str_view (text);
		uint64_t bar = bar_of (v);

		if (part == P_NUMER)
		{
			r = ml_str_append (out, ml_slice_sub (v, 0, bar));
		}

		else if (part == P_DENOM)
		{
			r = ml_str_append (out, ml_slice_sub (v, bar + 1, v.len - bar - 1));
		}

		else if (part == P_SUPER)
		{
			for (uint64_t i = 0; i < v.len && r == ML_OK; i++)
			{
				r = ml_str_append (
					out,
					ml_slice_cstr (superscripts [(uint8_t)v.data [i] - '0'])
				);
			}
		}

		else
		{
			r = ml_str_append (out, v);
		}
	}

	ml_str_free (&text);
	return r;
}

static int32_t is_negative_number (const MlSym *s)
{
	return s->kind == ML_SYM_NUM && ml_rat_sign (s->num) < 0;
}

/* Has the term a minus sign in front: -3, -x, -2x? */
static int32_t is_negative_term (const MlSym *s)
{
	return (
		   is_negative_number (s)
		|| (
			   s->kind == ML_SYM_MUL
			&& s->count > 0
			&& is_negative_number (s->kids [0])
		)
	);
}

/* Pushes the tasks of seq (in reading order) so they are popped in order. */
static MlResult flush_seq (TaskVec *st, TaskVec *seq)
{
	MlResult r = ML_OK;

	for (uint64_t i = seq->len; i > 0 && r == ML_OK; i--)
	{
		r = TaskVec_push (st, seq->data [i - 1]);
	}

	TaskVec_free (seq);
	return r;
}

/* The sequence in seq, in parentheses when prec asks for more than own. */
static MlResult wrap_seq (TaskVec *seq, int32_t prec, int32_t own)
{
	MlResult r = ML_OK;

	if (prec > own)
	{
		TaskVec wrapped;

		TaskVec_init (&wrapped);
		r = push_text (&wrapped, "(");

		for (uint64_t i = 0; i < seq->len && r == ML_OK; i++)
		{
			r = TaskVec_push (&wrapped, seq->data [i]);
		}

		r = ml_choose (r == ML_OK, push_text (&wrapped, ")"), r);
		TaskVec_free (seq);
		*seq = wrapped;
	}

	return r;
}

/* A power; F_RECIP prints a negative exponent as its magnitude. */
static MlResult print_pow (
	TaskVec     *st,
	const MlSym *n,
	int32_t     prec,
	int32_t     flags
)
{
	MlResult    r     = ML_OK;
	const MlSym *base = n->kids [0];
	const MlSym *exp  = n->kids [1];
	TaskVec     seq;
	MlStr       mag;
	MlSlice     m;
	int32_t     own   = 3;
	int32_t     neg   = (
		   exp->kind == ML_SYM_NUM
		&& ml_rat_sign (exp->num) < 0
		&& !(flags & F_RECIP)
	);

	TaskVec_init (&seq);
	ml_str_init (&mag);

	if (exp->kind == ML_SYM_NUM)
	{
		r = magnitude_text (&mag, exp->num);
	}

	m = ml_str_view (mag);

	if (r != ML_OK)
	{
		own = 3;
	}

	else if (exp->kind != ML_SYM_NUM)
	{
		/* x^y: an exponent that is not an atom gets parentheses */
		r = push_node (&seq, base, 4, 0);
		r = ml_choose (r == ML_OK, push_text (&seq, "^"), r);
		r = ml_choose (r == ML_OK, push_node (&seq, exp, 4, 0), r);
	}

	else if (neg)
	{
		/* 1/x^2: a product */
		own = 2;
		r   = push_text (&seq, "1/");
		r   = ml_choose (r == ML_OK, push_node (&seq, n, 3, F_RECIP), r);
	}

	else if (ml_slice_equal (m, ml_slice_cstr ("1")))
	{
		own = 4;
		r   = push_node (&seq, base, prec, 0);
	}

	else if (ml_slice_equal (m, ml_slice_cstr ("1/2")))
	{
		own = 3;
		r   = push_text (&seq, "\xE2\x88\x9A");
		r   = ml_choose (r == ML_OK, push_node (&seq, base, 4, 0), r);
	}

	else if (ml_rat_is_integer (exp->num) && m.len <= 3)
	{
		r = push_node (&seq, base, 4, 0);
		r = ml_choose (r == ML_OK, push_num (&seq, exp, P_SUPER), r);
	}

	else
	{
		r = push_node (&seq, base, 4, 0);
		r = ml_choose (r == ML_OK, push_text (&seq, "^("), r);
		r = ml_choose (r == ML_OK, push_num (&seq, exp, P_ABS), r);
		r = ml_choose (r == ML_OK, push_text (&seq, ")"), r);
	}

	if (r == ML_OK && own < 4)
	{
		r = wrap_seq (&seq, prec, own);
	}

	ml_str_free (&mag);

	if (r == ML_OK)
	{
		r = flush_seq (st, &seq);
	}

	else
	{
		TaskVec_free (&seq);
	}

	return r;
}

/* A sum: the terms with " + " and " - " between them. */
static MlResult print_add (TaskVec *st, const MlSym *n, int32_t prec)
{
	MlResult r = ML_OK;
	TaskVec  seq;

	TaskVec_init (&seq);

	for (uint64_t i = 0; i < n->count && r == ML_OK; i++)
	{
		int32_t neg = is_negative_term (n->kids [i]);

		if (i == 0)
		{
			r = ml_choose (neg, push_text (&seq, "-"), ML_OK);
		}

		else
		{
			r = push_text (&seq, ml_choose (neg, " - ", " + "));
		}

		r = ml_choose (
			r == ML_OK,
			push_node (&seq, n->kids [i], 2, ml_choose (neg, F_ABS, 0)),
			r
		);
	}

	if (r == ML_OK)
	{
		r = wrap_seq (&seq, prec, 1);
	}

	if (r == ML_OK)
	{
		r = flush_seq (st, &seq);
	}

	else
	{
		TaskVec_free (&seq);
	}

	return r;
}

/* Is the factor a power with a negative number as exponent? */
static int32_t is_denominator (const MlSym *f)
{
	return (
		   f->kind == ML_SYM_POW
		&& f->kids [1]->kind == ML_SYM_NUM
		&& ml_rat_sign (f->kids [1]->num) < 0
	);
}

/*
 * A product: the number, then the factors, joined by a dot unless a number
 * stands before a name; factors with a negative power go below a slash.
 */
static MlResult print_mul (
	TaskVec     *st,
	const MlSym *n,
	int32_t     prec,
	int32_t     flags
)
{
	MlResult r      = ML_OK;
	TaskVec  seq;
	uint64_t first  = 0; /* the number, if the product starts with one */
	uint64_t in_num = 0;
	uint64_t in_den = 0;
	int32_t  minus  = 0;
	int32_t  has_q  = 0; /* the number has a denominator */
	int32_t  has_p  = 0; /* the number is printed in front */

	TaskVec_init (&seq);

	if (n->count > 0 && n->kids [0]->kind == ML_SYM_NUM)
	{
		MlStr text;

		first = 1;
		minus = ml_rat_sign (n->kids [0]->num) < 0;
		ml_str_init (&text);
		r     = magnitude_text (&text, n->kids [0]->num);
		has_q = bar_of (ml_str_view (text)) < text.len;
		has_p = !(
			   text.len >= 1
			&& text.data [0] == '1'
			&& bar_of (ml_str_view (text)) == 1
		);
		ml_str_free (&text);
	}

	for (uint64_t i = first; i < n->count; i++)
	{
		in_num += ml_choose (is_denominator (n->kids [i]), 0, 1);
		in_den += ml_choose (is_denominator (n->kids [i]), 1, 0);
	}

	if (r == ML_OK && minus && !(flags & F_ABS))
	{
		r = push_text (&seq, "-");
	}

	/* the numerator: the number if it is not 1, or 1 when nothing else is */
	if (r == ML_OK && first && (has_p || in_num == 0))
	{
		r = push_num (&seq, n->kids [0], P_NUMER);
	}

	else if (r == ML_OK && !first && in_num == 0)
	{
		r = push_text (&seq, "1");
	}

	for (uint64_t i = first; i < n->count && r == ML_OK; i++)
	{
		if (!is_denominator (n->kids [i]))
		{
			/* a number before a name needs no dot */
			int32_t after_number = (
				   seq.len > 0
				&& seq.data [seq.len - 1].kind == TASK_NUM
			);

			if (seq.len > 0 && !after_number)
			{
				r = ml_choose (
					seq.data [seq.len - 1].kind == TASK_TEXT
						&& seq.data [seq.len - 1].text [0] == '-',
					ML_OK,
					push_text (&seq, "\xC2\xB7")
				);
			}

			r = ml_choose (r == ML_OK, push_node (&seq, n->kids [i], 3, 0), r);
		}
	}

	/* the denominator */
	if (r == ML_OK && (in_den > 0 || has_q))
	{
		uint64_t items = in_den + ml_choose (has_q, 1, 0);
		uint64_t k     = 0;

		r = push_text (&seq, "/");
		r = ml_choose (
			r == ML_OK && items > 1,
			push_text (&seq, "("),
			r
		);

		if (r == ML_OK && has_q)
		{
			r = push_num (&seq, n->kids [0], P_DENOM);
			k = 1;
		}

		for (uint64_t i = first; i < n->count && r == ML_OK; i++)
		{
			if (is_denominator (n->kids [i]))
			{
				/* no dot after the number of the denominator: 2\xE2\x88\x9Ax */
				r = ml_choose (
					k > 0 && seq.data [seq.len - 1].kind != TASK_NUM,
					push_text (&seq, "\xC2\xB7"),
					ML_OK
				);
				r = ml_choose (
					r == ML_OK,
					push_node (&seq, n->kids [i], 3, F_RECIP),
					r
				);
				k++;
			}
		}

		r = ml_choose (
			r == ML_OK && items > 1,
			push_text (&seq, ")"),
			r
		);
	}

	if (r == ML_OK)
	{
		r = wrap_seq (&seq, prec, 2);
	}

	if (r == ML_OK)
	{
		r = flush_seq (st, &seq);
	}

	else
	{
		TaskVec_free (&seq);
	}

	return r;
}

/* One node: atoms are written at once, the others push their parts. */
static MlResult print_node (
	MlStr       *out,
	TaskVec     *st,
	const MlSym *n,
	int32_t     prec,
	int32_t     flags
)
{
	MlResult r = ML_OK;

	if (n->kind == ML_SYM_NUM)
	{
		int32_t neg   = ml_rat_sign (n->num) < 0 && !(flags & F_ABS);
		int32_t paren = prec >= 3 && (neg || !ml_rat_is_integer (n->num));

		r = ml_choose (paren, ml_str_append (out, ml_slice_cstr ("(")), r);

		if (r == ML_OK && neg)
		{
			r = ml_str_append (out, ml_slice_cstr ("-"));
		}

		if (r == ML_OK)
		{
			r = print_num_part (out, n, P_ABS);
		}

		if (r == ML_OK && paren)
		{
			r = ml_str_append (out, ml_slice_cstr (")"));
		}
	}

	else if (n->kind == ML_SYM_VAR)
	{
		r = ml_str_append (
			out,
			ml_choose (
				ml_slice_equal (n->name, ml_slice_cstr ("pi")),
				ml_slice_cstr ("\xCF\x80"),
				n->name
			)
		);
	}

	else if (ml_sym_is_set (n))
	{
		r = ml_str_append (out, ml_slice_cstr ("{"));

		if (r == ML_OK)
		{
			r = push_text (st, "}");
		}

		/* the stack pops the last pushed first: the items go in reverse */
		for (uint64_t i = n->count; i > 0 && r == ML_OK; i--)
		{
			r = push_node (st, n->kids [i - 1], 0, 0);

			if (r == ML_OK && i > 1)
			{
				r = push_text (st, ", ");
			}
		}
	}

	else if (n->kind == ML_SYM_FUNC)
	{
		r = ml_str_append (out, n->name);
		r = ml_choose (
			r == ML_OK,
			ml_str_append (out, ml_slice_cstr ("(")),
			r
		);

		if (r == ML_OK)
		{
			/* popped in this order: arg then ")" */
			r = push_text (st, ")");
			r = ml_choose (r == ML_OK, push_node (st, n->kids [0], 0, 0), r);
		}
	}

	else if (n->kind == ML_SYM_ADD)
	{
		r = print_add (st, n, prec);
	}

	else if (n->kind == ML_SYM_MUL)
	{
		r = print_mul (st, n, prec, flags);
	}

	else
	{
		r = print_pow (st, n, prec, flags);
	}

	return r;
}

MlResult ml_sym_format (MlStr *out, const MlSym *s)
{
	MlResult r = ML_OK;
	TaskVec  st;

	TaskVec_init (&st);
	r = push_node (&st, s, 0, 0);

	while (r == ML_OK && st.len > 0)
	{
		Task t = st.data [st.len - 1];

		st.len--;

		if (t.kind == TASK_TEXT)
		{
			r = ml_str_append (out, ml_slice_cstr (t.text));
		}

		else if (t.kind == TASK_NUM)
		{
			r = print_num_part (out, t.node, t.flags);
		}

		else
		{
			r = print_node (out, &st, t.node, t.prec, t.flags);
		}
	}

	TaskVec_free (&st);
	return r;
}
