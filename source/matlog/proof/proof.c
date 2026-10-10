#include <matlog/proof/proof.h>

#include <matlog/core/mem.h>
#include <matlog/math/env.h>
#include <matlog/parse/parser.h>
#include <matlog/parse/words.h>
#include <matlog/sym/expr.h>
#include <matlog/texts/texts.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

void ml_proof_report_init (MlProofReport *rep)
{
	MlProofLineVec_init (&rep->lines);
	rep->theorems = 0;
	rep->proved   = 0;
}

void ml_proof_report_free (MlProofReport *rep)
{
	for (uint64_t i = 0; i < rep->lines.len; i++)
	{
		ml_str_free (&rep->lines.data [i].note);
	}

	MlProofLineVec_free (&rep->lines);
	rep->theorems = 0;
	rep->proved   = 0;
}

/* ---- the state of a check ---------------------------------------------- */

typedef struct Eq
{
	const MlSym *l;
	const MlSym *r;
} Eq;

typedef struct Step
{
	Eq      eq;
	int32_t valid;
} Step;

typedef struct Lemma
{
	MlSlice name;
	Eq      eq;
	int32_t has_hyps; /* it was proved from hypotheses */
} Lemma;

ML_VEC_DEFINE (EqVec, Eq)
ML_VEC_DEFINE (StepVec, Step)
ML_VEC_DEFINE (LemmaVec, Lemma)
ML_VEC_DEFINE (SymVec, const MlSym *)
ML_VEC_DEFINE (NameVec, MlSlice)

typedef struct Checker
{
	MlSymCtx     ctx;
	MlEnv        env;
	MlResult     status; /* the first failure of the machine, ML_OK if none */
	EqVec        hyps;   /* the "given" equations of the open theorem */
	StepVec      steps;  /* its steps so far */
	LemmaVec     lemmas; /* the theorems proved above */
	int32_t      open;   /* a theorem is open */
	MlSlice      name;
	Eq           goal;
	int32_t      has_last; /* the step above gave a right hand side */
	const MlSym *last_r;
	uint64_t     errors; /* steps that are invalid or unreadable */
	uint64_t     sorries;
	uint64_t     unchecked;
} Checker;

/* ---- text -------------------------------------------------------------- */

