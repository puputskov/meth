#ifndef MATLOG_MATH_ENV_H
#define MATLOG_MATH_ENV_H

#include <matlog/math/value.h>
#include <matlog/parse/parser.h>

/*
 * The environment: the variables and functions that lines have defined, as an
 * append-only list of bindings. Nothing is ever changed or removed, a
 * redefinition is just a newer binding that hides the older ones with the same
 * name. That makes a snapshot free: it is the length of the list at some
 * moment, and a lookup that is given a snapshot ignores every binding added
 * after it. So any earlier line can be evaluated again in the environment it
 * saw, which the step by step output needs.
 *
 * The environment owns its memory: a variable's value is copied into its own
 * arena when it is defined, and so are the source text and the tree of a
 * function, so the caller may reset its arena and free its tree right after.
 * A binding handed out is valid until ml_env_free.
 */

typedef enum MlBindKind
{
	ML_BIND_NONE = 0, /* the answer of a lookup that found nothing */
	ML_BIND_VAR,
	ML_BIND_FUNC
} MlBindKind;

typedef struct MlBinding
{
	MlBindKind kind;
	MlSlice    name;
	MlValue    value;  /* ML_BIND_VAR */
	/* ML_BIND_FUNC: the definition, with its own copy of text and tree */
	MlSlice    source;
	MlAst      ast;
	uint64_t   params; /* index of the params node */
	uint64_t   body;   /* index of the body expression */
} MlBinding;

ML_VEC_DEFINE (MlBindingVec, MlBinding)

typedef struct MlEnv
{
	MlArena      arena;
	MlBindingVec bindings;
	/*
	 * 1: a name that stands for nothing is a symbol, so x + y is the
	 * expression x + y instead of an error (see :symbols). It is a property
	 * of the evaluation, not of a binding, and survives ml_env_truncate.
	 */
	int32_t      free_symbols;
	/*
	 * The bindings below this index are the standard library (see
	 * docs/stdlib.h): they are not listed as the user's names. 0 for an
	 * environment that has no library.
	 */
	uint64_t     library;
} MlEnv;

void ml_env_init (MlEnv *env);
void ml_env_free (MlEnv *env);

/* The snapshot of the environment as it is now. */
uint64_t ml_env_snapshot (const MlEnv *env);

/*
 * Drops every binding added after the snapshot, so the environment is as it
 * was then. The numbers of dropped variables stay in the arena until
 * ml_env_free, so a caller that starts over should free and init instead.
 * A snapshot that is not older than the environment changes nothing.
 */
void ml_env_truncate (MlEnv *env, uint64_t snapshot);

/*
 * Adds a variable. The value must be a number, a matrix or an expression
 * (not an error, not none); its digits and nodes are copied. ML_ERR_INVALID
 * otherwise.
 */
MlResult ml_env_define_var (MlEnv *env, MlSlice name, MlValue value);

/*
 * Adds the function of a parsed definition (the root of ast must be an
 * ML_NODE_FUNC_DEF of source). ML_ERR_INVALID for any other statement.
 */
MlResult ml_env_define_func (MlEnv *env, MlSlice source, const MlAst *ast);

/*
 * The newest binding called name among the first `snapshot` ones (pass the
 * result of ml_env_snapshot for "everything"). The kind is ML_BIND_NONE when
 * there is none.
 */
MlBinding ml_env_lookup (const MlEnv *env, MlSlice name, uint64_t snapshot);

/* The number of parameters of a function binding (0 for anything else). */
uint64_t ml_binding_param_count (MlBinding b);

/* The name of the n-th parameter of a function, empty if there is none. */
MlSlice ml_binding_param (MlBinding b, uint64_t n);

#endif /* MATLOG_MATH_ENV_H */
