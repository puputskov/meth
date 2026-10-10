#include <matlog/ui/keymap.h>

#include <string.h>

#include <matlog/core/utf8.h>
#include <matlog/session/settings.h>
#include <matlog/texts/texts.h>

#define KEYS_SECTION "keys"

typedef struct ActionInfo
{
	const char *name;
} ActionInfo;

static const ActionInfo actions [ML_ACT_COUNT] = {
	[ML_ACT_NONE]        = { "none" },
	[ML_ACT_UNDO]        = { "undo" },
	[ML_ACT_REDO]        = { "redo" },
	[ML_ACT_BLOCK_NEW]   = { "block-new" },
	[ML_ACT_BLOCK_PICK]  = { "block-pick" },
	[ML_ACT_NOTE_TOGGLE] = { "note-toggle" },
	[ML_ACT_DOMAIN_NEXT] = { "domain-next" },
	[ML_ACT_DOMAIN_PREV] = { "domain-prev" },
	[ML_ACT_LINES_DOWN]  = { "lines-down" },
	[ML_ACT_LINES_UP]    = { "lines-up" },
	[ML_ACT_FOLD]        = { "fold" },
	[ML_ACT_BLOCK_NEXT]  = { "block-next" },
	[ML_ACT_BLOCK_PREV]  = { "block-prev" },
	[ML_ACT_GOTO_FIRST]  = { "goto-first" },
	[ML_ACT_GOTO_LAST]   = { "goto-last" },
	[ML_ACT_SELECT_LINE] = { "select-line" },
	[ML_ACT_SELECT_ALL]  = { "select-all" },
	[ML_ACT_COPY]        = { "copy" },
	[ML_ACT_CUT]         = { "cut" },
	[ML_ACT_PASTE]       = { "paste" },
	[ML_ACT_COMMAND]     = { "command" },
	[ML_ACT_SAVE]        = { "save" },
	[ML_ACT_QUIT]        = { "quit" },
	[ML_ACT_STEPS]       = { "steps" },
	[ML_ACT_HELP]        = { "help" },
	[ML_ACT_WATCH]       = { "watch" },
	[ML_ACT_PLOT]        = { "plot" }
};

const char *ml_action_name (MlAction a)
{
	return ml_choose (
		(int32_t)a >= 0 && a < ML_ACT_COUNT,
		actions [a].name,
		"?"
	);
}

const char *ml_action_summary (MlAction a)
{
	return ml_choose (
		(int32_t)a >= 0 && a < ML_ACT_COUNT,
		ml_text_in ("actions", actions [a].name),
		""
	);
}

MlAction ml_action_from_name (MlSlice name)
{
	MlAction found = ML_ACT_COUNT;

	for (uint32_t i = 0; i < ML_ACT_COUNT && found == ML_ACT_COUNT; i++)
	{
		if (ml_slice_equal (name, ml_slice_cstr (actions [i].name)))
		{
			found = (MlAction)i;
		}
	}

	return found;
}

/* ---- keys ---- */

static uint8_t lower (uint8_t c)
{
	return ml_choose (c >= 'A' && c <= 'Z', (uint8_t)(c + 32), c);
}

static int32_t same_word (MlSlice a, const char *word)
{
	MlSlice w  = ml_slice_cstr (word);
	int32_t ok = a.len == w.len;

	for (uint64_t i = 0; i < a.len && ok; i++)
	{
		ok = lower ((uint8_t)a.data [i]) == lower ((uint8_t)w.data [i]);
	}

	return ok;
}

/* Alternative spellings of the names of ml_key_code_name. */
typedef struct KeyAlias
{
	const char *name;
	MlKeyCode  code;
} KeyAlias;

static const KeyAlias aliases [] = {
	{ .name = "Escape",    .code = ML_KEY_ESCAPE },
	{ .name = "Return",    .code = ML_KEY_ENTER },
	{ .name = "Del",       .code = ML_KEY_DELETE },
	{ .name = "Ins",       .code = ML_KEY_INSERT },
	{ .name = "PgUp",      .code = ML_KEY_PAGE_UP },
	{ .name = "PgDn",      .code = ML_KEY_PAGE_DOWN },
	{ .name = "Page_Up",   .code = ML_KEY_PAGE_UP },
	{ .name = "Page_Down", .code = ML_KEY_PAGE_DOWN }
};