static int32_t is_space (char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static MlSlice trim (MlSlice s)
{
	uint64_t a = 0;
	uint64_t b = s.len;

	while (a < b && is_space (s.data [a]))
	{
		a++;
	}

	while (b > a && is_space (s.data [b - 1]))
	{
		b--;
	}

	return ml_slice_sub (s, a, b - a);
}

/* 1 when s starts with the word w (followed by a space or the end). */
static int32_t starts_word (MlSlice s, const char *w)
{
	uint64_t n  = strlen (w);
	int32_t  ok = 0;

	if (s.len >= n && memcmp (s.data, w, n) == 0)
	{
		ok = s.len == n || is_space (s.data [n]);
	}

	return ok;
}

/* The text after the first word. */
static MlSlice after_word (MlSlice s)
{
	uint64_t i = 0;

	while (i < s.len && !is_space (s.data [i]))
	{
		i++;
	}

	return trim (ml_slice_sub (s, i, s.len - i));
}

static MlSlice first_word (MlSlice s)
{
	uint64_t i = 0;

	while (i < s.len && !is_space (s.data [i]))
	{
		i++;
	}

	return ml_slice_sub (s, 0, i);
}

static int32_t opens (char c)
{
	return c == '(' || c == '[' || c == '{';
}

static int32_t closes (char c)
{
	return c == ')' || c == ']' || c == '}';
}

/* The place of the word w outside brackets, s.len when it is not there. */
static uint64_t find_word (MlSlice s, const char *w)
{
	uint64_t n     = strlen (w);
	uint64_t found = s.len;
	int64_t  depth = 0;

	for (uint64_t i = 0; i < s.len && found == s.len; i++)
	{
		depth += ml_choose (opens (s.data [i]), 1, 0);
		depth -= ml_choose (closes (s.data [i]), 1, 0);

		if (depth == 0 && i + n <= s.len && memcmp (s.data + i, w, n) == 0
			&& (i == 0 || is_space (s.data [i - 1]))
			&& (i + n == s.len || is_space (s.data [i + n])))
		{
			found = i;
		}
	}

	return found;
}

/* The place of a lone = outside brackets, s.len when there is none. */
static uint64_t find_equals (MlSlice s)
{
	uint64_t found = s.len;
	int64_t  depth = 0;

	for (uint64_t i = 0; i < s.len && found == s.len; i++)
	{
		char c    = s.data [i];
		char prev = ml_choose (i > 0, s.data [i - 1], ' ');
		char next = ml_choose (i + 1 < s.len, s.data [i + 1], ' ');

		depth += ml_choose (opens (c), 1, 0);
		depth -= ml_choose (closes (c), 1, 0);

		if (depth == 0 && c == '=' && next != '=' && prev != '=' && prev != '<'
			&& prev != '>' && prev != '!')
		{
			found = i;
		}
	}

	return found;
}

/* ---- the notes of a line ----------------------------------------------- */

static void mark (
	Checker         *c,
	MlProofLine     *out,
	MlProofMark      m,
	const char      *code,
	const MlTextArg *args,
	uint64_t         count
)
{
	MlResult r = ML_OK;

	out->mark = m;
	ml_str_clear (&out->note);

	if (code != NULL)
	{
		r = ml_text_fill (&out->note, ml_text (code), args, count);
	}

	c->status = ml_choose (c->status == ML_OK, r, c->status);
}

static void mark_plain (
	Checker     *c,
	MlProofLine *out,
	MlProofMark  m,
	const char  *code
)
{
	mark (c, out, m, code, NULL, 0);
}

/* ---- formulas ---------------------------------------------------------- */

static void ctx_reset (Checker *c)
{
	c->ctx.status = ML_OK;
	c->ctx.budget = ML_SYM_BUDGET;
}

/* The formula of the text, NULL when it cannot be read. */
static const MlSym *read_side (Checker *c, MlSlice text)
{
	MlParseResult p = ml_parse (text);
	const MlSym  *s = NULL;

	if (p.error.code == ML_PARSE_OK)
	{
		ctx_reset (c);
		s = ml_sym_from_ast (
			&c->ctx,
			&c->env,
			0,
			text,
			&p.ast,
			p.ast.root,
			ml_slice ("", 0)
		);
		s = ml_choose (ml_symctx_ok (&c->ctx), s, NULL);
		ml_ast_free (&p.ast);
	}

	return s;
}

typedef enum EqCode
{
	EQ_OK = 0,
	EQ_NO_EQUALS,
	EQ_BAD_SIDE
} EqCode;

typedef struct EqRead
{
	EqCode  code;
	Eq      eq;
	MlSlice bad; /* EQ_BAD_SIDE: the text that is no formula */
} EqRead;

/* The equation left = right of the text. */
static EqRead read_eq (Checker *c, MlSlice text)
{
	EqRead e = {
		.code = EQ_OK,
		.eq   = { .l = NULL, .r = NULL }
	};

	uint64_t at = find_equals (text);
	MlSlice  lt = trim (ml_slice_sub (text, 0, at));
	MlSlice  rt = ml_slice ("", 0);

	e.bad = text;

	if (at >= text.len)
	{
		e.code = EQ_NO_EQUALS;
	}

	else
	{
		rt     = trim (ml_slice_sub (text, at + 1, text.len - at - 1));
		e.eq.l = read_side (c, lt);
		e.eq.r = read_side (c, rt);

		if (e.eq.l == NULL)
		{
			e.code = EQ_BAD_SIDE;
			e.bad  = lt;
		}

		else if (e.eq.r == NULL)
		{
			e.code = EQ_BAD_SIDE;
			e.bad  = rt;
		}
	}

	return e;
}

/* The canonical form, by simplify (how 0) or by expand (how 1). */
static const MlSym *normal (Checker *c, const MlSym *s, int32_t how)
{
	ctx_reset (c);

	return ml_choose (
		how == 1,
		ml_sym_expand (&c->ctx, s),
		ml_sym_simplify (&c->ctx, s)
	);
}

/* 1 when the two formulas have the same expanded form. */
static int32_t same_formula (Checker *c, const MlSym *a, const MlSym *b)
{
	const MlSym *na = normal (c, a, 1);
	const MlSym *nb = normal (c, b, 1);

	return ml_symctx_ok (&c->ctx) && ml_sym_equal (&c->ctx, na, nb);
}

/*
 * 1 when two equations say the same: the difference of the sides is the
 * same, or its negative, after expanding.
 */
static int32_t same_eq (Checker *c, Eq a, Eq b)
{
	const MlSym *da = NULL;
	const MlSym *db = NULL;
	int32_t      ok = 0;

	ctx_reset (c);
	da = ml_sym_sub (&c->ctx, a.l, a.r);
	db = ml_sym_sub (&c->ctx, b.l, b.r);
	ok = ml_symctx_ok (&c->ctx);

	if (ok)
	{
		const MlSym *nb = ml_sym_neg (&c->ctx, db);

		ok = (same_formula (c, da, db) || same_formula (c, da, nb));
	}

	return ok;
}

/* ---- counterexamples --------------------------------------------------- */

#define SAMPLE_COUNT 6

static const int64_t samples [SAMPLE_COUNT] = { 2, 3, -1, 5, 7, -2 };

/* The symbols of a formula that stand for unknowns, once each. */
static MlResult collect_vars (NameVec *names, const MlSym *root)
{
	MlResult r = ML_OK;
	SymVec   stack;

	SymVec_init (&stack);
	r = SymVec_push (&stack, root);

	while (r == ML_OK && stack.len > 0)
	{
		const MlSym *s = SymVec_get (&stack, stack.len - 1);

		SymVec_pop (&stack);

		if (s->kind == ML_SYM_VAR
			&& ml_word_find (s->name).kind == ML_WORD_NONE)
		{
			int32_t known = 0;

			for (uint64_t i = 0; i < names->len; i++)
			{
				known = known || ml_slice_equal (names->data [i], s->name);
			}

			if (!known)
			{
				r = NameVec_push (names, s->name);
			}
		}

		for (uint64_t k = 0; k < s->count && r == ML_OK; k++)
		{
			r = SymVec_push (&stack, s->kids [k]);
		}
	}

	SymVec_free (&stack);
	return r;
}

/* 1 when the two values are certainly different. */
static int32_t values_differ (Checker *c, const MlSym *a, const MlSym *b)
{
	int32_t differ = 0;
	double  x      = 0.0;
	double  y      = 0.0;

	if (a->kind == ML_SYM_NUM && b->kind == ML_SYM_NUM)
	{
		differ = !ml_sym_equal (&c->ctx, a, b);
	}

	else if (ml_sym_to_double (ml_arena_app (), a, &x)
			 && ml_sym_to_double (ml_arena_app (), b, &y))
	{
		double scale = 1.0 + fabs (x) + fabs (y);

		differ = isfinite (x) && isfinite (y) && fabs (x - y) > 1e-6 * scale;
	}

	return differ;
}

/*
 * Tries the equation at a few whole numbers. 1 when the sides differ at one
 * of them; point says where and left and right say what each side is.
 */
static int32_t refute (
	Checker *c,
	Eq       eq,
	MlStr   *point,
	MlStr   *left,
	MlStr   *right
)
{
	int32_t  found = 0;
	NameVec  names;
	MlResult r     = ML_OK;
	uint64_t tries = 1;

	NameVec_init (&names);
	r     = collect_vars (&names, eq.l);
	r     = ml_choose (r == ML_OK, collect_vars (&names, eq.r), r);
	tries = ml_choose (names.len > 0, SAMPLE_COUNT, 1);

	for (uint64_t k = 0; k < tries && r == ML_OK && !found; k++)
	{
		const MlSym *ls = eq.l;
		const MlSym *rs = eq.r;

		ctx_reset (c);

		for (uint64_t i = 0; i < names.len; i++)
		{
			int64_t      v   = samples [(k + 2 * i) % SAMPLE_COUNT];
			const MlSym *var = ml_sym_var (&c->ctx, names.data [i]);
			const MlSym *val = ml_sym_int (&c->ctx, v);

			ls = ml_sym_substitute (&c->ctx, ls, var, val);
			rs = ml_sym_substitute (&c->ctx, rs, var, val);
		}

		if (ml_symctx_ok (&c->ctx) && values_differ (c, ls, rs))
		{
			found = 1;
			ml_str_clear (point);
			ml_str_clear (left);
			ml_str_clear (right);

			for (uint64_t i = 0; i < names.len && r == ML_OK; i++)
			{
				r = ml_str_appendf (
					point,
					"%s%.*s = %lld",
					ml_choose (i > 0, ", ", ""),
					(int)names.data [i].len,
					names.data [i].data,
					(long long)samples [(k + 2 * i) % SAMPLE_COUNT]
				);
			}

			r = ml_choose (r == ML_OK, ml_sym_format (left, ls), r);
			r = ml_choose (r == ML_OK, ml_sym_format (right, rs), r);
		}
	}

	c->status = ml_choose (c->status == ML_OK, r, c->status);
	NameVec_free (&names);
	return found;
}

/* ---- the justifications ------------------------------------------------ */

/* simplify (how 0) and expand (how 1): both sides have the same form. */
static void justify_form (Checker *c, MlProofLine *out, Eq eq, int32_t how)
{
	const MlSym *a  = normal (c, eq.l, how);
	const MlSym *b  = normal (c, eq.r, how);
	int32_t      ok = ml_symctx_ok (&c->ctx);

	if (ok && ml_sym_equal (&c->ctx, a, b))
	{
		mark_plain (c, out, ML_PROOF_VALID, NULL);
	}

	else if (!ok)
	{
		mark_plain (c, out, ML_PROOF_UNCHECKED, "proof.too_big");
	}

	else
	{
		MlStr point;
		MlStr left;
		MlStr right;

		ml_str_init (&point);
		ml_str_init (&left);
		ml_str_init (&right);

		if (refute (c, eq, &point, &left, &right))
		{
			MlTextArg args [3] = {
				{ .name = "point", .value = ml_str_view (point) },
				{ .name = "left",  .value = ml_str_view (left)  },
				{ .name = "right", .value = ml_str_view (right) }
			};

			mark (c, out, ML_PROOF_INVALID, "proof.not_equal", args, 3);
		}

		else
		{
			MlSlice   how_text = ml_slice_cstr (
				ml_choose (how == 1, "expand", "simplify")
			);
			MlTextArg args [1] = {
				{ .name = "how", .value = how_text }
			};

			mark (c, out, ML_PROOF_UNCHECKED, "proof.undecided", args, 1);
		}

		ml_str_free (&point);
		ml_str_free (&left);
		ml_str_free (&right);
	}
}

static void justify_hypothesis (Checker *c, MlProofLine *out, Eq eq)
{
	int32_t ok = 0;

	for (uint64_t i = 0; i < c->hyps.len && !ok; i++)
	{
		ok = same_eq (c, eq, c->hyps.data [i]);
	}

	mark_plain (
		c,
		out,
		ml_choose (ok, ML_PROOF_VALID, ML_PROOF_UNCHECKED),
		ml_choose (ok, NULL, "proof.hyp_missing")
	);
}

/* "step N": the equation is the N-th step above, and that step is valid. */
static void justify_step (Checker *c, MlProofLine *out, Eq eq, MlSlice arg)
{
	uint64_t n    = 0;
	uint64_t used = 0;
	uint64_t here = c->steps.len;
	char     buf [ML_TEXT_NUMBER];

	while (used < arg.len && arg.data [used] >= '0' && arg.data [used] <= '9')
	{
		n = ml_choose (
			n < 100000,
			n * 10 + (uint64_t)(arg.data [used] - '0'),
			n
		);
		used++;
	}

	if (arg.len == 0 || used != arg.len || n < 1 || n >= here)
	{
		MlTextArg args [1] = {
			{ .name = "n", .value = arg }
		};

		mark (c, out, ML_PROOF_INVALID, "proof.step_missing", args, 1);
	}

	else
	{
		MlTextArg args [1] = {
			{ .name = "n", .value = ml_text_number (buf, n) }
		};

		Step st = c->steps.data [n - 1];

		if (!st.valid)
		{
			mark (c, out, ML_PROOF_UNCHECKED, "proof.step_not_valid", args, 1);
		}

		else if (same_eq (c, eq, st.eq))
		{
			mark_plain (c, out, ML_PROOF_VALID, NULL);
		}

		else
		{
			mark (c, out, ML_PROOF_UNCHECKED, "proof.step_differs", args, 1);
		}
	}
}

static void justify_lemma (Checker *c, MlProofLine *out, Eq eq, MlSlice name)
{
	uint64_t at = c->lemmas.len;

	for (uint64_t i = 0; i < c->lemmas.len; i++)
	{
		at = ml_choose (ml_slice_equal (c->lemmas.data [i].name, name), i, at);
	}

	if (at >= c->lemmas.len)
	{
		MlTextArg args [1] = {
			{ .name = "name", .value = name }
		};

		mark (c, out, ML_PROOF_INVALID, "proof.lemma_unknown", args, 1);
	}

	else
	{
		MlTextArg args [1] = {
			{ .name = "name", .value = name }
		};

		Lemma lem = c->lemmas.data [at];

		if (lem.has_hyps)
		{
			mark (c, out, ML_PROOF_UNCHECKED, "proof.lemma_given", args, 1);
		}

		else if (same_eq (c, eq, lem.eq))
		{
			mark_plain (c, out, ML_PROOF_VALID, NULL);
		}

		else
		{
			mark (c, out, ML_PROOF_UNCHECKED, "proof.lemma_differs", args, 1);
		}
	}
}

/* ---- the lines --------------------------------------------------------- */

typedef enum LineKind
{
	LINE_BLANK = 0,
	LINE_THEOREM,
	LINE_GIVEN,
	LINE_QED,
	LINE_STEP
} LineKind;

static LineKind line_kind (MlSlice line)
{
	LineKind k = LINE_STEP;

	if (line.len == 0 || line.data [0] == '#')
	{
		k = LINE_BLANK;
	}

	else if (starts_word (line, "theorem"))
	{
		k = LINE_THEOREM;
	}

	else if (starts_word (line, "given"))
	{
		k = LINE_GIVEN;
	}

	else if (starts_word (line, "qed"))
	{
		k = LINE_QED;
	}

	return k;
}

/* The equations of "A, B, C": the ones with a = are hypotheses. */
static void add_givens (Checker *c, MlProofLine *out, MlSlice list)
{
	uint64_t pos = 0;
	int32_t  ok  = 1;

	mark_plain (c, out, ML_PROOF_NONE, NULL);

	while (pos <= list.len && ok)
	{
		uint64_t end   = pos;
		int64_t  depth = 0;
		MlSlice  item;

		while (end < list.len && (depth > 0 || list.data [end] != ','))
		{
			depth += ml_choose (opens (list.data [end]), 1, 0);
			depth -= ml_choose (closes (list.data [end]), 1, 0);
			end++;
		}

		item = trim (ml_slice_sub (list, pos, end - pos));

		if (find_equals (item) < item.len)
		{
			EqRead e = read_eq (c, item);

			if (e.code == EQ_OK)
			{
				c->status = ml_choose (
					c->status == ML_OK,
					EqVec_push (&c->hyps, e.eq),
					c->status
				);
			}

			else
			{
				MlTextArg args [1] = {
					{ .name = "text", .value = e.bad }
				};

				mark (c, out, ML_PROOF_SYNTAX, "proof.bad_formula", args, 1);
				ok = 0;
			}
		}

		pos = end + 1;
	}
}

static void start_theorem (Checker *c, MlProofLine *out, MlSlice line)
{
	MlSlice  rest  = after_word (line);
	uint64_t colon = 0;
	uint64_t giv   = 0;
	MlSlice  claim;
	EqRead   e;

	while (colon < rest.len && rest.data [colon] != ':')
	{
		colon++;
	}

	EqVec_clear (&c->hyps);
	StepVec_clear (&c->steps);
	c->open      = 0;
	c->has_last  = 0;
	c->errors    = 0;
	c->sorries   = 0;
	c->unchecked = 0;

	if (colon >= rest.len)
	{
		mark_plain (c, out, ML_PROOF_SYNTAX, "proof.theorem_form");
	}

	else
	{
		MlSlice tail
			= trim (ml_slice_sub (rest, colon + 1, rest.len - colon - 1));

		giv   = find_word (tail, "given");
		claim = trim (ml_slice_sub (tail, 0, giv));
		e     = read_eq (c, claim);

		if (e.code == EQ_NO_EQUALS)
		{
			mark_plain (c, out, ML_PROOF_SYNTAX, "proof.theorem_form");
		}

		else if (e.code == EQ_BAD_SIDE)
		{
			MlTextArg args [1] = {
				{ .name = "text", .value = e.bad }
			};

			mark (c, out, ML_PROOF_SYNTAX, "proof.bad_formula", args, 1);
		}

		else
		{
			c->open = 1;
			c->name = trim (ml_slice_sub (rest, 0, colon));
			c->goal = e.eq;
			mark_plain (c, out, ML_PROOF_NONE, NULL);

			if (giv < tail.len)
			{
				add_givens (
					c,
					out,
					after_word (ml_slice_sub (tail, giv, tail.len - giv))
				);
			}
		}
	}
}

/* One step: a claim, "by", and the reason. */
static void check_step (Checker *c, MlProofLine *out, MlSlice line)
{
	uint64_t by    = find_word (line, "by");
	MlSlice  claim = trim (ml_slice_sub (line, 0, by));
	MlSlice  just  = ml_slice ("", 0);
	int32_t  cont  = 0;
	EqRead   e     = {
			  .code = EQ_OK,
			  .eq   = { .l = NULL, .r = NULL }
	};

	if (by < line.len)
	{
		just = after_word (ml_slice_sub (line, by, line.len - by));
	}

	if (claim.len >= 3 && memcmp (claim.data, "...", 3) == 0)
	{
		claim = trim (ml_slice_sub (claim, 3, claim.len - 3));
		cont  = 1;
	}

	if (claim.len > 0 && claim.data [0] == '=')
	{
		claim = trim (ml_slice_sub (claim, 1, claim.len - 1));
		cont  = 1;
	}

	if (cont && !c->has_last)
	{
		e.code = EQ_NO_EQUALS;
		mark_plain (c, out, ML_PROOF_SYNTAX, "proof.no_previous");
	}

	else if (cont)
	{
		e.eq.l = c->last_r;
		e.eq.r = read_side (c, claim);
		e.code = ml_choose (e.eq.r != NULL, EQ_OK, EQ_BAD_SIDE);
		e.bad  = claim;
	}

	else
	{
		e = read_eq (c, claim);
	}

	if (e.code == EQ_BAD_SIDE)
	{
		MlTextArg args [1] = {
			{ .name = "text", .value = e.bad }
		};

		mark (c, out, ML_PROOF_SYNTAX, "proof.bad_formula", args, 1);
	}

	else if (e.code == EQ_NO_EQUALS && !cont)
	{
		mark_plain (c, out, ML_PROOF_SYNTAX, "proof.step_form");
	}

	else if (e.code == EQ_OK)
	{
		MlSlice word = first_word (just);
		MlSlice arg  = after_word (just);
		Step    st   = { .eq = e.eq, .valid = 0 };

		c->last_r   = e.eq.r;
		c->has_last = 1;
		c->status   = ml_choose (
			c->status == ML_OK,
			StepVec_push (&c->steps, st),
			c->status
		);

		if (just.len == 0)
		{
			mark_plain (c, out, ML_PROOF_UNCHECKED, "proof.no_by");
		}

		else if (ml_slice_equal (word, ml_slice_cstr ("simplify")))
		{
			justify_form (c, out, e.eq, 0);
		}

		else if (ml_slice_equal (word, ml_slice_cstr ("expand")))
		{
			justify_form (c, out, e.eq, 1);
		}

		else if (ml_slice_equal (word, ml_slice_cstr ("hypothesis")))
		{
			justify_hypothesis (c, out, e.eq);
		}

		else if (ml_slice_equal (word, ml_slice_cstr ("step")))
		{
			justify_step (c, out, e.eq, arg);
		}

		else if (ml_slice_equal (word, ml_slice_cstr ("lemma")))
		{
			justify_lemma (c, out, e.eq, arg);
		}

		else if (ml_slice_equal (word, ml_slice_cstr ("sorry")))
		{
			mark_plain (c, out, ML_PROOF_SORRY, "proof.sorry");
		}

		else if (ml_slice_equal (word, ml_slice_cstr ("rule")))
		{
			mark_plain (c, out, ML_PROOF_UNCHECKED, "proof.rule_unsupported");
		}

		else
		{
			MlTextArg args [1] = {
				{ .name = "word", .value = word }
			};

			mark (c, out, ML_PROOF_SYNTAX, "proof.unknown_by", args, 1);
		}

		if (c->status == ML_OK)
		{
			c->steps.data [c->steps.len - 1].valid
				= out->mark == ML_PROOF_VALID;
		}
	}

	if (out->mark == ML_PROOF_INVALID || out->mark == ML_PROOF_SYNTAX)
	{
		c->errors++;
	}

	else if (out->mark == ML_PROOF_UNCHECKED)
	{
		c->unchecked++;
	}

	else if (out->mark == ML_PROOF_SORRY)
	{
		c->sorries++;
	}
}

/*
 * qed: the steps lead from the left side of the claim to its right side, and
 * every one of them is valid.
 */
static int32_t chain_reaches_goal (Checker *c, MlStr *stop)
{
	const MlSym *at = c->goal.l;
	int32_t      ok = 0;

	for (uint64_t i = 0; i < c->steps.len; i++)
	{
		if (same_formula (c, c->steps.data [i].eq.l, at))
		{
			at = c->steps.data [i].eq.r;
		}
	}

	ok = same_formula (c, at, c->goal.r);

	if (!ok)
	{
		c->status = ml_choose (
			c->status == ML_OK,
			ml_sym_format (stop, at),
			c->status
		);
	}

	return ok;
}

/* Returns 1 when the theorem is closed. */
static int32_t close_theorem (Checker *c, MlProofLine *out)
{
	int32_t proved = 0;
	MlStr   stop;

	ml_str_init (&stop);

	if (c->errors > 0)
	{
		mark_plain (c, out, ML_PROOF_INVALID, "proof.qed_errors");
	}

	else if (c->sorries > 0)
	{
		mark_plain (c, out, ML_PROOF_SORRY, "proof.qed_sorry");
	}

	else if (c->unchecked > 0)
	{
		char      buf [ML_TEXT_NUMBER];
		MlTextArg args [1] = {
			{ .name = "count", .value = ml_text_number (buf, c->unchecked) }
		};

		mark (c, out, ML_PROOF_UNCHECKED, "proof.qed_unchecked", args, 1);
	}

	else if (!chain_reaches_goal (c, &stop))
	{
		MlTextArg args [1] = {
			{ .name = "at", .value = ml_str_view (stop) }
		};

		mark (c, out, ML_PROOF_INVALID, "proof.qed_goal", args, 1);
	}

	else
	{
		Lemma lem
			= { .name = c->name, .eq = c->goal, .has_hyps = c->hyps.len > 0 };

		proved    = 1;
		c->status = ml_choose (
			c->status == ML_OK,
			LemmaVec_push (&c->lemmas, lem),
			c->status
		);
		mark_plain (c, out, ML_PROOF_VALID, "proof.qed_ok");
	}

	ml_str_free (&stop);
	return proved;
}

static void check_line (
	Checker       *c,
	MlProofReport *rep,
	MlSlice        raw,
	MlProofLine   *out
)
{
	MlSlice  line = trim (raw);
	LineKind kind = line_kind (line);

	if (kind != LINE_BLANK && kind != LINE_THEOREM && !c->open)
	{
		mark_plain (c, out, ML_PROOF_SYNTAX, "proof.outside");
	}

	else
	{
		switch (kind)
		{
			case LINE_THEOREM:
			{
				rep->theorems++;
				start_theorem (c, out, line);
			} break;

			case LINE_GIVEN:
			{
				add_givens (c, out, after_word (line));
			} break;

			case LINE_QED:
			{
				rep->proved += close_theorem (c, out);
				c->open      = 0;
			} break;

			case LINE_STEP:
			{
				check_step (c, out, line);
			} break;

			default:
			{
				mark_plain (c, out, ML_PROOF_NONE, NULL);
			} break;
		}
	}
}

MlResult ml_proof_check (MlProofReport *rep, MlSlice source)
{
	MlResult    r   = ML_OK;
	MlArena    *app = ml_arena_app ();
	MlArenaMark mk  = ml_arena_mark (app);
	Checker     c;
	uint64_t    pos = 0;

	ml_proof_report_free (rep);
	ml_proof_report_init (rep);
	ml_symctx_init (&c.ctx, app);
	ml_env_init (&c.env);
	EqVec_init (&c.hyps);
	StepVec_init (&c.steps);
	LemmaVec_init (&c.lemmas);
	c.status    = ML_OK;
	c.open      = 0;
	c.has_last  = 0;
	c.last_r    = NULL;
	c.errors    = 0;
	c.sorries   = 0;
	c.unchecked = 0;

	while (r == ML_OK && c.status == ML_OK && pos <= source.len)
	{
		uint64_t    end  = pos;
		MlProofLine line = { .mark = ML_PROOF_NONE };

		while (end < source.len && source.data [end] != '\n')
		{
			end++;
		}

		ml_str_init (&line.note);
		check_line (&c, rep, ml_slice_sub (source, pos, end - pos), &line);
		r   = MlProofLineVec_push (&rep->lines, line);
		pos = end + 1;

		if (r != ML_OK)
		{
			ml_str_free (&line.note);
		}
	}

	r = ml_choose (r == ML_OK, c.status, r);

	EqVec_free (&c.hyps);
	StepVec_free (&c.steps);
	LemmaVec_free (&c.lemmas);
	ml_env_free (&c.env);
	ml_arena_rewind (app, mk);
	return r;
}
