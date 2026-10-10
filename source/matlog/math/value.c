#include <matlog/math/value.h>

#include <math.h>

#include <matlog/math/hyper.h>

/* Approximations are written with this many significant digits. */
#define APPROX_DIGITS 9

static const char *const error_texts [ML_EVAL_KIND_COUNT] = {
	[ML_EVAL_NONE]           = "no error",
	[ML_EVAL_SYNTAX]         = "syntax error",
	[ML_EVAL_DIV_ZERO]       = "division by zero",
	[ML_EVAL_ZERO_POW_ZERO]  = "0 to the power 0 is undefined",
	[ML_EVAL_NOT_REAL]       = "the result is not a real number",
	[ML_EVAL_DOMAIN]         = "argument outside the domain of the function",
	[ML_EVAL_NOT_INTEGER]    = "an integer is needed",
	[ML_EVAL_UNDEFINED_NAME] = "undefined name",
	[ML_EVAL_NOT_FUNCTION]   = "not a function",
	[ML_EVAL_NOT_VALUE]      = "a function is not a number",
	[ML_EVAL_ARITY]          = "wrong number of arguments",
	[ML_EVAL_TOO_LARGE]      = "value too large",
	[ML_EVAL_DEPTH]          = "recursion too deep",
	[ML_EVAL_NO_FILE]        = "included file not found",
	[ML_EVAL_INCLUDE_LOOP]   = "include cycle",
	[ML_EVAL_SIZE]           = "matrix sizes do not fit",
	[ML_EVAL_TYPE]           = "these operands cannot be combined",
	[ML_EVAL_NOT_EXACT]      = "an exact number is needed here",
	[ML_EVAL_CANNOT_INTEGRATE] = "no closed form found",
	[ML_EVAL_NOT_SYMBOLIC]   = "not a formula that can be worked on",
	[ML_EVAL_SINGULAR]       = "matrix is singular",
	[ML_EVAL_DIMENSION_MISMATCH] = "the units have different dimensions",
	[ML_EVAL_UNKNOWN_UNIT]   = "unknown unit",
	[ML_EVAL_NO_LIMIT]       = "no limit found, or the two sides differ",
	[ML_EVAL_CANNOT_SOLVE]   = "the equation cannot be solved in closed form",
	[ML_EVAL_NO_MEMORY]      = "out of memory",
	[ML_EVAL_TOO_LONG]       = "the computation took too many steps"
};

const char *ml_error_text (MlErrorKind kind)
{
	return ml_choose (
		(int32_t)kind >= 0 && kind < ML_EVAL_KIND_COUNT,
		error_texts [kind],
		"unknown error"
	);
}

static const char *const error_codes [ML_EVAL_KIND_COUNT] = {
	[ML_EVAL_NONE]                  = "none",
	[ML_EVAL_SYNTAX]                = "syntax",
	[ML_EVAL_DIV_ZERO]              = "div_zero",
	[ML_EVAL_ZERO_POW_ZERO]         = "zero_pow_zero",
	[ML_EVAL_NOT_REAL]              = "not_real",
	[ML_EVAL_DOMAIN]                = "domain",
	[ML_EVAL_NOT_INTEGER]           = "not_integer",
	[ML_EVAL_UNDEFINED_NAME]        = "undefined_name",
	[ML_EVAL_NOT_FUNCTION]          = "not_function",
	[ML_EVAL_NOT_VALUE]             = "not_value",
	[ML_EVAL_ARITY]                 = "arity",
	[ML_EVAL_TOO_LARGE]             = "too_large",
	[ML_EVAL_DEPTH]                 = "depth",
	[ML_EVAL_NO_FILE]               = "no_file",
	[ML_EVAL_INCLUDE_LOOP]          = "include_loop",
	[ML_EVAL_SIZE]                  = "size",
	[ML_EVAL_TYPE]                  = "type",
	[ML_EVAL_NOT_EXACT]             = "not_exact",
	[ML_EVAL_CANNOT_INTEGRATE]      = "cannot_integrate",
	[ML_EVAL_NOT_SYMBOLIC]          = "not_symbolic",
	[ML_EVAL_SINGULAR]              = "singular",
	[ML_EVAL_DIMENSION_MISMATCH]    = "dimension_mismatch",
	[ML_EVAL_UNKNOWN_UNIT]          = "unknown_unit",
	[ML_EVAL_NO_LIMIT]              = "no_limit",
	[ML_EVAL_CANNOT_SOLVE]          = "cannot_solve",
	[ML_EVAL_NO_MEMORY]             = "no_memory",
	[ML_EVAL_TOO_LONG]              = "too_long"
};

const char *ml_error_code (MlErrorKind kind)
{
	return ml_choose (
		(int32_t)kind >= 0 && kind < ML_EVAL_KIND_COUNT,
		error_codes [kind],
		"unknown"
	);
}