/* The key code a name spells, ML_KEY_NONE for none (or ML_KEY_CHAR). */
static MlKeyCode code_from_name (MlSlice name)
{
	MlKeyCode found = ML_KEY_NONE;

	for (uint32_t c = ML_KEY_ENTER; c <= ML_KEY_F12; c++)
	{
		if (same_word (name, ml_key_code_name ((MlKeyCode)c)))
		{
			found = (MlKeyCode)c;
		}
	}

	for (uint64_t i = 0; i < ml_count (aliases); i++)
	{
		if (same_word (name, aliases [i].name))
		{
			found = aliases [i].code;
		}
	}

	return found;
}

/* 1 when the text starts with the word and a plus. */
static int32_t starts_with_mod (MlSlice text, const char *word)
{
	uint64_t n = strlen (word);

	return (
		   text.len > n
		&& text.data [n] == '+'
		&& same_word (ml_slice_sub (text, 0, n), word)
	);
}

/* The key as the terminal reports it: the letter of Ctrl+Z is lower case. */
static MlKey normalized (MlKey key)
{
	if (key.code == ML_KEY_CHAR && key.cp < 128)
	{
		key.cp = lower ((uint8_t)key.cp);
	}

	if (key.code != ML_KEY_CHAR)
	{
		key.cp = 0;
	}

	return key;
}

/* The key a text spells, whether the key can be bound or not. */
static MlKeyParse parse_any (MlSlice text)
{
	MlKeyParse p = {
		.result  = ML_OK,
		.key     = {
			.code = ML_KEY_NONE,
			.cp   = 0,
			.mods = 0,
			.col  = 0,
			.row  = 0
		},
		.problem = ""
	};

	MlSlice rest = text;
	int32_t more = 1;

	while (more)
	{
		more = 0;

		if (starts_with_mod (rest, "Ctrl"))
		{
			p.key.mods |= ML_MOD_CTRL;
			rest        = ml_slice_sub (rest, 5, rest.len - 5);
			more        = 1;
		}

		else if (starts_with_mod (rest, "Alt"))
		{
			p.key.mods |= ML_MOD_ALT;
			rest        = ml_slice_sub (rest, 4, rest.len - 4);
			more        = 1;
		}

		else if (starts_with_mod (rest, "Shift"))
		{
			p.key.mods |= ML_MOD_SHIFT;
			rest        = ml_slice_sub (rest, 6, rest.len - 6);
			more        = 1;
		}
	}

	p.key.code = code_from_name (rest);

	if (p.key.code == ML_KEY_NONE && rest.len > 0)
	{
		MlDecoded d = ml_utf8_decode (rest);

		if (d.size == rest.len && d.cp != ML_CP_REPLACEMENT)
		{
			p.key.code = ML_KEY_CHAR;
			p.key.cp   = d.cp;
		}
	}

	p.key = normalized (p.key);

	if (p.key.code == ML_KEY_NONE)
	{
		p.result  = ML_ERR_PARSE;
		p.problem = "not a key";
	}

	return p;
}

MlKeyParse ml_key_parse_any (MlSlice text)
{
	return parse_any (text);
}

MlKeyParse ml_key_parse (MlSlice text)
{
	MlKeyParse p = parse_any (text);

	if (p.result == ML_OK)
	{
		int32_t fkey   = p.key.code >= ML_KEY_F1 && p.key.code <= ML_KEY_F12;
		int32_t strong = (p.key.mods & (ML_MOD_CTRL | ML_MOD_ALT)) != 0;
		int32_t block  = (
			   p.key.code == ML_KEY_ENTER
			&& (p.key.mods & ML_MOD_SHIFT)
		);

		if (!fkey && !strong && !block)
		{
			p.result  = ML_ERR_PARSE;
			p.problem = "a key needs Ctrl or Alt, or is a function key";
		}
	}

	return p;
}

