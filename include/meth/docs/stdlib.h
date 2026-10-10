#ifndef METH_DOCS_STDLIB_H
#define METH_DOCS_STDLIB_H

#include <meth/docs/docs.h>
#include <meth/math/env.h>

/*
 * The standard library (plan section 4.18): functions written in meth's own
 * language, in stdlib/*.mlog, compiled into the program (source/meth/docs/
 * stdlib_data.c, made by tools/gen_docs.py) and defined in the environment
 * before the first line of every session. A file is a session file: notes
 * and lines of any domain (`m:` lines define, `a:` lines assert). By
 * convention a function is described by the note above it,
 * `# name (a, b): what it gives`, which the tests compare with the
 * definitions.
 *
 * What the library defines is hidden from the watch window and from the
 * variable listing, and the user's own definitions of the same names win, as
 * every later definition does. Names that start with an underscore are the
 * helpers of the library: they work but are not offered by the completion.
 */

extern const MlDocSource ml_stdlib_sources [];
extern const uint64_t    ml_stdlib_source_count;

/*
 * Defines the library in an environment that has nothing else yet and sets
 * env->library to the number of its bindings. ML_ERR_PARSE when a line of the
 * library does not parse (a mistake of the program, found by the tests).
 */
MlResult ml_stdlib_load (MlEnv *env);

/*
 * Runs the asserts of the library. A library file may hold, next to the
 * definitions `m:<domain> <math>`, lines `a:<domain> <math>` that test them:
 * the text the line gives must be `true`, or the text after ` => ` in the
 * line (`a:algebra fib(10) => 55`). Each assert is evaluated in a session
 * that has the library, in its own domain. A line that gives something else
 * is written to report and counted in problems.
 */
MlResult ml_stdlib_check (MlStr *report, uint64_t *problems);

#endif /* METH_DOCS_STDLIB_H */