MlErrorKind ml_error_from_result (MlResult status)
{
	MlErrorKind kind = ML_EVAL_SYNTAX;

	if (status == ML_ERR_DIVZERO)
	{
		kind = ML_EVAL_DIV_ZERO;
	}

	else if (status == ML_ERR_LIMIT)
	{
		kind = ML_EVAL_TOO_LARGE;
	}

	else if (status == ML_ERR_NOMEM)
	{
		kind = ML_EVAL_NO_MEMORY;
	}

	else if (status == ML_ERR_RANGE)
	{
		kind = ML_EVAL_DOMAIN;
	}

	return kind;
}

MlValue ml_value_error (MlErrorKind kind, uint64_t start, uint64_t len)
{
	MlValue v = {
		.kind        = ML_VALUE_ERROR,
		.exact       = ml_rat_error (ML_OK),
		.dim         = { .e = { 0, 0, 0, 0, 0, 0, 0 } },
		.show        = { .data = NULL, .len = 0 },
		.scale       = ml_rat_error (ML_OK),
		.matrix      = ml_mat_error (ML_OK),
		.sym         = NULL,
		.approx      = 0.0,
		.error       = kind,
		.error_start = start,
		.error_len   = len
	};

	return v;
}

MlValue ml_value_none (void)
{
	MlValue v = ml_value_error (ML_EVAL_NONE, 0, 0);

	v.kind = ML_VALUE_NONE;
	return v;
}

MlValue ml_value_exact (MlRat x)
{
	MlValue v = ml_value_error (ML_EVAL_NONE, 0, 0);

	if (ml_rat_ok (x))
	{
		v.kind  = ML_VALUE_EXACT;
		v.exact = x;
	}

	else
	{
		v = ml_value_error (ml_error_from_result (ml_rat_status (x)), 0, 0);
	}

	return v;
}

MlValue ml_value_approx (double x)
{
	MlValue v = ml_value_error (ML_EVAL_NONE, 0, 0);

	if (isfinite (x))
	{
		v.kind   = ML_VALUE_APPROX;
		v.approx = x;
	}

	else
	{
		v = ml_value_error (
			ml_choose (isnan (x), ML_EVAL_NOT_REAL, ML_EVAL_TOO_LARGE),
			0,
			0
		);
	}

	return v;
}

MlValue ml_value_matrix (MlMat m)
{
	MlValue v = ml_value_error (ML_EVAL_NONE, 0, 0);

	if (m.status == ML_OK)
	{
		v.kind   = ML_VALUE_MATRIX;
		v.matrix = m;
	}

	else
	{
		v = ml_value_error (
			ml_choose (
				m.status == ML_ERR_INVALID,
				ML_EVAL_SIZE,
				ml_choose (
					m.status == ML_ERR_DIVZERO,
					ML_EVAL_SINGULAR,
					ml_error_from_result (m.status)
				)
			),
			0,
			0
		);
	}

	return v;
}

MlValue ml_value_plot (const MlPlotData *plot)
{
	MlValue v = ml_value_none ();

	v.kind = ML_VALUE_PLOT;
	v.plot = plot;

	return v;
}

int32_t ml_value_is_list (MlValue v)
{
	return v.kind == ML_VALUE_LIST;
}

MlValue ml_value_list (MlArena *arena, const MlValue *items, uint64_t n)
{
	MlValue     r    = ml_value_none ();
	MlListData *data = NULL;
	MlValue     *own = NULL;

	for (uint64_t i = 0; i < n && r.kind == ML_VALUE_NONE; i++)
	{
		if (items [i].kind == ML_VALUE_ERROR)
		{
			r = items [i];
		}

		else if (items [i].kind == ML_VALUE_LIST)
		{
			r = ml_value_error (ML_EVAL_TYPE, 0, 0);
		}
	}

	if (r.kind == ML_VALUE_NONE && n > ML_LIST_MAX)
	{
		r = ml_value_error (ML_EVAL_TOO_LARGE, 0, 0);
	}

	if (r.kind == ML_VALUE_NONE)
	{
		data = ML_ARENA_NEW (arena, MlListData);
		own  = ML_ARENA_NEW_ARRAY (arena, MlValue, ml_choose (n > 0, n, 1));

		if (data == NULL || own == NULL)
		{
			r = ml_value_error (ML_EVAL_NO_MEMORY, 0, 0);
		}
	}

	if (r.kind == ML_VALUE_NONE)
	{
		for (uint64_t i = 0; i < n; i++)
		{
			own [i] = items [i];
		}

		data->count = n;
		data->items = own;
		r.kind      = ML_VALUE_LIST;
		r.list      = data;
	}

	return r;
}

MlValue ml_value_bool (int32_t flag)
{
	MlValue v = ml_value_none ();

	v.kind = ML_VALUE_BOOL;
	v.flag = flag != 0;

	return v;
}

/* 1 when the matrices have the same size and cells. */
static int32_t matrices_equal (MlArena *arena, MlMat a, MlMat b)
{
	int32_t same = a.rows == b.rows && a.cols == b.cols;

	for (uint64_t i = 0; i < a.rows * a.cols && same; i++)
	{
		same = ml_rat_cmp (arena, a.cells [i], b.cells [i]) == 0;
	}

	return same;
}

