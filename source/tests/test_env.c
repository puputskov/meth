#include "test.h"

#include <matlog/math/env.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static int32_t is_int (MlArena *a, MlValue v, int64_t n)
{
	return (
		   ml_value_is_exact (v)
		&& ml_value_cmp (a, v, ml_value_from_i64 (a, n)) == 0
	);
}

static void test_variables (void)
{
	MlEnv    env;
	MlArena  tmp;
	uint64_t all;

	ml_env_init (&env);
	ml_arena_init (&tmp, 0);

	CHECK (ml_env_snapshot (&env) == 0);
	CHECK (ml_env_lookup (&env, lit ("a"), 0).kind == ML_BIND_NONE);

	CHECK (
		ml_env_define_var (&env, lit ("a"), ml_value_from_i64 (&tmp, 3))
		== ML_OK
	);
	CHECK (
		ml_env_define_var (&env, lit ("b"), ml_value_approx (1.5)) == ML_OK
	);
	all = ml_env_snapshot (&env);
	CHECK (all == 2);

	{
		MlBinding a = ml_env_lookup (&env, lit ("a"), all);
		MlBinding b = ml_env_lookup (&env, lit ("b"), all);

		CHECK (a.kind == ML_BIND_VAR);
		CHECK (is_int (&tmp, a.value, 3));
		CHECK (ml_slice_equal (a.name, lit ("a")));
		CHECK (b.kind == ML_BIND_VAR);
		CHECK (b.value.kind == ML_VALUE_APPROX);
		CHECK (b.value.approx == 1.5);
	}

	CHECK (ml_env_lookup (&env, lit ("c"), all).kind == ML_BIND_NONE);
	CHECK (ml_env_lookup (&env, lit ("ab"), all).kind == ML_BIND_NONE);
	CHECK (ml_env_lookup (&env, lit (""), all).kind == ML_BIND_NONE);

	/* only numbers can be bound */
	CHECK (
		ml_env_define_var (&env, lit ("e1"), ml_value_none ())
		== ML_ERR_INVALID
	);
	CHECK (
		ml_env_define_var (
			&env,
			lit ("e2"),
			ml_value_error (ML_EVAL_DIV_ZERO, 0, 0)
		) == ML_ERR_INVALID
	);
	CHECK (ml_env_snapshot (&env) == all);

	ml_arena_free (&tmp);
	ml_env_free (&env);
}

static void test_shadowing_and_snapshots (void)
{
	MlEnv    env;
	MlArena  tmp;
	uint64_t s0;
	uint64_t s1;
	uint64_t s2;

	ml_env_init (&env);
	ml_arena_init (&tmp, 0);

	s0 = ml_env_snapshot (&env);
	ml_env_define_var (&env, lit ("x"), ml_value_from_i64 (&tmp, 1));
	s1 = ml_env_snapshot (&env);
	ml_env_define_var (&env, lit ("x"), ml_value_from_i64 (&tmp, 2));
	s2 = ml_env_snapshot (&env);
	ml_env_define_var (&env, lit ("x"), ml_value_from_i64 (&tmp, 3));

	CHECK (s0 == 0 && s1 == 1 && s2 == 2);

	/* the newest binding wins, older ones are still there for old lines */
	CHECK (is_int (&tmp, ml_env_lookup (&env, lit ("x"), 3).value, 3));
	CHECK (is_int (&tmp, ml_env_lookup (&env, lit ("x"), s2).value, 2));
	CHECK (is_int (&tmp, ml_env_lookup (&env, lit ("x"), s1).value, 1));
	CHECK (ml_env_lookup (&env, lit ("x"), s0).kind == ML_BIND_NONE);

	/* a snapshot past the end means everything */
	CHECK (is_int (&tmp, ml_env_lookup (&env, lit ("x"), 99).value, 3));

	ml_arena_free (&tmp);
	ml_env_free (&env);
}

