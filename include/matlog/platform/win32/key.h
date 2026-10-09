#ifndef MATLOG_PLATFORM_WIN32_KEY_H
#define MATLOG_PLATFORM_WIN32_KEY_H

#include <matlog/platform/key.h>

/*
 * Decoding of Windows console key events into MlKey. It only uses the numeric
 * values of the Win32 virtual key codes and control key state bits, so it does
 * not include any OS header and can be unit tested.
 */

typedef struct MlRawKey
{
	uint16_t vk;      /* wVirtualKeyCode */
	uint16_t unicode; /* uChar.UnicodeChar, a UTF-16 code unit or 0 */
	uint32_t control; /* dwControlKeyState */
} MlRawKey;

typedef struct MlDecodeResult
{
	MlKey    key;          /* ML_KEY_NONE when the event produced no key (modifier alone, high surrogate) */
	uint16_t pending_high; /* high surrogate waiting for its pair, pass it back in on the next call */
} MlDecodeResult;

MlDecodeResult ml_key_decode_win32 (MlRawKey raw, uint16_t pending_high);

#endif /* MATLOG_PLATFORM_WIN32_KEY_H */
