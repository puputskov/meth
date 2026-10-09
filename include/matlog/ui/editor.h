#ifndef MATLOG_UI_EDITOR_H
#define MATLOG_UI_EDITOR_H

#include <matlog/session/mlog.h>
#include <matlog/session/results.h>
#include <matlog/session/steps.h>
#include <matlog/ui/complete.h>
#include <matlog/ui/lineedit.h>
#include <matlog/ui/rewrite.h>

/*
 * The editor: free form editing on top of a session, and no modes. It takes
 * key events and changes the session; it does no drawing and no console
 * input, so it can be driven by tests.
 *
 * The cursor is always in a line (a note by default) and typing edits it.
 *	Text     typing, with the rewrites of rewrite.h; Left Right Home End
 *	         Up Down move the cursor, across lines too; Backspace at the
 *	         start joins a line with the one before, Delete at the end with
 *	         the next; Enter splits the line; Esc writes the line into the
 *	         session (also when the cursor leaves it) and, in a block,
 *	         starts a note below the block
 *	         Shift+Left Right Home End  select text; Backspace, Delete or
 *	         typing replace it
 *	Lines    Shift+Up Shift+Down  select whole lines, Ctrl+L the line;
 *	         there Backspace and Delete remove them, Ctrl+C copies, Ctrl+X cuts, Ctrl+V
 *	         replaces them by the copied lines, Alt+Up Alt+Down move them,
 *	         arrows and Esc go back to editing
 *	         without a selection Ctrl+C Ctrl+X Ctrl+V and Alt+Up Alt+Down
 *	         work on the line the cursor is in
 *	         Alt+Enter  fold or unfold the block (a folded block is selected)
 *	         Ctrl+Up Ctrl+Down  previous / next block;  Ctrl+Home Ctrl+End
 *	         Ctrl+Enter, Shift+Enter, Ctrl+B  choose the domain of a new
 *	         block; Ctrl+N  turn the line into a note or back
 *	         Alt+Left / Alt+Right  previous / next domain of the line
 *	         Ctrl+Z  undo;  Ctrl+Shift+Z, Ctrl+Y  redo
 *	         F3  show or hide the steps of the line
 *	         Ctrl+S  save;  Ctrl+Q  quit;  Ctrl+P  command line
 *	Command  :q :q! :w [path] :wq :open[!] path :mode name :pga 2|3 :steps
 *	         :undo :redo  :[range]d  :[range]y  :[range]m dest  :[range]t dest
 *	         ranges: 3  3,5  %  .  $  (1 based); m and t put the lines after
 *	         line dest (0 is the top)
 *	         :12 goes to line 12; Tab accepts the ghost text, see complete.h
 *	         (the command is typed after Ctrl+P, without a colon)
 */

typedef enum MlInputState
{
	ML_STATE_MATH = 0, /* a math line is being edited */
	ML_STATE_NOTE,     /* a note is being edited */
	ML_STATE_COMMAND,  /* the command line */
	ML_STATE_SELECT,   /* whole lines are selected */
	ML_STATE_COUNT
} MlInputState;

typedef struct MlEditor
{
	MlSession    session;
	MlResults    results; /* of the session lines, see results.h */
	/* the session revision the results were computed for */
	uint64_t     results_rev;
	/* the steps shown under a line (F3, or :steps) */
	int32_t      steps_open;
	uint64_t     steps_line;
	MlStr        steps;
	MlInputState state;
	MlLineEdit   edit;    /* the line being typed, or the command */
	MlRewriter   rewriter;
	MlLineVec    yank;    /* the line register: whole lines, with their kinds */
	MlStr        message; /* shown in the status line until the next key */
	uint64_t anchor;     /* the fixed end of the selected lines */
	int32_t  selecting;  /* 1 while text of the line is selected */
	uint64_t sel_anchor; /* the fixed end of it, a byte offset */
	/* where the line being typed lives, or will be inserted */
	uint64_t edit_index;
	/* 1 when editing a line that is already in the session */
	int32_t    edit_existing;
	MlLineKind edit_kind;
	MlDomain   edit_domain;
	/* 1 while the block to start is chosen (Ctrl+Enter, Shift+Enter) */
	int32_t    picking;
	MlDomain   pick; /* the domain chosen so far */
	int32_t    quit; /* a quit command was given */
} MlEditor;

void ml_editor_init (MlEditor *ed);
void ml_editor_free (MlEditor *ed);

/* Starts typing a new math line at the end of the session. */
MlResult ml_editor_begin (MlEditor *ed);

/* Writes the line being edited into the session (a new empty one is not). */
MlResult ml_editor_commit (MlEditor *ed);

/* Starts typing a new note at the end of the session (free form editing). */
MlResult ml_editor_begin_note (MlEditor *ed);

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
