#include <matlog/session/steps.h>

#include <matlog/core/mem.h>
#include <matlog/math/eval.h>

/* What the statement of a line asks for. */
typedef enum StepKind
{
	STEP_NONE = 0,
	STEP_DET,
	STEP_INV,
	STEP_RREF,
	STEP_SOLVE,
	STEP_MUL,
	STEP_SYMBOLIC
} StepKind;

/* The value of the subtree at node, in the environment of the line. */
static MlValue eval_node (
	MlArena         *arena,
	const MlResults *results,
	uint64_t        snapshot,
	MlSlice         source,
	const MlAst     *ast,
	uint64_t        node
)
{
	MlAst view = *ast;

	view.root = node;
	return ml_eval (arena, &results->env, snapshot, source, &view);
}

/* The matrix a subtree evaluates to; ok is 0 when it is anything else. */
typedef struct Operand
{
	int32_t ok;
	MlMat   matrix;
} Operand;

static Operand operand (
	MlArena         *arena,
	const MlResults *results,
	uint64_t        snapshot,
	MlSlice         source,
	const MlAst     *ast,
	uint64_t        node
)
{
	MlValue v   = eval_node (arena, results, snapshot, source, ast, node);
	Operand out =
	{
		.ok     = ml_value_is_matrix (v),
		.matrix = v.matrix
	};

	return out;
}

/* A^-1: a power whose exponent is the exact number -1. */
static int32_t is_minus_one (
	MlArena         *arena,
	const MlResults *results,
	uint64_t        snapshot,
	MlSlice         source,
	const MlAst     *ast,
	uint64_t        node
)
{
	MlValue v = eval_node (arena, results, snapshot, source, ast, node);

	return (
		   ml_value_is_exact (v)
		&& ml_value_cmp (arena, v, ml_value_from_i64 (arena, -1)) == 0
	);
}

/* The kind of steps for the function called in a call node. */
static StepKind call_kind (MlSlice name, uint64_t argc)
{
	StepKind kind = STEP_NONE;

	kind = ml_choose (
		argc == 1 && ml_slice_equal (name, ml_slice_cstr ("det")),
		STEP_DET,
		kind
	);
	kind = ml_choose (
		argc == 1 && ml_slice_equal (name, ml_slice_cstr ("inv")),
		STEP_INV,
		kind
	);
	kind = ml_choose (
		argc == 1
			&& (
				   ml_slice_equal (name, ml_slice_cstr ("rref"))
				|| ml_slice_equal (name, ml_slice_cstr ("rank"))
			),
		STEP_RREF,
		kind
	);
	kind = ml_choose (
		argc == 2 && ml_slice_equal (name, ml_slice_cstr ("solve")),
		STEP_SOLVE,
		kind
	);
	kind = ml_choose (
		   ml_slice_equal (name, ml_slice_cstr ("simplify"))
		|| ml_slice_equal (name, ml_slice_cstr ("expand"))
		|| ml_slice_equal (name, ml_slice_cstr ("diff"))
		|| ml_slice_equal (name, ml_slice_cstr ("integrate"))
		|| ml_slice_equal (name, ml_slice_cstr ("limit")),
		STEP_SYMBOLIC,
		kind
	);
	return kind;
}

/* Runs the explaining version of the operation into the log. */
static void explain (
	MlArena         *arena,
	const MlResults *results,
	uint64_t        snapshot,
	MlSlice         source,
	const MlAst     *ast,
	uint64_t        core,
	MlStepLog       *log
)
{
	MlNode   node = ml_ast_node (ast, core);
	StepKind kind = STEP_NONE;
	uint64_t a    = ml_ast_child (ast, core, 0);
	uint64_t b    = ml_ast_child (ast, core, 1);

	if (node.kind == ML_NODE_CALL)
	{
		kind = call_kind (
			ml_ast_text (ast, source, a),
			ml_ast_child_count (ast, core) - 1
		);
		a    = ml_ast_child (ast, core, 1);
		b    = ml_ast_child (ast, core, 2);
	}

	else if (node.kind == ML_NODE_POW)
	{
		kind = ml_choose (
			is_minus_one (arena, results, snapshot, source, ast, b),
			STEP_INV,
			STEP_NONE
		);
	}

	else if (node.kind == ML_NODE_MUL)
	{
		kind = STEP_MUL;
	}

	else if (node.kind == ML_NODE_TO)
	{
		kind = STEP_SYMBOLIC;
	}

	if (kind == STEP_SYMBOLIC)
	{
		/* the evaluator explains symbolic calls and conversions itself */
		MlAst view = *ast;

		view.root = core;
		ml_eval_steps (
			arena,
			&results->env,
			snapshot,
			source,
			&view,
			log
		);
	}

	else if (kind != STEP_NONE)
	{
		Operand first  = operand (arena, results, snapshot, source, ast, a);
		Operand second = operand (arena, results, snapshot, source, ast, b);

		if (!first.ok)
		{
			kind = STEP_NONE;
		}

		else if ((kind == STEP_SOLVE || kind == STEP_MUL) && !second.ok)
		{
			kind = STEP_NONE;
		}

		else if (kind == STEP_DET)
		{
			ml_mat_det_steps (arena, first.matrix, log);
		}

		else if (kind == STEP_INV)
		{
			ml_mat_inverse_steps (arena, first.matrix, log);
		}

		else if (kind == STEP_RREF)
		{
			ml_mat_rref_steps (arena, first.matrix, log);
		}

		else if (kind == STEP_SOLVE)
		{
			ml_mat_solve_steps (arena, first.matrix, second.matrix, log);
		}

		else
		{
			ml_mat_mul_steps (arena, first.matrix, second.matrix, log);
		}
	}
}

MlLineSteps ml_line_steps (
	MlStr           *out,
	const MlResults *results,
	const MlSession *s,
	uint64_t        idx
)
{
	MlLineSteps result =
	{
		.result = ML_OK,
		.found  = 0
	};
	MlLineView  view   = ml_session_get (s, idx);

	if (
		   view.kind == ML_LINE_MATH
		&& view.domain == ML_DOMAIN_ALGEBRA
		&& idx < results->lines.len
	)
	{
		MlParseResult p = ml_parse (view.text);

		if (p.error.code == ML_PARSE_OK && p.ast.root != ML_NODE_NIL_INDEX)
		{
			MlArena   arena;
			MlStepLog log;
			uint64_t  core = ml_choose (
				ml_ast_node (&p.ast, p.ast.root).kind == ML_NODE_ASSIGN,
				ml_ast_child (&p.ast, p.ast.root, 1),
				p.ast.root
			);

			ml_arena_init (&arena, 0);
			ml_steplog_init (&log);
			explain (
				&arena,
				results,
				results->lines.data [idx].snapshot,
				view.text,
				&p.ast,
				core,
				&log
			);
			ml_steplog_trim (&log);
			result.result = log.status;

			if (result.result == ML_OK && log.text.len > 0)
			{
				result.result = ml_str_append (out, ml_str_view (log.text));
				result.found  = result.result == ML_OK;
			}

			ml_steplog_free (&log);
			ml_arena_free (&arena);
		}

		ml_ast_free (&p.ast);
	}

	return result;
}