/* The result of a comparison, given the order -1, 0 or 1 of the operands. */
static MlValue compare_order (MlCompare op, int32_t order)
{
	int32_t holds;

	if (op == ML_COMPARE_LT)
	{
		holds = order < 0;
	}

	else if (op == ML_COMPARE_GT)
	{
		holds = order > 0;
	}

	else if (op == ML_COMPARE_LE)
	{
		holds = order <= 0;
	}

	else if (op == ML_COMPARE_GE)
	{
		holds = order >= 0;
	}

	else if (op == ML_COMPARE_EQ)
	{
		holds = order == 0;
	}

	else
	{
		holds = order != 0;
	}

	return ml_value_bool (holds);
}

/* The comparison of two values that are not lists. */
static MlValue compare_scalar (
	MlArena   *arena,
	MlCompare op,
	MlValue   a,
	MlValue   b
)
{
	MlValue r;
	int32_t equality = op == ML_COMPARE_EQ || op == ML_COMPARE_NE;

	if (a.kind == ML_VALUE_ERROR)
	{
		r = a;
	}

	else if (b.kind == ML_VALUE_ERROR)
	{
		r = b;
	}

	else if (ml_value_is_number (a) && ml_value_is_number (b))
	{
		r = compare_order (op, ml_value_cmp (arena, a, b));
	}

	else if (a.kind == ML_VALUE_QUANTITY && b.kind == ML_VALUE_QUANTITY)
	{
		r = ml_choose (
			ml_dim_equal (a.dim, b.dim),
			compare_order (op, ml_value_cmp (arena, a, b)),
			ml_value_error (ML_EVAL_DIMENSION_MISMATCH, 0, 0)
		);
	}

	else if (a.kind == ML_VALUE_QUANTITY || b.kind == ML_VALUE_QUANTITY)
	{
		r = ml_choose (
			ml_value_is_number (a) || ml_value_is_number (b),
			ml_value_error (ML_EVAL_DIMENSION_MISMATCH, 0, 0),
			ml_value_error (ML_EVAL_TYPE, 0, 0)
		);
	}

	else if (equality && a.kind == ML_VALUE_BOOL && b.kind == ML_VALUE_BOOL)
	{
		r = compare_order (op, ml_choose (a.flag == b.flag, 0, 1));
	}

	else if (equality && a.kind == ML_VALUE_MATRIX && b.kind == ML_VALUE_MATRIX)
	{
		r = compare_order (
			op,
			ml_choose (matrices_equal (arena, a.matrix, b.matrix), 0, 1)
		);
	}

	else
	{
		r = ml_value_error (ML_EVAL_TYPE, 0, 0);
	}

	return r;
}

/* 1 when the lists have the same length and equal items. */
static int32_t lists_equal (
	MlArena          *arena,
	const MlListData *a,
	const MlListData *b
)
{
	int32_t same = a->count == b->count;

	for (uint64_t i = 0; i < a->count && same; i++)
	{
		MlValue eq = compare_scalar (
			arena,
			ML_COMPARE_EQ,
			a->items [i],
			b->items [i]
		);

		same = eq.kind == ML_VALUE_BOOL && eq.flag;
	}

	return same;
}

MlValue ml_value_compare (MlArena *arena, MlCompare op, MlValue a, MlValue b)
{
	MlValue r;

	if (
		   (op == ML_COMPARE_EQ || op == ML_COMPARE_NE)
		&& a.kind == ML_VALUE_LIST
		&& b.kind == ML_VALUE_LIST
	)
	{
		r = compare_order (
			op,
			ml_choose (lists_equal (arena, a.list, b.list), 0, 1)
		);
	}

	else
	{
		r = compare_scalar (arena, op, a, b);
	}

	return r;
}

MlValue ml_value_expr (const MlSym *sym, MlResult status)
{
	MlValue v = ml_value_error (ML_EVAL_NONE, 0, 0);

	if (status != ML_OK)
	{
		v = ml_value_error (
			ml_choose (
				status == ML_ERR_INVALID,
				ML_EVAL_NOT_SYMBOLIC,
				ml_error_from_result (status)
			),
			0,
			0
		);
	}

	else if (sym->kind == ML_SYM_NUM)
	{
		v = ml_value_exact (sym->num);
	}

	else
	{
		v.kind = ML_VALUE_EXPR;
		v.sym  = sym;
	}

	return v;
}

MlValue ml_value_quantity (MlRat si, MlDim dim)
{
	MlValue v = ml_value_exact (si);

	if (v.kind == ML_VALUE_EXACT && !ml_dim_is_none (dim))
	{
		v.kind = ML_VALUE_QUANTITY;
		v.dim  = dim;
	}

	return v;
}

MlValue ml_value_from_i64 (MlArena *arena, int64_t n)
{
	return ml_value_exact (ml_rat_from_i64 (arena, n));
}

MlValue ml_value_locate (MlValue v, uint64_t start, uint64_t len)
{
	MlValue located = v;

	if (v.kind == ML_VALUE_ERROR && v.error_len == 0)
	{
		located.error_start = start;
		located.error_len   = len;
	}

	return located;
}

int32_t ml_value_is_error (MlValue v)
{
	return v.kind == ML_VALUE_ERROR;
}

int32_t ml_value_is_number (MlValue v)
{
	return v.kind == ML_VALUE_EXACT || v.kind == ML_VALUE_APPROX;
}

