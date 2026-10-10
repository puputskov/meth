#ifndef METH_SESSION_FILE_H
#define METH_SESSION_FILE_H

#include <meth/session/session.h>

/*
 * The .meth session file format: UTF-8 text, one entry per line.
 *
 *	# some free text            note
 *	m:algebra A = [[1,2],[3,4]] math line in the given domain
 *	include "constants.meth"    include another session
 *	(empty line)                blank
 *
 * Anything else, including a line that is not valid UTF-8, is kept as an
 * ML_LINE_UNKNOWN line holding the raw text, so loading and saving never
 * loses data. Lines end with LF or CRLF when reading and with LF when writing
 * (a carriage return left at the end of a line is dropped, so that reading
 * what was written gives the same lines).
 * Leading UTF-8 byte order marks are ignored.
 */

/* Appends the lines found in text to s. */
MlResult ml_file_parse (MlSession *s, MlSlice text);

/* Appends the file form of the session to out. */
MlResult ml_file_format (MlStr *out, const MlSession *s);

/*
 * Replaces the content of s with the file at path and remembers the path.
 * The cursor goes to the first line and the domain to that of the last math
 * line. s is left untouched when loading fails (ML_ERR_IO for a missing file).
 */
MlResult ml_file_load (MlSession *s, MlSlice path);

/*
 * Writes the session to its path (atomically) and clears the dirty flag.
 * ML_ERR_INVALID without a path.
 */
MlResult ml_file_save (MlSession *s);

#endif /* METH_SESSION_FILE_H */
