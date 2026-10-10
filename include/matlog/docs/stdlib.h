#ifndef MATLOG_DOCS_STDLIB_H
#define MATLOG_DOCS_STDLIB_H

#include <matlog/docs/docs.h>
#include <matlog/math/env.h>

/*
 * The standard library (plan section 4.18): functions written in matlog's own
 * language, in stdlib/*.mlog, compiled into the program (source/matlog/docs/
 * stdlib_data.c, made by tools/gen_docs.py) and defined in the environment
 * before the first line of every session. A file is a session file: notes
 * and lines of the algebra domain. By convention a function is described by
 * the note above it, `# name (a, b): what it gives`, which the tests compare
 * with the definitions.
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

#endif /* MATLOG_DOCS_STDLIB_H */
