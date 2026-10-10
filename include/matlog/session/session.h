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
	/* the block that starts with this line is shown as one row; not saved */
	int32_t    folded;
} MlLine;

ML_VEC_DEFINE (MlLineVec, MlLine)

/*
 * Blocks, as in a notebook. The lines of a session are read as a sequence of
 * blocks, nothing of it is stored in the file: a run of math lines in one
 * domain is a code block, a run of notes is a text block, a run of include
 * lines or of unreadable lines is a block of its own, and every blank line
 * is a block by itself, so a blank line ends the block before it.
 */
typedef enum MlBlockKind
{
	ML_BLOCK_NONE = 0, /* no such line */
	ML_BLOCK_CODE,
	ML_BLOCK_TEXT,
	ML_BLOCK_INCLUDE,
	ML_BLOCK_BLANK,
	ML_BLOCK_UNKNOWN
} MlBlockKind;

typedef struct MlBlock
{
	MlBlockKind kind;
	MlDomain    domain; /* ML_BLOCK_CODE */
	uint64_t    first;  /* the lines first..last, inclusive */
	uint64_t    last;
	int32_t     folded; /* drawn as one row */
} MlBlock;

/* 1 when a line b can follow a line a inside one block. */
int32_t ml_block_continues (
	MlLineKind a_kind,
	MlDomain   a_domain,
	MlLineKind b_kind,
	MlDomain   b_domain
);

/*
 * The history of changes, for undo and redo. Every change of the lines
 * records the operation that takes it back; undoing applies such an
 * operation and records the operation that takes the undo back, on the redo
 * stack, so the two stacks are mirrors. The operations move the text of a
 * line instead of copying it, so the history costs one small record per
 * change. Records of one user action share a group, and undo takes back a
 * whole group. A new change empties the redo stack.
 */
typedef enum MlUndoKind
{
	ML_UNDO_INSERT = 0, /* put line at idx */
	ML_UNDO_DELETE,     /* remove the line at idx */
	ML_UNDO_REPLACE,    /* put line at idx, in place of what is there */
	ML_UNDO_MOVE        /* the lines idx..last now start at dest */
} MlUndoKind;

typedef struct MlUndoOp
{
	MlUndoKind kind;
	uint64_t   idx;
	uint64_t   last;
	uint64_t   dest;
	MlLine     line;  /* INSERT, REPLACE: owned until it is applied */
	uint64_t   group;
} MlUndoOp;

ML_VEC_DEFINE (MlUndoVec, MlUndoOp)

/* The most records kept; the oldest groups go first. */
#define ML_UNDO_MAX 10000

typedef struct MlLineView
{
	MlLineKind kind;
	MlDomain   domain;
	MlSlice    text;
	int32_t    folded;
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
	MlUndoVec undo;   /* operations that take changes back */
	MlUndoVec redo;   /* operations that take undos back */
	uint64_t  group;  /* the group new records belong to */
	/* counts every change of the lines, also a load; never goes back */
	uint64_t  revision;
} MlSession;

/* "algebra", "lambda", ... ("unknown" for an invalid value). */
const char *ml_domain_name (MlDomain domain);

/*
 * Parses a domain name as used in the file format. ML_ERR_PARSE when unknown.
 * Matrices, vectors and equations are part of the algebra domain; the names
 * matrix, vector and equations of older files read as algebra.
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

/*
 * The block that line idx belongs to (kind ML_BLOCK_NONE when idx is out of
 * range). The block is folded when its first line is.
 */
MlBlock ml_session_block (const MlSession *s, uint64_t idx);

/*
 * Folds or unfolds the block that starts with line idx. A fold is a matter of
 * the view: it is not a change (no undo, not dirty) and not saved.
 */
void ml_session_set_folded (MlSession *s, uint64_t idx, int32_t folded);

/*
 * Moves the lines first..last (inclusive) so that they start at index dest
 * (counted after they are taken out, 0 to len - count). ML_ERR_RANGE for
 * indices that do not fit. The cursor is not changed.
 */
MlResult ml_session_move (
	MlSession *s,
	uint64_t  first,
	uint64_t  last,
	uint64_t  dest
);

/*
 * Starts a new undo group: the changes made from now on are taken back
 * together by one ml_session_undo.
 */
void ml_session_checkpoint (MlSession *s);

/* Forgets the whole history (after a file was loaded, say). */
void ml_session_clear_history (MlSession *s);

typedef struct MlUndoResult
{
	MlResult result;
	int32_t  did;   /* 0 when there was nothing to undo or redo */
	uint64_t where; /* a line the change touched, for the cursor */
} MlUndoResult;

/* Takes back the last group of changes. */
MlUndoResult ml_session_undo (MlSession *s);

/* Does again what the last undo took back. */
MlUndoResult ml_session_redo (MlSession *s);

#endif /* MATLOG_SESSION_SESSION_H */