int32_t ml_value_is_exact (MlValue v)
{
	return v.kind == ML_VALUE_EXACT;
}

int32_t ml_value_is_matrix (MlValue v)
{
	return v.kind == ML_VALUE_MATRIX;
}

int32_t ml_value_is_quantity (MlValue v)
{
	return v.kind == ML_VALUE_QUANTITY;
}

int32_t ml_value_is_expr (MlValue v)
{
	return v.kind == ML_VALUE_EXPR;
}

MlI64Result ml_value_to_i64 (MlValue v)
{
	MlI64Result result = {
		.ok    = 0,
		.value = 0
	};

	if (v.kind == ML_VALUE_EXACT && ml_rat_is_integer (v.exact))
	{
		MlBig       magnitude = v.exact.num;
		int32_t     neg       = v.exact.num.negative;
		MlU64Result mag;

		magnitude.negative = 0;
		mag                = ml_big_to_u64 (magnitude);
		result.ok          = (
			   mag.ok
			&& ml_choose (
				neg,
				mag.value <= (uint64_t)INT64_MAX + 1,
				mag.value <= (uint64_t)INT64_MAX
			)
		);

		if (result.ok)
		{
			result.value = ml_choose (
				neg,
				(int64_t)(0 - mag.value),
				(int64_t)mag.value
			);
		}
	}

	return result;
}

double ml_value_to_double (MlArena *arena, MlValue v)
{
	return ml_choose (
		v.kind == ML_VALUE_EXACT,
		ml_rat_to_double (arena, v.exact),
		ml_choose (v.kind == ML_VALUE_APPROX, v.approx, 0.0)
	);
}

int32_t ml_value_cmp (MlArena *arena, MlValue a, MlValue b)
{
	double  x = ml_value_to_double (arena, a);
	double  y = ml_value_to_double (arena, b);
	int32_t order;

	if (
		   (a.kind == ML_VALUE_EXACT || a.kind == ML_VALUE_QUANTITY)
		&& (b.kind == ML_VALUE_EXACT || b.kind == ML_VALUE_QUANTITY)
	)
	{
		order = ml_rat_cmp (arena, a.exact, b.exact);
	}

	else
	{
		order = ml_choose (x < y, -1, ml_choose (x > y, 1, 0));
	}

	return order;
}

/* The first error among two operands, or a value that says there is none. */
static MlValue first_error (MlValue a, MlValue b)
{
	MlValue found = ml_value_none ();

	if (a.kind == ML_VALUE_ERROR)
	{
		found = a;
	}

	else if (b.kind == ML_VALUE_ERROR)
	{
		found = b;
	}

	else if (!ml_value_is_number (a) || !ml_value_is_number (b))
	{
		found = ml_value_error (ML_EVAL_SYNTAX, 0, 0);
	}

	return found;
}

/* The expression tree of a value, or NULL when it has none (a matrix). */
static const MlSym *sym_of (MlSymCtx *ctx, MlValue v)
{
	const MlSym *s = NULL;

	if (v.kind == ML_VALUE_EXACT)
	{
		s = ml_sym_num (ctx, v.exact);
	}

	else if (v.kind == ML_VALUE_EXPR)
	{
		s = v.sym;
	}

	return s;
}

typedef enum SymOp
{
	SYM_OP_ADD = 0,
	SYM_OP_MUL,
	SYM_OP_DIV,
	SYM_OP_POW
} SymOp;

/*
 * a op b when one of them is an expression. Errors first, then a matrix or an
 * approximate number is no operand of a formula.
 */
static MlValue expr_op (MlArena *arena, SymOp op, MlValue a, MlValue b)
{
	MlValue  result = ml_value_none ();
	MlSymCtx ctx;

	ml_symctx_init (&ctx, arena);

	if (a.kind == ML_VALUE_ERROR || b.kind == ML_VALUE_ERROR)
	{
		result = ml_choose (a.kind == ML_VALUE_ERROR, a, b);
	}

	else if (a.kind == ML_VALUE_APPROX || b.kind == ML_VALUE_APPROX)
	{
		result = ml_value_error (ML_EVAL_NOT_EXACT, 0, 0);
	}

	else if (sym_of (&ctx, a) == NULL || sym_of (&ctx, b) == NULL)
	{
		result = ml_value_error (ML_EVAL_TYPE, 0, 0);
	}

	else
	{
		const MlSym *x = sym_of (&ctx, a);
		const MlSym *y = sym_of (&ctx, b);
		const MlSym *z = ml_choose (
			op == SYM_OP_ADD,
			ml_sym_add (&ctx, x, y),
			ml_choose (
				op == SYM_OP_MUL,
				ml_sym_mul (&ctx, x, y),
				ml_choose (
					op == SYM_OP_DIV,
					ml_sym_div (&ctx, x, y),
					ml_sym_pow (&ctx, x, y)
				)
			)
		);

		result = ml_value_expr (z, ctx.status);
	}

	return result;
}

static int32_t involves_expr (MlValue a, MlValue b)
{
	return a.kind == ML_VALUE_EXPR || b.kind == ML_VALUE_EXPR;
}