MlResult ml_key_spec (MlStr *out, MlKey key)
{
	MlResult r = ML_OK;

	if (key.mods & ML_MOD_CTRL)
	{
		r = ml_str_append (out, ml_slice_cstr ("Ctrl+"));
	}

	if (r == ML_OK && (key.mods & ML_MOD_ALT))
	{
		r = ml_str_append (out, ml_slice_cstr ("Alt+"));
	}

	if (r == ML_OK && (key.mods & ML_MOD_SHIFT))
	{
		r = ml_str_append (out, ml_slice_cstr ("Shift+"));
	}

	if (r == ML_OK && key.code == ML_KEY_CHAR)
	{
		MlEncoded e = ml_utf8_encode (
			ml_choose (
				key.cp >= 'a' && key.cp <= 'z',
				key.cp - 32,
				key.cp
			)
		);

		r = ml_str_append (out, ml_slice (e.bytes, e.size));
	}

	else if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr (ml_key_code_name (key.code)));
	}

	return r;
}

/* ---- the table ---- */

typedef struct Default
{
	const char *key;
	MlAction   action;
} Default;

static const Default defaults [] = {
	{ .key = "Ctrl+Z",       .action = ML_ACT_UNDO },
	{ .key = "Ctrl+Y",       .action = ML_ACT_REDO },
	{ .key = "Ctrl+Shift+Z", .action = ML_ACT_REDO },
	{ .key = "Ctrl+R",       .action = ML_ACT_REDO },
	{ .key = "Ctrl+Enter",   .action = ML_ACT_BLOCK_NEW },
	{ .key = "Shift+Enter",  .action = ML_ACT_BLOCK_NEW },
	{ .key = "Ctrl+B",       .action = ML_ACT_BLOCK_PICK },
	{ .key = "Ctrl+N",       .action = ML_ACT_NOTE_TOGGLE },
	{ .key = "Alt+Right",    .action = ML_ACT_DOMAIN_NEXT },
	{ .key = "Alt+Left",     .action = ML_ACT_DOMAIN_PREV },
	{ .key = "Alt+Down",     .action = ML_ACT_LINES_DOWN },
	{ .key = "Alt+Up",       .action = ML_ACT_LINES_UP },
	{ .key = "Alt+Enter",    .action = ML_ACT_FOLD },
	{ .key = "Ctrl+Down",    .action = ML_ACT_BLOCK_NEXT },
	{ .key = "Ctrl+Up",      .action = ML_ACT_BLOCK_PREV },
	{ .key = "Ctrl+Home",    .action = ML_ACT_GOTO_FIRST },
	{ .key = "Ctrl+End",     .action = ML_ACT_GOTO_LAST },
	{ .key = "Ctrl+L",       .action = ML_ACT_SELECT_LINE },
	{ .key = "Ctrl+A",       .action = ML_ACT_SELECT_ALL },
	{ .key = "Ctrl+C",       .action = ML_ACT_COPY },
	{ .key = "Ctrl+X",       .action = ML_ACT_CUT },
	{ .key = "Ctrl+V",       .action = ML_ACT_PASTE },
	{ .key = "Ctrl+P",       .action = ML_ACT_COMMAND },
	{ .key = "Ctrl+S",       .action = ML_ACT_SAVE },
	{ .key = "Ctrl+Q",       .action = ML_ACT_QUIT },
	{ .key = "F3",           .action = ML_ACT_STEPS },
	{ .key = "F1",           .action = ML_ACT_HELP },
	{ .key = "F4",           .action = ML_ACT_WATCH },
	{ .key = "F7",           .action = ML_ACT_PLOT }
};

MlResult ml_keymap_init (MlKeymap *km)
{
	MlResult r = ML_OK;

	MlKeyBindingVec_init (&km->bindings);

	for (uint64_t i = 0; i < ml_count (defaults) && r == ML_OK; i++)
	{
		MlKeyParse p = ml_key_parse (ml_slice_cstr (defaults [i].key));

		r = ml_keymap_bind (km, p.key, defaults [i].action);
	}

	return r;
}

void ml_keymap_free (MlKeymap *km)
{
	MlKeyBindingVec_free (&km->bindings);
}

