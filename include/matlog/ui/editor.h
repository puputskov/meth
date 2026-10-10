#ifndef MATLOG_UI_EDITOR_H
#define MATLOG_UI_EDITOR_H

#include <matlog/platform/key.h>
#include <matlog/session/mlog.h>
#include <matlog/session/results.h>
#include <matlog/session/steps.h>
#include <matlog/ui/complete.h>
#include <matlog/ui/help.h>
#include <matlog/ui/keymap.h>
#include <matlog/ui/lineedit.h>
#include <matlog/ui/rewrite.h>
#include <matlog/ui/theme.h>

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
 *	Page     Page Up, Page Down move the cursor a page of lines (with Shift
 *	         they select); Ctrl+Left at the start of a line goes to the end
 *	         of the one before, Ctrl+Right at the end to the start of the next
 *	Select   as in other editors: Shift with Left Right Up Down Home End
 *	         (and Ctrl for words) selects text, also across lines; Ctrl+A
 *	         all, Ctrl+L the line; Backspace, Delete, Enter or typing
 *	         replace the selection; a plain Left or Right goes to its start
 *	         or end; Ctrl+C copies, Ctrl+X cuts and Ctrl+V pastes text
 *	         (whole lines keep their kinds); Alt+Up Alt+Down move the lines
 *	         of the selection
 *	         without a selection Ctrl+C Ctrl+X Ctrl+V work on the line the
 *	         cursor is in (paste goes below it) and Alt+Up Alt+Down move it
 *	         Alt+Enter  fold or unfold the block (a folded block is selected)
 *	         Ctrl+Up Ctrl+Down  previous / next block;  Ctrl+Home Ctrl+End
 *	         Ctrl+Enter, Shift+Enter  start a block of the domain used last
 *	         (the same key twice: terminals differ in which one they report);
 *	         Ctrl+B  choose the domain of the new block first;
 *	         Ctrl+N  turn the line into a note or back
 *	         Alt+Left / Alt+Right  previous / next domain of the line
 *	         Ctrl+Z  undo;  Ctrl+Shift+Z, Ctrl+Y  redo
 *	         F3  show or hide the steps of the line
 *	         Ctrl+S  save;  Ctrl+Q  quit;  Ctrl+P  command line
 *	Command  :q :q! :w [path] :wq :open[!] path :mode name :pga 2|3 :steps
 *	         :help [page|words]  (F1: help for the word under the cursor)
 *	         :theme [name|reload]  :colors [24bit|256|16|off]
 *	         :export[!] latex|mathml|markdown [body] [errors] [3-12] [path]
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
	ML_STATE_HELP,     /* the help viewer, see help.h */
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
	/*
	 * 1 while text is selected: from (sel_line, sel_anchor), the fixed end,
	 * to the cursor (edit_index, edit.cursor); bytes are offsets in a line
	 */
	int32_t  selecting;
	uint64_t sel_line;
	uint64_t sel_anchor;
	/* 1 when the register holds text to paste inline, 0 for whole lines */
	int32_t  yank_text;
	/* where the line being typed lives, or will be inserted */
	uint64_t edit_index;
	/* 1 when editing a line that is already in the session */
	int32_t    edit_existing;
	MlLineKind edit_kind;
	MlDomain   edit_domain;
	/* 1 while the block to start is chosen (Ctrl+B) */
	int32_t    picking;
	MlDomain   pick; /* the domain chosen so far */
	int32_t    quit; /* a quit command was given */
	/* how many lines Page Up and Page Down move, see ml_editor_set_page */
	uint64_t   page_rows;
	/* a mouse drag in progress: the text under the button when it went down */
	int32_t    mouse_down;
	uint64_t   mouse_line;
	uint64_t   mouse_byte;
	/*
	 * The look: 0 draws the plain way (dim, reverse and red only), 1 draws
	 * with the theme at the colour level. :theme and :colors set them.
	 */
	int32_t      highlight;
	int32_t      ghost; /* 1 shows the preview of the result while typing */
	int32_t      watch; /* 1 shows the watch window, see watchpanel.h */
	MlTheme      theme;
	MlColorLevel colors;
	/* the theme files of the user, name.theme per line, read by Ctrl+P */
	MlStr        theme_files;
	MlHelp       help; /* the help viewer, shown in the state HELP */
	MlKeymap     keymap; /* key to action, see keymap.h */
} MlEditor;

/* The selected text in text order; valid is 0 when nothing is selected. */
typedef struct MlTextRange
{
	int32_t  valid;
	uint64_t first_line;
	uint64_t first_byte;
	uint64_t last_line;
	uint64_t last_byte;
} MlTextRange;

MlTextRange ml_editor_selection (const MlEditor *ed);

void ml_editor_init (MlEditor *ed);

/*
 * Resets the key bindings to the defaults and applies the [keys] section of
 * the settings file (see keymap.h). A line that is not a binding is ignored
 * and told in the message. Also :config reload.
 */
MlResult ml_editor_load_keys (MlEditor *ed);

/*
 * Applies the settings of the file (session/options.h) that the file has:
 * theme, colors, highlight and ghost. Also :config reload.
 */
MlResult ml_editor_load_settings (MlEditor *ed);

/*
 * Chooses a built in theme or a name.theme file of the settings folder, as
 * :theme does (the message says what happened, the theme stays when there is
 * no such one).
 */
MlResult ml_editor_apply_theme (MlEditor *ed, MlSlice name);

/*
 * The size of the screen: Page Up and Page Down move a page of its rows, and
 * the help viewer lays its pages out for it.
 */
void ml_editor_set_size (MlEditor *ed, uint32_t cols, uint32_t rows);

/* The lines a Page Up or Page Down moves: the height of the text area. */
void ml_editor_set_page (MlEditor *ed, uint64_t rows);
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

/* The text under a place of the screen (see ml_screen_hit). */
typedef struct MlHit
{
	int32_t  valid;  /* 0 when the place shows no text of a line */
	int32_t  folded; /* the place is a folded block */
	uint64_t line;   /* session line */
	uint64_t byte;   /* byte in that line */
} MlHit;

/*
 * A mouse event (key.code is ML_KEY_MOUSE) and what is under it on the screen,
 * see ml_screen_hit. A press puts the cursor there (on a folded block it
 * selects the block), a drag with the button down selects text from where the
 * press was, the wheel moves the cursor three lines. Nothing else happens.
 */
MlResult ml_editor_mouse (MlEditor *ed, MlKey key, MlHit hit);

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
