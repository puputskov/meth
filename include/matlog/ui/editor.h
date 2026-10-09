#ifndef MATLOG_UI_EDITOR_H
#define MATLOG_UI_EDITOR_H

#include <matlog/session/mlog.h>
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
 *	         g + m l a e v b 2 3   set the domain (matrix, lambda, algebra,
 *	                           equations, vector, boolean, pga2, pga3)
 *	Insert   typing, with the rewrites of rewrite.h; Enter commits the line
 *	         and starts the next one, Esc commits and returns to Normal
 *	Note     like Insert for free text: no rewrites
 *	Command  :q :q! :w [path] :wq :open[!] path :mode name :pga 2|3
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

MlResult ml_editor_handle_key (MlEditor *ed, MlKey key);

/*
 * Appends "-- INSERT -- matrix>" (or ":command" in Command state) and the
 * message.
 */
MlResult ml_editor_status (MlStr *out, const MlEditor *ed);

const char *ml_input_state_name (MlInputState state);

#endif /* MATLOG_UI_EDITOR_H */