static void test_values_are_copied (void)
{
	MlEnv   env;
	MlArena tmp;
	MlValue big;

	ml_env_init (&env);
	ml_arena_init (&tmp, 0);

	big = ml_value_pow (
		&tmp,
		ml_value_from_i64 (&tmp, 2),
		ml_value_from_i64 (&tmp, 200)
	);
	CHECK (ml_value_is_exact (big));
	CHECK (ml_env_define_var (&env, lit ("big"), big) == ML_OK);

	/* the caller's memory goes away, the binding stays good */
	ml_arena_free (&tmp);
	ml_arena_init (&tmp, 0);

	{
		MlBinding b   = ml_env_lookup (&env, lit ("big"), 1);
		MlValue   two = ml_value_pow (
			&tmp,
			ml_value_from_i64 (&tmp, 2),
			ml_value_from_i64 (&tmp, 200)
		);

		CHECK (b.kind == ML_BIND_VAR);
		CHECK (ml_value_cmp (&tmp, b.value, two) == 0);
	}

	ml_arena_free (&tmp);
	ml_env_free (&env);
}

static void test_matrix_values (void)
{
	MlEnv   env;
	MlArena tmp;
	MlMat   m;
	MlStr   text;

	ml_env_init (&env);
	ml_arena_init (&tmp, 0);
	ml_str_init (&text);

	m = ml_mat_zero (&tmp, 2, 2);
	ml_mat_set (&m, 0, 0, ml_rat_from_i64 (&tmp, 1));
	ml_mat_set (&m, 0, 1, ml_rat_from_i64 (&tmp, 2));
	ml_mat_set (&m, 1, 0, ml_rat_from_i64 (&tmp, 3));
	ml_mat_set (&m, 1, 1, ml_rat_from_i64 (&tmp, 4));
	CHECK (ml_env_define_var (&env, lit ("A"), ml_value_matrix (m)) == ML_OK);

	/* the caller's memory goes away, the binding stays good */
	ml_arena_free (&tmp);
	ml_arena_init (&tmp, 0);

	{
		MlBinding b = ml_env_lookup (&env, lit ("A"), 1);

		CHECK (b.kind == ML_BIND_VAR && ml_value_is_matrix (b.value));
		CHECK (ml_value_format (&text, &tmp, b.value) == ML_OK);
		CHECK (strcmp (ml_str_cstr (text), "[[1, 2], [3, 4]]") == 0);
	}

	/* a failed matrix is an error value and cannot be bound */
	CHECK (
		ml_env_define_var (
			&env,
			lit ("B"),
			ml_value_matrix (ml_mat_error (ML_ERR_INVALID))
		) == ML_ERR_INVALID
	);
	CHECK (ml_env_snapshot (&env) == 1);

	ml_str_free (&text);
	ml_arena_free (&tmp);
	ml_env_free (&env);
}

static void test_functions (void)
{
	MlEnv         env;
	MlSlice       src = lit ("f(x, y) = 2x + y");
	MlParseResult p   = ml_parse (src);

	ml_env_init (&env);
	CHECK (p.error.code == ML_PARSE_OK);
	CHECK (ml_env_define_func (&env, src, &p.ast) == ML_OK);

	/* the definition does not depend on the caller's tree */
	ml_ast_free (&p.ast);

	{
		MlBinding f = ml_env_lookup (&env, lit ("f"), 1);
		MlStr     s;

		ml_str_init (&s);
		CHECK (f.kind == ML_BIND_FUNC);
		CHECK (ml_slice_equal (f.name, lit ("f")));
		CHECK (ml_binding_param_count (f) == 2);
		CHECK (ml_slice_equal (ml_binding_param (f, 0), lit ("x")));
		CHECK (ml_slice_equal (ml_binding_param (f, 1), lit ("y")));
		CHECK (ml_binding_param (f, 2).len == 0);
		CHECK (ml_ast_node (&f.ast, f.body).kind == ML_NODE_ADD);
		CHECK (
			ml_slice_equal (
				ml_ast_text (&f.ast, f.source, f.body),
				lit ("2x + y")
			)
		);
		CHECK (ml_ast_dump (&s, f.source, &f.ast) == ML_OK);
		CHECK (
			ml_slice_equal (
				ml_str_view (s),
				lit ("(def f (x y) (+ (* 2 x) y))")
			)
		);
		ml_str_free (&s);
	}

	/* a function and a variable share one namespace, the newer one wins */
	{
		MlArena tmp;

		ml_arena_init (&tmp, 0);
		CHECK (
			ml_env_define_var (&env, lit ("f"), ml_value_from_i64 (&tmp, 7))
			== ML_OK
		);
		CHECK (ml_env_lookup (&env, lit ("f"), 2).kind == ML_BIND_VAR);
		CHECK (ml_env_lookup (&env, lit ("f"), 1).kind == ML_BIND_FUNC);
		ml_arena_free (&tmp);
	}

	{
		MlBinding v = ml_env_lookup (&env, lit ("f"), 2);

		CHECK (ml_binding_param_count (v) == 0);
		CHECK (ml_binding_param (v, 0).len == 0);
	}

	ml_env_free (&env);
}

