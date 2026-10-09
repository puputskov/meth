#ifndef MATLOG_PLATFORM_CONSOLE_H
#define MATLOG_PLATFORM_CONSOLE_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/platform/key.h>

/*
 * Raw terminal access: unbuffered key input, VT escape sequence output and
 * UTF-8 text. The console state (modes, code pages) is saved on open and
 * restored on close. ml_console_close is safe to call more than once.
 */

typedef struct MlConsole
{
	uint64_t in_handle;    /* OS handles, opaque to callers */
	uint64_t out_handle;
	uint32_t saved_in_mode;
	uint32_t saved_out_mode;
	uint32_t saved_out_cp;
	uint16_t pending_high; /* high surrogate waiting for its pair */
	int32_t  active;
} MlConsole;

typedef struct MlConsoleSize
{
	uint32_t cols;
	uint32_t rows;
} MlConsoleSize;

typedef struct MlKeyResult
{
	MlResult result;
	MlKey    key;
} MlKeyResult;

/* Enters raw mode. Fails with ML_ERR_IO when stdin/stdout are not a console. */
MlResult ml_console_open (MlConsole *c);

void ml_console_close (MlConsole *c);

/*
 * Blocks until a key press (or a resize) arrives. Key releases and bare
 * modifier presses are skipped.
 */
MlKeyResult ml_console_read_key (MlConsole *c);

/* Visible window size in character cells (0 x 0 when unknown). */
MlConsoleSize ml_console_size (const MlConsole *c);

/* Writes UTF-8 text and VT escape sequences. */
MlResult ml_console_write (const MlConsole *c, MlSlice text);

#endif /* MATLOG_PLATFORM_CONSOLE_H */
