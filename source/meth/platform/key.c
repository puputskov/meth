#include <meth/platform/key.h>

static const char *const code_names [ML_KEY_COUNT] = {
	[ML_KEY_NONE]      = "None",
	[ML_KEY_CHAR]      = "Char",
	[ML_KEY_ENTER]     = "Enter",
	[ML_KEY_ESCAPE]    = "Esc",
	[ML_KEY_BACKSPACE] = "Backspace",
	[ML_KEY_TAB]       = "Tab",
	[ML_KEY_DELETE]    = "Delete",
	[ML_KEY_INSERT]    = "Insert",
	[ML_KEY_LEFT]      = "Left",
	[ML_KEY_RIGHT]     = "Right",
	[ML_KEY_UP]        = "Up",
	[ML_KEY_DOWN]      = "Down",
	[ML_KEY_HOME]      = "Home",
	[ML_KEY_END]       = "End",
	[ML_KEY_PAGE_UP]   = "PageUp",
	[ML_KEY_PAGE_DOWN] = "PageDown",
	[ML_KEY_F1]        = "F1",
	[ML_KEY_F2]        = "F2",
	[ML_KEY_F3]        = "F3",
	[ML_KEY_F4]        = "F4",
	[ML_KEY_F5]        = "F5",
	[ML_KEY_F6]        = "F6",
	[ML_KEY_F7]        = "F7",
	[ML_KEY_F8]        = "F8",
	[ML_KEY_F9]        = "F9",
	[ML_KEY_F10]       = "F10",
	[ML_KEY_F11]       = "F11",
	[ML_KEY_F12]       = "F12",
	[ML_KEY_RESIZE]    = "Resize",
	[ML_KEY_MOUSE]     = "Mouse"
};

const char *ml_key_code_name (MlKeyCode code)
{
	int32_t known = (int32_t)code >= 0 && (int32_t)code < ML_KEY_COUNT;

	return ml_choose (
		known,
		code_names [code],
		"Unknown"
	);
}

MlResult ml_key_format (MlStr *out, MlKey key)
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
		r = ml_str_append (out, ml_slice_cstr ("Char '"));

		if (r == ML_OK)
		{
			r = ml_str_append_cp (out, key.cp);
		}

		if (r == ML_OK)
		{
			r = ml_str_appendf (out, "' U+%04X", (uint32_t)key.cp);
		}
	}

	else if (r == ML_OK)
	{
		r = ml_str_append (out, ml_slice_cstr (ml_key_code_name (key.code)));
	}

	return r;
}
