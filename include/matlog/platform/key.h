#ifndef MATLOG_PLATFORM_KEY_H
#define MATLOG_PLATFORM_KEY_H

#include <matlog/core/err.h>
#include <matlog/core/str.h>

typedef enum MlKeyCode
{
	ML_KEY_NONE = 0,
	ML_KEY_CHAR, /* printable character, see MlKey.cp */
	ML_KEY_ENTER,
	ML_KEY_ESCAPE,
	ML_KEY_BACKSPACE,
	ML_KEY_TAB,
	ML_KEY_DELETE,
	ML_KEY_INSERT,
	ML_KEY_LEFT,
	ML_KEY_RIGHT,
	ML_KEY_UP,
	ML_KEY_DOWN,
	ML_KEY_HOME,
	ML_KEY_END,
	ML_KEY_PAGE_UP,
	ML_KEY_PAGE_DOWN,
	ML_KEY_F1,
	ML_KEY_F2,
	ML_KEY_F3,
	ML_KEY_F4,
	ML_KEY_F5,
	ML_KEY_F6,
	ML_KEY_F7,
	ML_KEY_F8,
	ML_KEY_F9,
	ML_KEY_F10,
	ML_KEY_F11,
	ML_KEY_F12,
	ML_KEY_RESIZE, /* the terminal changed size, query it again */
	ML_KEY_COUNT
} MlKeyCode;

#define ML_MOD_SHIFT 1u
#define ML_MOD_CTRL  2u
#define ML_MOD_ALT   4u

typedef struct MlKey
{
	MlKeyCode code;
	uint32_t  cp; /* code point, only for ML_KEY_CHAR */
	/*
	 * ML_MOD_* flags. Ctrl+letter arrives as ML_KEY_CHAR with the lowercase
	 * letter and ML_MOD_CTRL.
	 */
	uint32_t mods;
} MlKey;

const char *ml_key_code_name (MlKeyCode code);

/*
 * Appends a readable form such as "Ctrl+Alt+Left", "Char 'a' U+0061" or "Esc".
 */
MlResult ml_key_format (MlStr *out, MlKey key);

#endif /* MATLOG_PLATFORM_KEY_H */
