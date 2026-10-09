#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <matlog/platform/console.h>
#include <matlog/platform/posix/key.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* How long a lone ESC waits for the rest of a sequence, in milliseconds. */
#define ESC_WAIT_MS 30

#define INPUT_CAP 256

/*
 * There is one terminal, so its state lives here: the settings to give back,
 * the bytes read but not decoded yet, and the resize notice from the signal.
 */
static struct termios        saved_termios;
static struct sigaction      saved_winch;
static uint8_t               in_bytes [INPUT_CAP];
static uint64_t              in_len;
static volatile sig_atomic_t resized;

static void on_winch (int32_t sig)
{
	(void)sig;
	resized = 1;
}

MlResult ml_console_open (MlConsole *c)
{
	MlResult         r = ML_OK;
	struct termios   raw;
	struct sigaction act;

	c->active         = 0;
	c->pending_high   = 0;
	c->in_handle      = STDIN_FILENO;
	c->out_handle     = STDOUT_FILENO;
	c->saved_in_mode  = 0;
	c->saved_out_mode = 0;
	c->saved_out_cp   = 0;
	in_len            = 0;
	resized           = 0;

	if (!isatty (STDIN_FILENO) || !isatty (STDOUT_FILENO))
	{
		r = ML_ERR_IO;
	}

	else if (tcgetattr (STDIN_FILENO, &saved_termios) != 0)
	{
		r = ML_ERR_IO;
	}

	if (r == ML_OK)
	{
		raw = saved_termios;
		raw.c_iflag &= ~(tcflag_t)(
			BRKINT | ICRNL | INPCK | ISTRIP | IXON | INLCR | IGNCR
		);
		raw.c_oflag &= ~(tcflag_t)OPOST;
		raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
		raw.c_cc [VMIN]  = 1;
		raw.c_cc [VTIME] = 0;

		if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &raw) != 0)
		{
			r = ML_ERR_IO;
		}
	}

	if (r == ML_OK)
	{
		/* no SA_RESTART: a resize interrupts the blocking read */
		act.sa_handler = on_winch;
		act.sa_flags   = 0;
		sigemptyset (&act.sa_mask);
		sigaction (SIGWINCH, &act, &saved_winch);
		c->active = 1;
	}

	return r;
}

void ml_console_close (MlConsole *c)
{
	if (c->active)
	{
		tcsetattr (STDIN_FILENO, TCSAFLUSH, &saved_termios);
		sigaction (SIGWINCH, &saved_winch, NULL);
		c->active = 0;
	}
}

/*
 * Reads what is there into the buffer, waiting at most wait_ms (-1: for ever).
 * Sets *timeout when nothing came in time.
 */
static MlResult fill (int32_t wait_ms, int32_t *timeout)
{
	MlResult      r    = ML_OK;
	struct pollfd pfd;
	int32_t       ready;

	pfd.fd      = STDIN_FILENO;
	pfd.events  = POLLIN;
	pfd.revents = 0;
	*timeout    = 0;
	ready       = poll (&pfd, 1, wait_ms);

	if (ready < 0)
	{
		/* a signal (the resize) ends the wait without an error */
		r = ml_choose (
			errno == EINTR,
			ML_OK,
			ML_ERR_IO
		);
	}

	else if (ready == 0)
	{
		*timeout = 1;
	}

	else
	{
		int64_t got = read (
			STDIN_FILENO,
			in_bytes + in_len,
			INPUT_CAP - in_len
		);

		if (got > 0)
		{
			in_len += (uint64_t)got;
		}

		else if (got == 0 || errno != EINTR)
		{
			r = ML_ERR_IO;
		}
	}

	return r;
}

/* Drops the first n bytes of the buffer. */
static void consume (uint64_t n)
{
	for (uint64_t i = n; i < in_len; i++)
	{
		in_bytes [i - n] = in_bytes [i];
	}

	in_len -= n;
}

MlKeyResult ml_console_read_key (MlConsole *c)
{
	MlKeyResult res =
	{
		.result = ML_OK,
		.key =
		{
			.code = ML_KEY_NONE,
			.cp   = 0,
			.mods = 0
		}
	};
	int32_t waited = 0;

	(void)c;

	while (res.result == ML_OK && res.key.code == ML_KEY_NONE)
	{
		MlVtDecode d = ml_key_decode_vt (
			ml_slice ((const char *)in_bytes, in_len),
			waited || in_len >= INPUT_CAP
		);

		if (resized)
		{
			resized      = 0;
			res.key.code = ML_KEY_RESIZE;
		}

		else if (!d.incomplete)
		{
			consume (d.used);
			res.key = d.key;
			waited  = 0;
		}

		else
		{
			int32_t timeout = 0;

			/* a started sequence gets a moment, an empty buffer waits */
			res.result = fill (
				ml_choose (
					in_len > 0,
					ESC_WAIT_MS,
					-1
				),
				&timeout
			);
			waited = timeout;
		}
	}

	return res;
}

MlConsoleSize ml_console_size (const MlConsole *c)
{
	MlConsoleSize  size =
	{
		.cols = 0,
		.rows = 0
	};
	struct winsize ws;

	(void)c;

	if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
	{
		size.cols = ws.ws_col;
		size.rows = ws.ws_row;
	}

	return size;
}

MlResult ml_console_write (const MlConsole *c, MlSlice text)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	(void)c;

	while (r == ML_OK && pos < text.len)
	{
		int64_t put = write (STDOUT_FILENO, text.data + pos, text.len - pos);

		if (put > 0)
		{
			pos += (uint64_t)put;
		}

		else if (put < 0 && errno == EINTR)
		{
			put = 0;
		}

		else
		{
			r = ML_ERR_IO;
		}
	}

	return r;
}