/* The index of the binding of a (normalized) key, else the length. */
static uint64_t find (const MlKeymap *km, MlKey key)
{
	uint64_t found = km->bindings.len;

	for (uint64_t i = 0; i < km->bindings.len && found == km->bindings.len; i++)
	{
		MlKeyBinding b = km->bindings.data [i];

		if (b.code == key.code && b.cp == key.cp && b.mods == key.mods)
		{
			found = i;
		}
	}

	return found;
}

MlAction ml_keymap_lookup (const MlKeymap *km, MlKey key)
{
	MlKey    k = normalized (key);
	uint64_t i = find (km, k);

	return ml_choose (
		i < km->bindings.len,
		km->bindings.data [i].action,
		ML_ACT_NONE
	);
}

MlResult ml_keymap_bind (MlKeymap *km, MlKey key, MlAction action)
{
	MlResult r = ML_OK;
	MlKey    k = normalized (key);
	uint64_t i = find (km, k);
	MlKeyBinding b = {
		.code   = k.code,
		.cp     = k.cp,
		.mods   = k.mods,
		.action = action
	};

	if (i < km->bindings.len)
	{
		km->bindings.data [i] = b;
	}

	else
	{
		r = MlKeyBindingVec_push (&km->bindings, b);
	}

	return r;
}

int32_t ml_keymap_unbind (MlKeymap *km, MlKey key)
{
	uint64_t i   = find (km, normalized (key));
	int32_t  had = i < km->bindings.len;

	if (had)
	{
		MlKeyBindingVec_remove (&km->bindings, i);
	}

	return had;
}

static MlResult report_line (
	MlStr      *report,
	uint64_t   line,
	const char *what,
	MlSlice    text
)
{
	MlResult r = ML_OK;

	if (report != NULL)
	{
		r = ml_str_appendf (
			report,
			"line %u: %s: %.*s\n",
			(uint32_t)line,
			what,
			(int)text.len,
			text.data
		);
	}

	return r;
}

MlResult ml_keymap_apply_text (
	MlKeymap *km,
	MlSlice  text,
	MlStr    *report,
	uint64_t *bad
)
{
	MlResult       r = ML_OK;
	MlSettingEntry e = ml_settings_text_entry (
		text,
		ml_slice_cstr (KEYS_SECTION),
		0
	);

	*bad = 0;

	for (uint64_t i = 1; e.found && r == ML_OK; i++)
	{
		MlKeyParse p = ml_key_parse (e.key);
		MlAction   a = ml_action_from_name (e.value);

		if (p.result != ML_OK)
		{
			(*bad)++;
			r = report_line (report, e.line, p.problem, e.key);
		}

		else if (a == ML_ACT_COUNT)
		{
			(*bad)++;
			r = report_line (report, e.line, "not an action", e.value);
		}

		else if (a == ML_ACT_NONE)
		{
			ml_keymap_unbind (km, p.key);
		}

		else
		{
			r = ml_keymap_bind (km, p.key, a);
		}

		e = ml_settings_text_entry (text, ml_slice_cstr (KEYS_SECTION), i);
	}

	return r;
}

MlResult ml_keymap_keys_of (MlStr *out, const MlKeymap *km, MlAction action)
{
	MlResult r     = ML_OK;
	int32_t  first = 1;

	for (uint64_t i = 0; i < km->bindings.len && r == ML_OK; i++)
	{
		MlKeyBinding b = km->bindings.data [i];
		MlKey     k = {
			.code = b.code,
			.cp   = b.cp,
			.mods = b.mods,
			.col  = 0,
			.row  = 0
		};

		if (b.action == action)
		{
			if (!first)
			{
				r = ml_str_append (out, ml_slice_cstr (", "));
			}

			first = 0;

			if (r == ML_OK)
			{
				r = ml_key_spec (out, k);
			}
		}
	}

	return r;
}