typedef enum QuantityOp
{
	Q_ADD,
	Q_MUL,
	Q_DIV,
	Q_POW
} QuantityOp;

/* Quantities and exact numbers are the operands of the unit arithmetic. */
static int32_t involves_quantity (MlValue a, MlValue b)
{
	return a.kind == ML_VALUE_QUANTITY || b.kind == ML_VALUE_QUANTITY;
}

/*
 * What is wrong with the operands of a quantity operation, none when they are
 * exact numbers or quantities. Errors come first.
 */
static MlValue quantity_operands (MlValue a, MlValue b)
{
	MlValue found = ml_value_none ();

	if (a.kind == ML_VALUE_ERROR)
	{
		found = a;
	}

	else if (b.kind == ML_VALUE_ERROR)
	{
		found = b;
	}

	else if (a.kind == ML_VALUE_APPROX || b.kind == ML_VALUE_APPROX)
	{
		found = ml_value_error (ML_EVAL_NOT_EXACT, 0, 0);
	}

	else if (
		   (a.kind != ML_VALUE_EXACT && a.kind != ML_VALUE_QUANTITY)
		|| (b.kind != ML_VALUE_EXACT && b.kind != ML_VALUE_QUANTITY)
	)
	{
		found = ml_value_error (ML_EVAL_TYPE, 0, 0);
	}

	return found;
}

/* a + b: the same dimension, where a zero takes the dimension of the other. */
static MlValue quantity_add (MlArena *arena, MlValue a, MlValue b)
{
	MlDim da = a.dim;
	MlDim db = b.dim;

	if (a.kind == ML_VALUE_EXACT && ml_rat_is_zero (a.exact))
	{
		da = db;
	}

	if (b.kind == ML_VALUE_EXACT && ml_rat_is_zero (b.exact))
	{
		db = da;
	}

	return ml_choose (
		ml_dim_equal (da, db),
		ml_value_quantity (ml_rat_add (arena, a.exact, b.exact), da),
		ml_value_error (ML_EVAL_DIMENSION_MISMATCH, 0, 0)
	);
}

/* a ^ n for a quantity a and an exact integer n of moderate size. */
static MlValue quantity_pow (MlArena *arena, MlValue a, MlValue b)
{
	MlI64Result n      = ml_value_to_i64 (b);
	MlValue     result = ml_value_none ();

	if (b.kind != ML_VALUE_EXACT || a.kind != ML_VALUE_QUANTITY)
	{
		result = ml_value_error (ML_EVAL_DIMENSION_MISMATCH, 0, 0);
	}

	else if (!n.ok)
	{
		result = ml_value_error (ML_EVAL_NOT_INTEGER, 0, 0);
	}

	else if (n.value > 64 || n.value < -64)
	{
		result = ml_value_error (ML_EVAL_TOO_LARGE, 0, 0);
	}

	else
	{
		result = ml_value_quantity (
			ml_rat_pow (arena, a.exact, n.value),
			ml_dim_pow (a.dim, (int32_t)n.value)
		);
	}

	return result;
}

/* A binary operation where at least one operand is a quantity. */
static MlValue quantity_op (
	MlArena    *arena,
	QuantityOp op,
	MlValue    a,
	MlValue    b
)
{
	MlValue result = quantity_operands (a, b);

	if (result.kind == ML_VALUE_NONE && op == Q_ADD)
	{
		result = quantity_add (arena, a, b);
	}

	else if (result.kind == ML_VALUE_NONE && op == Q_MUL)
	{
		result = ml_value_quantity (
			ml_rat_mul (arena, a.exact, b.exact),
			ml_dim_mul (a.dim, b.dim)
		);
	}

	else if (result.kind == ML_VALUE_NONE && op == Q_DIV)
	{
		result = ml_choose (
			ml_rat_is_zero (b.exact),
			ml_value_error (ML_EVAL_DIV_ZERO, 0, 0),
			ml_value_quantity (
				ml_rat_div (arena, a.exact, b.exact),
				ml_dim_div (a.dim, b.dim)
			)
		);
	}

	else if (result.kind == ML_VALUE_NONE)
	{
		result = quantity_pow (arena, a, b);
	}

	return result;
}

MlValue ml_value_neg (MlArena *arena, MlValue a)
{
	MlValue result = a;

	if (a.kind == ML_VALUE_EXACT)
	{
		result = ml_value_exact (ml_rat_neg (arena, a.exact));
	}

	else if (a.kind == ML_VALUE_QUANTITY)
	{
		result = ml_value_quantity (ml_rat_neg (arena, a.exact), a.dim);
	}

	else if (ml_value_is_hyper (a))
	{
		result = ml_hyper_neg (arena, a);
	}

	else if (a.kind == ML_VALUE_APPROX)
	{
		result = ml_value_approx (-a.approx);
	}

	else if (a.kind == ML_VALUE_MATRIX)
	{
		result = ml_value_matrix (ml_mat_neg (arena, a.matrix));
	}

	else if (a.kind == ML_VALUE_EXPR)
	{
		result = expr_op (arena, SYM_OP_MUL, ml_value_from_i64 (arena, -1), a);
	}

	else if (a.kind != ML_VALUE_ERROR)
	{
		result = ml_value_error (ML_EVAL_SYNTAX, 0, 0);
	}

	return result;
}

