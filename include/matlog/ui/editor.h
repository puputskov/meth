#ifndef MATLOG_UI_EDITOR_H
#define MATLOG_UI_EDITOR_H

#include <matlog/session/mlog.h>
#include <matlog/session/results.h>
#include <matlog/ui/complete.h>
#include <matlog/ui/lineedit.h>
#include <matlog/ui/rewrite.h>

/*
 * The modal editor: the input states and the domain mode on top of a
 * session. It takes key events and changes the session; it does no drawing
 * and no console input, so it can be driven by tests.
 *
 *	Normal   j k (or Down Up)  move the cursor line     G gg  last / first line
 *	         dd yy p P         delete / yank / paste after / paste before
 *	         Enter             edit the cursor line     i  new math line below
 *	         n                 new note line below      :  command line
 *	         Alt+Left / Alt+Right  previous / next domain (also in Insert,
 *	                          where it changes the domain of the typed line)
 *	         g + m l a e v b 2 3   set the domain (matrix, lambda, algebra,
 *	                           equations, vector, boolean, pga2, pga3)
 *	Insert   typing, with the rewrites of rewrite.h; Enter commits the line
 *	         and starts the next one, Esc commits and returns to Normal
 *	Note     like Insert for free text: no rewrites
 *	Command  :q :q! :w [path] :wq :open[!] path :mode name :pga 2|3
 *	         :12 goes to line 12; Tab accepts the ghost text, see complete.h
 *
 * A new editor starts in Insert, in the domain of its session.
 */

typedef enum MlInputState
{
	ML_STATE_NORMAL = 0,
	ML_STATE_INSERT,
	ML_STATE_NOTE,
	ML_STATE_COMMAND,
	ML_STATE_COUNT
} MlInputState;

/* The line register of dd, yy, p and P. kind is ML_LINE_NONE when empty. */
typedef struct MlYank
{
	MlLineKind kind;
	MlDomain   domain;
	MlStr      text;
} MlYank;

typedef struct MlEditor
{
	MlSession    session;
	MlResults    results; /* of the session lines, see results.h */
	/* the session revision the results were computed for */
	uint64_t     results_rev;
	MlInputState state;
	MlLineEdit   edit;    /* the line being typed, or the command */
	MlRewriter   rewriter;
	MlYank       yank;
	MlStr        message; /* shown in the status line until the next key */
	/* first key of a two key command (d, y or g), 0 when none */
	uint32_t pending;
	/* where the line being typed lives, or will be inserted */
	uint64_t edit_index;
	/* 1 when editing a line that is already in the session */
	int32_t    edit_existing;
	MlLineKind edit_kind;
	MlDomain   edit_domain;
	int32_t    quit; /* a quit command was given */
} MlEditor;

void ml_editor_init (MlEditor *ed);
void ml_editor_free (MlEditor *ed);

/* Starts typing a new math line at the end of the session. */
MlResult ml_editor_begin (MlEditor *ed);

/*
 * Handles a key. When it changed the session (a committed line, dd, p, :open)
 * the results of all lines are computed again before it returns.
 */
MlResult ml_editor_handle_key (MlEditor *ed, MlKey key);

/*
 * Computes the results again if the session changed since they were computed
 * (also needed after the session was loaded from outside the editor).
 */
MlResult ml_editor_refresh (MlEditor *ed);

/*
 * Appends "-- INSERT -- matrix>" (or ":command" in Command state) and the
 * message.
 */
MlResult ml_editor_status (MlStr *out, const MlEditor *ed);

/*
 * Completion of the command line: candidates, and the ghost text that Tab (or
 * Right at the end of the line) types. Empty unless the Command state is
 * active with the cursor at the end of the command.
 */
MlCompletion ml_editor_completion (const MlEditor *ed);

const char *ml_input_state_name (MlInputState state);

#endif /* MATLOG_UI_EDITOR_H */
