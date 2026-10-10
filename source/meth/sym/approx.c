#include "internal.h"

#include <math.h>

typedef struct ApproxFrame
{
	const MlSym *node;
	uint64_t    next;
} ApproxFrame;

ML_VEC_DEFINE (ApproxFrameVec, ApproxFrame)
ML_VEC_DEFINE (DoubleVec, double)

/* The value of a function at x; ok is cleared for a name that is unknown. */
static double function_value (MlSlice name, double x, int32_t *ok)
{
	double v = 0.0;

	if (ML_WORD_IS (name, SIN))
	{
		v = sin (x);
	}

	else if (ML_WORD_IS (name, COS))
	{
		v = cos (x);
	}

	else if (ML_WORD_IS (name, TAN))
	{
		v = tan (x);
	}

	else if (ML_WORD_IS (name, ASIN))
	{
		v = asin (x);
	}

	else if (ML_WORD_IS (name, ACOS))
	{
		v = acos (x);
	}

	else if (ML_WORD_IS (name, ATAN))
	{
		v = atan (x);
	}

	else if (ML_WORD_IS (name, LN))
	{
		v = log (x);
	}

	else if (ML_WORD_IS (name, EXP))
	{
		v = exp (x);
	}

	else if (ML_WORD_IS (name, ABS))
	{
		v = fabs (x);
	}

	else
	{
		*ok = 0;
	}

	return v;
}

/* The value of a leaf: a number, pi or e. */
static double leaf_value (MlArena *arena, const MlSym *s, int32_t *ok)
{
	double v = 0.0;

	if (s->kind == ML_SYM_NUM)
	{
		v = ml_rat_to_double (arena, s->num);
	}

	else if (ML_WORD_IS (s->name, PI))
	{
		v = 3.14159265358979323846;
	}

	else if (ML_WORD_IS (s->name, E))
	{
		v = 2.71828182845904523536;
	}

	else
	{
		*ok = 0;
	}

	return v;
}

/* The value of an inner node from the values of its kids on the stack. */
static double node_value (const MlSym *s, const double *kids, int32_t *ok)
{
	double v = 0.0;

	if (s->kind == ML_SYM_ADD)
	{
		for (uint64_t i = 0; i < s->count; i++)
		{
			v += kids [i];
		}
	}

	else if (s->kind == ML_SYM_MUL)
	{
		v = 1.0;

		for (uint64_t i = 0; i < s->count; i++)
		{
			v *= kids [i];
		}
	}

	else if (s->kind == ML_SYM_POW)
	{
		v = pow (kids [0], kids [1]);
	}

	else if (s->count == 1)
	{
		v = function_value (s->name, kids [0], ok);
	}

	else
	{
		*ok = 0;
	}

	return v;
}

int32_t ml_sym_to_double (MlArena *arena, const MlSym *s, double *out)
{
	ApproxFrameVec frames;
	DoubleVec      vals;
	int32_t        ok   = 1;
	double         v    = 0.0;
	ApproxFrame    root = { .node = s, .next = 0 };

	ApproxFrameVec_init (&frames);
	DoubleVec_init (&vals);
	ok = ApproxFrameVec_push (&frames, root) == ML_OK;

	while (ok && frames.len > 0)
	{
		ApproxFrame *f = &frames.data [frames.len - 1];

		if (f->node->kind == ML_SYM_NUM || f->node->kind == ML_SYM_VAR)
		{
			double lv = leaf_value (arena, f->node, &ok);

			ok = ok && DoubleVec_push (&vals, lv) == ML_OK;
			frames.len--;
		}

		else if (f->next < f->node->count)
		{
			ApproxFrame kid = { .node = f->node->kids [f->next], .next = 0 };

			f->next++;
			ok = ApproxFrameVec_push (&frames, kid) == ML_OK;
		}

		else
		{
			uint64_t n = f->node->count;
			double   r = node_value (f->node, &vals.data [vals.len - n], &ok);

			vals.len -= n;
			ok        = ok && DoubleVec_push (&vals, r) == ML_OK;
			frames.len--;
		}
	}

	if (ok && vals.len == 1)
	{
		v  = vals.data [0];
		ok = isfinite (v) != 0;
	}

	else
	{
		ok = 0;
	}

	*out = ml_choose (ok, v, 0.0);
	DoubleVec_free (&vals);
	ApproxFrameVec_free (&frames);
	return ok;
}