/*
 * The result of a binary operation when a matrix is one of the operands, none
 * (ML_VALUE_NONE) when neither is one. Errors come first.
 */
static MlValue matrix_operands (MlValue a, MlValue b)
{
	MlValue found = ml_value_none ();

	if (a.kind == ML_VALUE_ERROR)
	{
		found = a;
	}

	else if (b.kind == ML_VALUE_ERROR)
	{
		found = b;
	}

	else if (a.kind == ML_VALUE_APPROX || b.kind == ML_VALUE_APPROX)
	{
		found = ml_value_error (ML_EVAL_NOT_EXACT, 0, 0);
	}

	else if (a.kind == ML_VALUE_MATRIX || b.kind == ML_VALUE_MATRIX)
	{
		found = ml_value_error (ML_EVAL_TYPE, 0, 0);
	}

	return found;
}

MlValue ml_value_add (MlArena *arena, MlValue a, MlValue b)
{
	MlValue result = first_error (a, b);

	if (ml_value_is_hyper (a) || ml_value_is_hyper (b))
	{
		result = ml_hyper_apply (arena, ML_HYPER_ADD, a, b);
	}

	else if (involves_quantity (a, b))
	{
		result = quantity_op (arena, Q_ADD, a, b);
	}

	else if (involves_expr (a, b))
	{
		result = expr_op (arena, SYM_OP_ADD, a, b);
	}

	else if (a.kind == ML_VALUE_MATRIX && b.kind == ML_VALUE_MATRIX)
	{
		result = ml_value_matrix (ml_mat_add (arena, a.matrix, b.matrix));
	}

	else if (a.kind == ML_VALUE_MATRIX || b.kind == ML_VALUE_MATRIX)
	{
		result = matrix_operands (a, b);
	}

	else if (
		   result.kind == ML_VALUE_NONE
		&& a.kind == ML_VALUE_EXACT
		&& b.kind == ML_VALUE_EXACT
	)
	{
		result = ml_value_exact (ml_rat_add (arena, a.exact, b.exact));
	}

	else if (result.kind == ML_VALUE_NONE)
	{
		result = ml_value_approx (
			ml_value_to_double (arena, a) + ml_value_to_double (arena, b)
		);
	}

	return result;
}

MlValue ml_value_sub (MlArena *arena, MlValue a, MlValue b)
{
	return ml_value_add (arena, a, ml_value_neg (arena, b));
}

MlValue ml_value_mul (MlArena *arena, MlValue a, MlValue b)
{
	MlValue result = first_error (a, b);

	if (ml_value_is_hyper (a) || ml_value_is_hyper (b))
	{
		result = ml_hyper_apply (arena, ML_HYPER_MUL, a, b);
	}

	else if (involves_quantity (a, b))
	{
		result = quantity_op (arena, Q_MUL, a, b);
	}

	else if (involves_expr (a, b))
	{
		result = expr_op (arena, SYM_OP_MUL, a, b);
	}

	else if (a.kind == ML_VALUE_MATRIX && b.kind == ML_VALUE_MATRIX)
	{
		result = ml_value_matrix (ml_mat_mul (arena, a.matrix, b.matrix));
	}

	else if (a.kind == ML_VALUE_MATRIX && b.kind == ML_VALUE_EXACT)
	{
		result = ml_value_matrix (ml_mat_scale (arena, a.matrix, b.exact));
	}

	else if (a.kind == ML_VALUE_EXACT && b.kind == ML_VALUE_MATRIX)
	{
		result = ml_value_matrix (ml_mat_scale (arena, b.matrix, a.exact));
	}

	else if (a.kind == ML_VALUE_MATRIX || b.kind == ML_VALUE_MATRIX)
	{
		result = matrix_operands (a, b);
	}

	else if (
		   result.kind == ML_VALUE_NONE
		&& a.kind == ML_VALUE_EXACT
		&& b.kind == ML_VALUE_EXACT
	)
	{
		result = ml_value_exact (ml_rat_mul (arena, a.exact, b.exact));
	}

	else if (result.kind == ML_VALUE_NONE)
	{
		result = ml_value_approx (
			ml_value_to_double (arena, a) * ml_value_to_double (arena, b)
		);
	}

	return result;
}

