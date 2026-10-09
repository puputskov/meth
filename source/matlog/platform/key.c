#include <matlog/platform/key.h>

static const char *const code_names[ML_KEY_COUNT] =
{
	"None", "Char", "Enter", "Esc", "Backspace", "Tab", "Delete", "Insert",
	"Left", "Right", "Up", "Down", "Home", "End", "PageUp", "PageDown",
	"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
	"Resize"
};

const char *ml_key_code_name (MlKeyCode code)
{
	int32_t known = (int32_t)code >= 0 && (int32_t)code < ML_KEY_COUNT;

	return known ? code_names[code] : "Unknown";
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