static void test_function_without_parameters (void)
{
	MlEnv         env;
	MlSlice       src = lit ("k() = 5");
	MlParseResult p   = ml_parse (src);

	ml_env_init (&env);
	CHECK (p.error.code == ML_PARSE_OK);
	CHECK (ml_env_define_func (&env, src, &p.ast) == ML_OK);
	ml_ast_free (&p.ast);

	{
		MlBinding k = ml_env_lookup (&env, lit ("k"), 1);

		CHECK (k.kind == ML_BIND_FUNC);
		CHECK (ml_binding_param_count (k) == 0);
		CHECK (ml_ast_node (&k.ast, k.body).kind == ML_NODE_NUMBER);
	}

	ml_env_free (&env);
}

static void test_define_func_rejects_other_statements (void)
{
	MlEnv         env;
	MlSlice       src = lit ("a = 3");
	MlParseResult p   = ml_parse (src);

	ml_env_init (&env);
	CHECK (p.error.code == ML_PARSE_OK);
	CHECK (ml_env_define_func (&env, src, &p.ast) == ML_ERR_INVALID);
	CHECK (ml_env_snapshot (&env) == 0);
	ml_ast_free (&p.ast);

	{
		MlAst empty;

		MlNodeVec_init (&empty.nodes);
		empty.root = 0;
		CHECK (
			ml_env_define_func (&env, lit (""), &empty) == ML_ERR_INVALID
		);
		MlNodeVec_free (&empty.nodes);
	}

	ml_env_free (&env);
}

static void test_truncate (void)
{
	MlEnv env;

	ml_env_init (&env);
	CHECK (ml_env_define_var (&env, lit ("a"), ml_value_approx (1.0)) == ML_OK);
	CHECK (ml_env_define_var (&env, lit ("b"), ml_value_approx (2.0)) == ML_OK);
	CHECK (ml_env_define_var (&env, lit ("a"), ml_value_approx (3.0)) == ML_OK);

	/* a snapshot that is not older changes nothing */
	ml_env_truncate (&env, 3);
	ml_env_truncate (&env, 10);
	CHECK (ml_env_snapshot (&env) == 3);

	ml_env_truncate (&env, 1);
	CHECK (ml_env_snapshot (&env) == 1);
	CHECK (ml_env_lookup (&env, lit ("a"), 1).kind == ML_BIND_VAR);
	CHECK (ml_env_lookup (&env, lit ("b"), 10).kind == ML_BIND_NONE);

	/* new bindings go after the kept ones */
	CHECK (ml_env_define_var (&env, lit ("c"), ml_value_approx (4.0)) == ML_OK);
	CHECK (ml_env_snapshot (&env) == 2);

	ml_env_truncate (&env, 0);
	CHECK (ml_env_snapshot (&env) == 0);
	ml_env_free (&env);
}

void run_env_tests (void)
{
	test_variables ();
	test_shadowing_and_snapshots ();
	test_values_are_copied ();
	test_matrix_values ();
	test_functions ();
	test_function_without_parameters ();
	test_define_func_rejects_other_statements ();
	test_truncate ();
}