/* 1 when name is ACTION_ and the action name in capitals, - as _. */
static int32_t placeholder_is (MlSlice name, const char *action)
{
	uint64_t prefix = 7;
	uint64_t n      = strlen (action);
	int32_t  same   = name.len == prefix + n
		&& memcmp (name.data, "ACTION_", prefix) == 0;

	for (uint64_t i = 0; i < n && same; i++)
	{
		char c = action [i];

		if (c == '-')
		{
			c = '_';
		}

		else if (c >= 'a' && c <= 'z')
		{
			c = (char)(c - 32);
		}

		same = name.data [prefix + i] == c;
	}

	return same;
}

MlAction ml_action_from_placeholder (MlSlice name)
{
	MlAction found = ML_ACT_COUNT;

	for (
		uint32_t i = ML_ACT_UNDO;
		i < ML_ACT_COUNT && found == ML_ACT_COUNT;
		i++
	)
	{
		if (placeholder_is (name, actions [i].name))
		{
			found = (MlAction)i;
		}
	}

	return found;
}

/* The first key of the action, or its name when it has none. */
static MlResult first_key (MlStr *out, const MlKeymap *km, MlAction action)
{
	MlResult r = ML_OK;
	MlStr    keys;
	uint64_t end = 0;

	ml_str_init (&keys);
	r = ml_keymap_keys_of (&keys, km, action);

	while (end < keys.len && keys.data [end] != ',')
	{
		end++;
	}

	if (r == ML_OK && end > 0)
	{
		r = ml_str_append (out, ml_slice_sub (ml_str_view (keys), 0, end));
	}

	else if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr (ml_action_name (action)));
	}

	ml_str_free (&keys);
	return r;
}

MlResult ml_keymap_expand (
	MlStr          *out,
	const MlKeymap *km,
	MlSlice        text,
	char           open,
	char           close,
	int32_t        keep
)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	while (pos < text.len && r == ML_OK)
	{
		MlAction act = ML_ACT_COUNT;
		uint64_t end = pos + 1;

		if (text.data [pos] == open)
		{
			while (end < text.len && text.data [end] != close)
			{
				end++;
			}

			if (end < text.len)
			{
				act = ml_action_from_placeholder (
					ml_slice_sub (text, pos + 1, end - pos - 1)
				);
			}
		}

		if (act != ML_ACT_COUNT)
		{
			uint64_t spaces = 0;
			uint64_t before = out->len;

			r = ml_choose (
				keep,
				ml_str_append_cp (out, (uint32_t)open),
				ML_OK
			);

			if (r == ML_OK)
			{
				r = first_key (out, km, act);
			}

			if (r == ML_OK && keep)
			{
				r = ml_str_append_cp (out, (uint32_t)close);
			}

			/*
			 * Two or more spaces after the placeholder are a column: the
			 * text after them stays where it is with the placeholder
			 * written out, so a table lines up with every key.
			 */
			while (
				   end + 1 + spaces < text.len
				&& text.data [end + 1 + spaces] == ' '
			)
			{
				spaces++;
			}

			if (r == ML_OK && spaces >= 2)
			{
				uint64_t column = (end + 1 - pos) + spaces;
				uint64_t used   = out->len - before;

				for (
					uint64_t k = 0;
					k < ml_choose (column > used + 1, column - used, 1);
					k++
				)
				{
					r = ml_str_append (out, ml_slice_cstr (" "));
				}

				end += spaces;
			}

			pos = end + 1;
		}

		else
		{
			r = ml_str_append (out, ml_slice_sub (text, pos, 1));
			pos++;
		}
	}

	return r;
}

MlResult ml_keymap_format (MlStr *out, const MlKeymap *km)
{
	MlResult r = ML_OK;

	for (uint32_t a = ML_ACT_UNDO; a < ML_ACT_COUNT && r == ML_OK; a++)
	{
		MlStr keys;

		ml_str_init (&keys);
		r = ml_keymap_keys_of (&keys, km, (MlAction)a);

		if (r == ML_OK)
		{
			r = ml_str_appendf (
				out,
				"%-12s %-30s %s\n",
				ml_action_name ((MlAction)a),
				ml_choose (keys.len > 0, ml_str_cstr (keys), "-"),
				ml_action_summary ((MlAction)a)
			);
		}

		ml_str_free (&keys);
	}

	return r;
}