MlValue ml_value_div (MlArena *arena, MlValue a, MlValue b)
{
	MlValue result = first_error (a, b);

	if (ml_value_is_hyper (a) || ml_value_is_hyper (b))
	{
		result = ml_hyper_apply (arena, ML_HYPER_DIV, a, b);
	}

	else if (involves_quantity (a, b))
	{
		result = quantity_op (arena, Q_DIV, a, b);
	}

	else if (involves_expr (a, b))
	{
		result = expr_op (arena, SYM_OP_DIV, a, b);
	}

	else if (a.kind == ML_VALUE_MATRIX && b.kind == ML_VALUE_EXACT)
	{
		result = ml_choose (
			ml_rat_is_zero (b.exact),
			ml_value_error (ML_EVAL_DIV_ZERO, 0, 0),
			ml_value_matrix (
				ml_mat_scale (arena, a.matrix, ml_rat_recip (arena, b.exact))
			)
		);
	}

	else if (a.kind == ML_VALUE_MATRIX || b.kind == ML_VALUE_MATRIX)
	{
		result = matrix_operands (a, b);
	}

	else if (
		   result.kind == ML_VALUE_NONE
		&& a.kind == ML_VALUE_EXACT
		&& b.kind == ML_VALUE_EXACT
	)
	{
		result = ml_value_exact (ml_rat_div (arena, a.exact, b.exact));
	}

	else if (result.kind == ML_VALUE_NONE)
	{
		double divisor = ml_value_to_double (arena, b);

		result = ml_choose (
			divisor == 0.0,
			ml_value_error (ML_EVAL_DIV_ZERO, 0, 0),
			ml_value_approx (ml_value_to_double (arena, a) / divisor)
		);
	}

	return result;
}

/* a^b through doubles, for everything that has no exact answer. */
static MlValue pow_approx (MlArena *arena, MlValue a, MlValue b)
{
	double  x      = ml_value_to_double (arena, a);
	double  y      = ml_value_to_double (arena, b);
	int32_t whole  = y == floor (y);
	MlValue result = ml_value_none ();

	if (x == 0.0 && y == 0.0)
	{
		result = ml_value_error (ML_EVAL_ZERO_POW_ZERO, 0, 0);
	}

	else if (x == 0.0 && y < 0.0)
	{
		result = ml_value_error (ML_EVAL_DIV_ZERO, 0, 0);
	}

	else if (x < 0.0 && !whole)
	{
		/* a negative number to a fraction: the principal complex value */
		result = ml_hyper_pow_negative (arena, a, b);
	}

	else
	{
		result = ml_value_approx (pow (x, y));
	}

	return result;
}

/*
 * An exact base to an exact power: exact when the power is an integer or 1/2.
 */
static MlValue pow_exact (MlArena *arena, MlValue a, MlValue b)
{
	MlI64Result exponent = ml_value_to_i64 (b);
	MlValue     result   = ml_value_none ();
	MlRat       half     = ml_rat_make (
		arena,
		ml_big_from_u64 (arena, 1),
		ml_big_from_u64 (arena, 2)
	);

	if (ml_rat_is_zero (a.exact) && ml_rat_is_zero (b.exact))
	{
		result = ml_value_error (ML_EVAL_ZERO_POW_ZERO, 0, 0);
	}

	else if (ml_rat_is_zero (a.exact) && ml_rat_sign (b.exact) < 0)
	{
		result = ml_value_error (ML_EVAL_DIV_ZERO, 0, 0);
	}

	else if (ml_rat_is_zero (a.exact))
	{
		result = ml_value_exact (a.exact);
	}

	else if (exponent.ok)
	{
		result = ml_value_exact (ml_rat_pow (arena, a.exact, exponent.value));
	}

	else if (ml_rat_is_integer (b.exact))
	{
		/* a whole power that does not fit 64 bits: only 1 and -1 stay small */
		result = ml_value_error (ML_EVAL_TOO_LARGE, 0, 0);
	}

	else if (ml_rat_cmp (arena, b.exact, half) == 0)
	{
		MlRatSqrt root = ml_rat_sqrt_exact (arena, a.exact);

		result = ml_choose (
			root.exact,
			ml_value_exact (root.value),
			ml_choose (
				ml_rat_sign (a.exact) < 0,
				ml_hyper_sqrt_negative (arena, a.exact),
				pow_approx (arena, a, b)
			)
		);
	}

	else
	{
		result = pow_approx (arena, a, b);
	}

	return result;
}

MlValue ml_value_pow (MlArena *arena, MlValue a, MlValue b)
{
	MlValue result = first_error (a, b);

	if (ml_value_is_hyper (a) || ml_value_is_hyper (b))
	{
		result = ml_hyper_apply (arena, ML_HYPER_POW, a, b);
	}

	else if (involves_quantity (a, b))
	{
		result = quantity_op (arena, Q_POW, a, b);
	}

	else if (involves_expr (a, b))
	{
		result = expr_op (arena, SYM_OP_POW, a, b);
	}

	else if (a.kind == ML_VALUE_MATRIX && b.kind == ML_VALUE_EXACT)
	{
		MlI64Result n = ml_value_to_i64 (b);

		result = ml_choose (
			n.ok,
			ml_value_matrix (ml_mat_pow (arena, a.matrix, n.value)),
			ml_value_error (ML_EVAL_NOT_INTEGER, 0, 0)
		);
	}

	else if (a.kind == ML_VALUE_MATRIX || b.kind == ML_VALUE_MATRIX)
	{
		result = matrix_operands (a, b);
	}

	else if (
		   result.kind == ML_VALUE_NONE
		&& a.kind == ML_VALUE_EXACT
		&& b.kind == ML_VALUE_EXACT
	)
	{
		result = pow_exact (arena, a, b);
	}

	else if (result.kind == ML_VALUE_NONE)
	{
		result = pow_approx (arena, a, b);
	}

	return result;
}

