#ifndef METH_UI_KEYMAP_H
#define METH_UI_KEYMAP_H

#include <meth/core/vec.h>
#include <meth/platform/key.h>

/*
 * Key bindings (plan section 4.15): a key is mapped to a named action, and
 * the editor looks keys up here instead of comparing characters. The control
 * keys of the editor are the default table; the entries of the [keys] section
 * of the settings file replace or remove them, `Ctrl+Y = redo` binds a key,
 * `Ctrl+Z = none` removes one.
 *
 * Only keys that cannot be typed as text are bindable: a key needs Ctrl or
 * Alt, or is a function key, or is Shift+Enter. Plain characters, the arrows,
 * Enter, Esc, Tab, Backspace and the keys that select text (Shift with a move
 * key) belong to editing and stay as they are.
 *
 * A key is written as modifiers joined with +, then the key: `Ctrl+Shift+Z`,
 * `Alt+Left`, `F3`, `Ctrl+Enter`. The names are the ones of ml_key_code_name
 * (Esc, PageUp, ...), letters are not case sensitive.
 */

typedef enum MlAction
{
	ML_ACT_NONE = 0,
	ML_ACT_UNDO,
	ML_ACT_REDO,
	ML_ACT_BLOCK_NEW,   /* a block of the domain used last */
	ML_ACT_BLOCK_PICK,  /* choose the domain of the new block first */
	ML_ACT_NOTE_TOGGLE, /* the line is a note, or a math line */
	ML_ACT_DOMAIN_NEXT,
	ML_ACT_DOMAIN_PREV,
	ML_ACT_LINES_DOWN,  /* move the selected lines */
	ML_ACT_LINES_UP,
	ML_ACT_FOLD,
	ML_ACT_BLOCK_NEXT,
	ML_ACT_BLOCK_PREV,
	ML_ACT_GOTO_FIRST,
	ML_ACT_GOTO_LAST,
	ML_ACT_SELECT_LINE,
	ML_ACT_SELECT_ALL,
	ML_ACT_COPY,
	ML_ACT_CUT,
	ML_ACT_PASTE,
	ML_ACT_COMMAND,
	ML_ACT_SAVE,
	ML_ACT_QUIT,
	ML_ACT_STEPS,
	ML_ACT_HELP,
	ML_ACT_WATCH,
	ML_ACT_PLOT,
	ML_ACT_COUNT
} MlAction;

/* `undo`, `block-new`, ... ; "none" for ML_ACT_NONE. */
const char *ml_action_name (MlAction a);

/* What the action does, one line. */
const char *ml_action_summary (MlAction a);

/* ML_ACT_COUNT when the name is none of them ("none" is ML_ACT_NONE). */
MlAction ml_action_from_name (MlSlice name);

typedef struct MlKeyBinding
{
	MlKeyCode code;
	uint32_t  cp;   /* ML_KEY_CHAR: the letter, lower case for ASCII */
	uint32_t  mods; /* ML_MOD_* */
	MlAction  action;
} MlKeyBinding;

ML_VEC_DEFINE (MlKeyBindingVec, MlKeyBinding)

typedef struct MlKeymap
{
	MlKeyBindingVec bindings;
} MlKeymap;

typedef struct MlKeyParse
{
	MlResult    result; /* ML_ERR_PARSE when the text is not a key */
	MlKey       key;
	const char *problem; /* why not (a static text), else "" */
} MlKeyParse;

/* Reads a key such as `Ctrl+Shift+Z`. Not every key is bindable, see above. */
MlKeyParse ml_key_parse (MlSlice text);

/*
 * The same without the rule about which keys can be bound: Enter, Up, `a`
 * and Ctrl+Enter are all keys. A tutorial or a test writes the keys it
 * presses this way.
 */
MlKeyParse ml_key_parse_any (MlSlice text);

/* Appends the canonical text of a key, such as `Ctrl+Shift+Z`. */
MlResult ml_key_spec (MlStr *out, MlKey key);

/* The default table. */
MlResult ml_keymap_init (MlKeymap *km);
void ml_keymap_free (MlKeymap *km);

/* The action of a key as the terminal reports it, ML_ACT_NONE for no one. */
MlAction ml_keymap_lookup (const MlKeymap *km, MlKey key);

/* Binds a key (it replaces an earlier binding of the same key). */
MlResult ml_keymap_bind (MlKeymap *km, MlKey key, MlAction action);

/* Removes the binding of a key; the result says if there was one. */
int32_t ml_keymap_unbind (MlKeymap *km, MlKey key);

/*
 * Applies the entries of the section [keys] of a settings text on top of the
 * table. A line that is not a key, or not an action, is ignored, and told in
 * report (one line each: `line 3: ...`; report may be NULL); *bad counts them.
 */
MlResult ml_keymap_apply_text (
	MlKeymap *km,
	MlSlice  text,
	MlStr    *report,
	uint64_t *bad
);

/* The keys of an action, `Ctrl+Z, Ctrl+Shift+Z`; "" when it has none. */
MlResult ml_keymap_keys_of (MlStr *out, const MlKeymap *km, MlAction action);

/*
 * Texts that name a key do not contain the key but the action, written
 * {ACTION_UNDO}: ACTION_ and the name of the action in capitals with _ for -
 * (ACTION_BLOCK_NEW). The expansion gives the first key bound to the action
 * (`Ctrl+Z`), or the name of the action (`undo`) when it has none, so a text
 * is right for every key table and a changed default key needs no change of
 * the texts. open and close are the delimiters ({ } in texts, < > in the
 * key scripts of the tutorial); keep says if they stay around the key. Text
 * that is not a placeholder, an unknown action included, is copied as it is.
 * A placeholder followed by two or more spaces starts a column: key and spaces
 * take as many columns as the placeholder and its spaces, so the text after
 * them stays aligned in a table of keys.
 */
MlResult ml_keymap_expand (
	MlStr           *out,
	const MlKeymap  *km,
	MlSlice         text,
	char            open,
	char            close,
	int32_t         keep
);

/* The action a placeholder name such as ACTION_UNDO stands for, or COUNT. */
MlAction ml_action_from_placeholder (MlSlice name);

/* One line per action: the action, its keys and what it does. */
MlResult ml_keymap_format (MlStr *out, const MlKeymap *km);

#endif /* METH_UI_KEYMAP_H */
