#ifndef METH_CLI_CLI_H
#define METH_CLI_CLI_H

#include <meth/core/slice.h>
#include <meth/core/str.h>
#include <meth/core/vec.h>

/*
 * The command line interface for scripts and AI agents: commands that read,
 * evaluate and print without a screen. The function here does no I/O of its
 * own besides reading session files: it gets the arguments and the text of
 * standard input, and appends what goes to standard output and standard error
 * to two strings, so the whole interface is testable and `source/app/main.c`
 * only connects it to the process.
 *
 *	meth eval [--session F] [--write] [--format text|json] EXPR...
 *	meth run FILE [--format text|json]
 *	meth lex TEXT [--format text|json]
 *	meth parse TEXT [--format text|json]
 *	meth help [COMMAND]    meth syntax    meth schema
 *	meth commands [--format text|json]     meth version
 *
 * An argument `-` takes lines from standard input (eval: one expression per
 * line; lex and parse: all of the text). Exit codes:
 *
 *	0  success
 *	1  an expression or a line was an error (the result is still printed)
 *	2  usage error (unknown command or option, missing argument)
 *	3  a file could not be read or written
 */

#define ML_EXIT_OK    0
#define ML_EXIT_ERROR 1
#define ML_EXIT_USAGE 2
#define ML_EXIT_IO    3

ML_VEC_DEFINE (MlSliceVec, MlSlice)

/* 1 when the first argument names a command of this interface. */
int32_t ml_cli_is_command (MlSlice first);

/* 1 when `-` is among the arguments, so standard input is needed. */
int32_t ml_cli_needs_stdin (const MlSliceVec *args);

/*
 * Runs the command in args (the program name is not part of it). Returns the
 * exit code.
 */
int32_t ml_cli_run (
	const MlSliceVec *args,
	MlSlice          stdin_text,
	MlStr            *out,
	MlStr            *err
);

#endif /* METH_CLI_CLI_H */
