#include <matlog/session/steps.h>
#include <matlog/parse/words.h>

#include <matlog/core/mem.h>
#include <matlog/logic/bool.h>
#include <matlog/logic/lambda.h>
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
	Operand out = {
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
		argc == 1 && ML_WORD_IS (name, DET),
		STEP_DET,
		kind
	);
	kind = ml_choose (
		argc == 1 && ML_WORD_IS (name, INV),
		STEP_INV,
		kind
	);
	kind = ml_choose (
		argc == 1
			&& (
				   ML_WORD_IS (name, RREF)
				|| ML_WORD_IS (name, RANK)
			),
		STEP_RREF,
		kind
	);
	kind = ml_choose (
		argc == 2 && ML_WORD_IS (name, SOLVE),
		STEP_SOLVE,
		kind
	);
	kind = ml_choose (
		   ML_WORD_IS (name, SIMPLIFY)
		|| ML_WORD_IS (name, EXPAND)
		|| ML_WORD_IS (name, DIFF)
		|| ML_WORD_IS (name, INTEGRATE)
		|| ML_WORD_IS (name, LIMIT),
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

/* 1 when text starts with the word, then ends or goes on with a non-name. */
static int32_t starts_with_word (MlSlice text, const char *word)
{
	MlSlice  w     = ml_slice_cstr (word);
	uint64_t first = 0;
	int32_t  found = 0;

	while (
		   first < text.len
		&& (text.data [first] == ' ' || text.data [first] == '\t')
	)
	{
		first++;
	}

	if (text.len - first >= w.len)
	{
		found = ml_slice_equal (ml_slice_sub (text, first, w.len), w);

		if (found && text.len - first > w.len)
		{
			uint8_t c = (uint8_t)text.data [first + w.len];

			found = !(
				   (c >= 'a' && c <= 'z')
				|| (c >= 'A' && c <= 'Z')
				|| (c >= '0' && c <= '9')
				|| c == '_'
				|| c == '\''
				|| c >= 0x80
			);
		}
	}

	return found;
}

/*
 * The steps of a line of the boolean or the lambda domain: the rows the
 * engine gives with `table` (the truth table) or `trace` (one beta step per
 * row) before the line, or after it when the line already has that word.
 */
static MlLineSteps logic_steps (
	MlStr           *out,
	const MlResults *results,
	MlLineView      view,
	uint64_t        idx
)
{
	MlLineSteps st = {
		.result = ML_OK,
		.found  = 0
	};

	int32_t    is_bool = view.domain == ML_DOMAIN_BOOLEAN;
	const char *word   = ml_choose (is_bool, "table", "trace");
	uint64_t   count   = results->lines.data [idx].logic;
	MlLogicOut o;
	MlStr      text;

	ml_logic_out_init (&o);
	ml_str_init (&text);

	if (!starts_with_word (view.text, word))
	{
		st.result = ml_str_append (&text, ml_slice_cstr (word));

		if (st.result == ML_OK)
		{
			st.result = ml_str_append (&text, ml_slice (" ", 1));
		}
	}

	if (st.result == ML_OK)
	{
		st.result = ml_str_append (&text, view.text);
	}

	if (st.result == ML_OK)
	{
		st.result = ml_choose (
			is_bool,
			ml_bool_run (&results->defs, count, ml_str_view (text), &o),
			ml_lambda_run (&results->defs, count, ml_str_view (text), &o)
		);
	}

	if (st.result == ML_OK && o.error == ML_LOGIC_OK && o.block.len > 0)
	{
		st.result = ml_str_append (out, ml_str_view (o.block));
		st.found  = st.result == ML_OK;
	}

	ml_str_free (&text);
	ml_logic_out_free (&o);
	return st;
}

MlLineSteps ml_line_steps (
	MlStr           *out,
	const MlResults *results,
	const MlSession *s,
	uint64_t        idx
)
{
	MlLineSteps result = {
		.result = ML_OK,
		.found  = 0
	};

	MlLineView  view   = ml_session_get (s, idx);

	if (
		   view.kind == ML_LINE_MATH
		&& (
			   view.domain == ML_DOMAIN_ALGEBRA
			|| view.domain == ML_DOMAIN_PHYSICS
		)
		&& idx < results->lines.len
	)
	{
		MlParseResult p = ml_parse_units (
			view.text,
			view.domain == ML_DOMAIN_PHYSICS
		);

		if (p.error.code == ML_PARSE_OK && p.ast.root != ML_NODE_NIL_INDEX)
		{
			MlArena     *arena = ml_arena_app ();
			MlArenaMark mark  = ml_arena_mark (arena);
			MlStepLog   log;
			uint64_t  core = ml_choose (
				ml_ast_node (&p.ast, p.ast.root).kind == ML_NODE_ASSIGN,
				ml_ast_child (&p.ast, p.ast.root, 1),
				p.ast.root
			);

			ml_steplog_init (&log);
			explain (
				arena,
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
			ml_arena_rewind (arena, mark);
		}

		ml_ast_free (&p.ast);
	}

	else if (
		   view.kind == ML_LINE_MATH
		&& (
			   view.domain == ML_DOMAIN_BOOLEAN
			|| view.domain == ML_DOMAIN_LAMBDA
		)
		&& idx < results->lines.len
	)
	{
		result = logic_steps (out, results, view, idx);
	}

	return result;
}
