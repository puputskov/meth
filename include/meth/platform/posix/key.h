#ifndef METH_PLATFORM_POSIX_KEY_H
#define METH_PLATFORM_POSIX_KEY_H

#include <meth/core/slice.h>
#include <meth/platform/key.h>

/*
 * Decoding of the bytes a Unix terminal sends into MlKey. It works on bytes
 * only and includes no OS header, so it can be unit tested.
 *
 *	printable ASCII and UTF-8     ML_KEY_CHAR
 *	Enter (CR, LF), Tab, 0x7F/BS  Enter, Tab, Backspace
 *	Ctrl+A .. Ctrl+Z              ML_KEY_CHAR, the lowercase letter, CTRL
 *	ESC [ A..D, H, F, 1~..24~     arrows, Home, End, Insert, Delete, PgUp,
 *	ESC O A..D, H, F, P..S        PgDn, F1..F12, with the xterm modifier
 *	                              parameter (ESC [ 1 ; 5 C is Ctrl+Right)
 *	ESC [ Z                       Shift+Tab
 *	ESC and then a key            that key with ALT
 *	a lone ESC                    Esc
 *
 * A lone ESC cannot be told from the start of a longer sequence by the bytes
 * alone, so the caller waits a short time for more bytes and then decodes with
 * final set.
 */

typedef struct MlVtDecode
{
	/* ML_KEY_NONE when the bytes were not a key (they are skipped) */
	MlKey    key;
	uint64_t used;       /* bytes consumed; 0 only when incomplete */
	/* the bytes are the start of a longer sequence; wait for more */
	int32_t  incomplete;
} MlVtDecode;

/*
 * Decodes the key at the start of bytes. With final set the bytes are all
 * there is, so a lone ESC is the Esc key and an unfinished sequence is
 * dropped instead of being incomplete.
 */
MlVtDecode ml_key_decode_vt (MlSlice bytes, int32_t final);

#endif /* METH_PLATFORM_POSIX_KEY_H */
