#ifndef MATLOG_SESSION_SESSION_H
#define MATLOG_SESSION_SESSION_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>
#include <matlog/core/vec.h>

/*
 * A session is an ordered list of lines (math, notes, includes, blanks) plus
 * the cursor line and the current domain. It owns the text of every line;
 * reading a line returns a view that is valid until the next mutation.
 */

typedef enum MlDomain
{
	ML_DOMAIN_ALGEBRA = 0,
	ML_DOMAIN_EQUATIONS,
	ML_DOMAIN_MATRIX,
	ML_DOMAIN_VECTOR,
	ML_DOMAIN_LAMBDA,
	ML_DOMAIN_PGA2,
	ML_DOMAIN_PGA3,
	ML_DOMAIN_BOOLEAN,
	ML_DOMAIN_COUNT
} MlDomain;

typedef enum MlLineKind
{
	ML_LINE_NONE = 0, /* no such line (index out of range) */
	ML_LINE_MATH,     /* an expression in a domain */
	ML_LINE_NOTE,     /* free text */
	ML_LINE_INCLUDE,  /* text is the included file's path */
	ML_LINE_BLANK,
	ML_LINE_UNKNOWN   /* unparsable line from a file, text is the raw line */
} MlLineKind;

typedef struct MlLine
{
	MlLineKind kind;
	MlDomain   domain; /* meaningful for ML_LINE_MATH only */
	MlStr      text;
} MlLine;

ML_VEC_DEFINE (MlLineVec, MlLine)

typedef struct MlLineView
{
	MlLineKind kind;
	MlDomain   domain;
	MlSlice    text;
} MlLineView;

typedef struct MlDomainResult
{
	MlResult result;
	MlDomain domain;
} MlDomainResult;

typedef struct MlSession
{
	MlLineVec lines;
	MlStr     path;   /* empty for a scratch session */
	uint64_t  cursor; /* index of the current line */
	MlDomain  domain; /* domain new math lines are written in */
	int32_t   dirty;  /* changed since the last save */
	/* counts every change of the lines, also a load; never goes back */
	uint64_t  revision;
} MlSession;

/* "algebra", "matrix", ... ("unknown" for an invalid value). */
const char *ml_domain_name (MlDomain domain);

/*
 * Parses a domain name as used in the file format. ML_ERR_PARSE when unknown.
 */
MlDomainResult ml_domain_from_name (MlSlice name);

void ml_session_init (MlSession *s);
void ml_session_free (MlSession *s);

MlResult ml_session_set_path (MlSession *s, MlSlice path);

/* Reads line idx; kind is ML_LINE_NONE when idx is out of range. */
MlLineView ml_session_get (const MlSession *s, uint64_t idx);

/*
 * Copies text into a new line before idx (idx == lines.len appends). Marks the
 * session dirty.
 */
MlResult ml_session_insert (
	MlSession  *s,
	uint64_t   idx,
	MlLineKind kind,
	MlDomain   domain,
	MlSlice    text
);

MlResult ml_session_append (
	MlSession  *s,
	MlLineKind kind,
	MlDomain   domain,
	MlSlice    text
);

/* Replaces line idx. On failure the old line is left untouched. */
MlResult ml_session_replace (
	MlSession  *s,
	uint64_t   idx,
	MlLineKind kind,
	MlDomain   domain,
	MlSlice    text
);

/* Removes line idx and keeps the cursor on a valid line. */
MlResult ml_session_delete (MlSession *s, uint64_t idx);

/*
 * Moves the cursor to idx, clamped to the last line (0 when the session is
 * empty).
 */
void ml_session_set_cursor (MlSession *s, uint64_t idx);

/* Moves the cursor by delta lines, clamped to the first and last line. */
void ml_session_move_cursor (MlSession *s, int64_t delta);

void ml_session_set_domain (MlSession *s, MlDomain domain);

#endif /* MATLOG_SESSION_SESSION_H */