/* The text of a value that is not a list. */
static MlResult format_scalar (MlStr *out, MlArena *arena, MlValue v)
{
	MlResult r = ML_ERR_INVALID;

	if (v.kind == ML_VALUE_EXACT)
	{
		r = ml_rat_format (out, v.exact);
	}

	else if (v.kind == ML_VALUE_APPROX)
	{
		/* U+2248 ALMOST EQUAL TO */
		r = ml_str_appendf (
			out,
			"\xE2\x89\x88 %.*g",
			APPROX_DIGITS,
			v.approx
		);
	}

	else if (v.kind == ML_VALUE_QUANTITY && v.show.len > 0)
	{
		r = ml_rat_format (out, ml_rat_div (arena, v.exact, v.scale));

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_slice_cstr (" "));
		}

		if (r == ML_OK)
		{
			r = ml_str_append (out, v.show);
		}
	}

	else if (v.kind == ML_VALUE_QUANTITY)
	{
		MlSlice unit = ml_dim_unit_name (v.dim);

		r = ml_rat_format (out, v.exact);

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_slice_cstr (" "));
		}

		if (r == ML_OK && unit.len > 0)
		{
			r = ml_str_append (out, unit);
		}

		else if (r == ML_OK)
		{
			r = ml_dim_format (out, v.dim);
		}
	}

	else if (v.kind == ML_VALUE_MATRIX)
	{
		r = ml_mat_format (out, v.matrix);
	}

	else if (ml_value_is_hyper (v))
	{
		r = ml_hyper_format (out, arena, v.hyper);
	}

	else if (v.kind == ML_VALUE_EXPR)
	{
		r = ml_sym_format (out, v.sym);
	}

	else if (v.kind == ML_VALUE_PLOT)
	{
		r = ml_plot_summary (out, v.plot);
	}

	else if (v.kind == ML_VALUE_BOOL)
	{
		r = ml_str_append (
			out,
			ml_slice_cstr (ml_choose (v.flag, "true", "false"))
		);
	}

	else if (v.kind == ML_VALUE_FUNC)
	{
		r = ml_str_append (out, ml_slice_cstr ("function"));

		if (r == ML_OK && v.fn_kind != ML_FN_LAMBDA)
		{
			r = ml_str_appendf (out, " %.*s", (int32_t)v.show.len, v.show.data);
		}
	}

	return r;
}

/* How many items of a list are written out before and after the dots. */
#define LIST_HEAD 8
#define LIST_TAIL 2

/*
 * [1, 2, 3]; a long list shows its first eight and its last two items, then
 * the count: [1, 2, 3, 4, 5, 6, 7, 8, ..., 99, 100] (100 items).
 */
static MlResult format_list (MlStr *out, MlArena *arena, const MlListData *l)
{
	MlResult r     = ml_str_append (out, ml_slice_cstr ("["));
	int32_t  split = l->count > LIST_HEAD + LIST_TAIL + 1;

	for (uint64_t i = 0; i < l->count && r == ML_OK; i++)
	{
		int32_t hidden = split && i >= LIST_HEAD && i < l->count - LIST_TAIL;

		if (hidden && i == LIST_HEAD)
		{
			r = ml_str_append (out, ml_slice_cstr ("..., "));
		}

		else if (!hidden)
		{
			r = format_scalar (out, arena, l->items [i]);

			if (r == ML_OK && i + 1 < l->count)
			{
				r = ml_str_append (out, ml_slice_cstr (", "));
			}
		}
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr ("]"));
	}

	if (r == ML_OK && split)
	{
		r = ml_str_appendf (out, " (%llu items)", (unsigned long long)l->count);
	}

	return r;
}

MlResult ml_value_format (MlStr *out, MlArena *arena, MlValue v)
{
	return ml_choose (
		v.kind == ML_VALUE_LIST,
		format_list (out, arena, v.list),
		format_scalar (out, arena, v)
	);
}

/*
 * A range that is a plain name. The range of an error inside a function body
 * is the call, which has a parenthesis; quoting that as a name would be wrong.
 */
static int32_t is_word (MlSlice text)
{
	int32_t word = text.len > 0;

	for (uint64_t i = 0; i < text.len; i++)
	{
		if (text.data [i] == '(')
		{
			word = 0;
		}
	}

	return word;
}

MlResult ml_error_message (MlStr *out, MlValue v, MlSlice source)
{
	MlResult r = ml_str_append (out, ml_slice_cstr (ml_error_text (v.error)));

	if (
		   r == ML_OK
		&& v.error == ML_EVAL_UNDEFINED_NAME
		&& v.error_len > 0
		&& v.error_start + v.error_len <= source.len
		&& is_word (ml_slice_sub (source, v.error_start, v.error_len))
	)
	{
		r = ml_str_append (out, ml_slice_cstr (" '"));

		if (r == ML_OK)
		{
			r = ml_str_append (
				out,
				ml_slice_sub (source, v.error_start, v.error_len)
			);
		}

		if (r == ML_OK)
		{
			r = ml_str_append (out, ml_slice_cstr ("'"));
		}
	}

	return r;
}
