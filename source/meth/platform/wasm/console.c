#include <meth/platform/console.h>

/* There is no terminal in a page: the page is the screen. */

MlResult ml_console_open (MlConsole *c)
{
	c->active = 0;
	return ML_ERR_IO;
}

void ml_console_close (MlConsole *c)
{
	c->active = 0;
}

MlKeyResult ml_console_read_key (MlConsole *c)
{
	MlKeyResult r = { .result = ML_ERR_IO };

	(void)c;
	return r;
}

int32_t ml_console_wait (MlConsole *c, uint32_t timeout_ms)
{
	(void)c;
	(void)timeout_ms;
	return 0;
}

MlConsoleSize ml_console_size (const MlConsole *c)
{
	MlConsoleSize s = { .cols = 0, .rows = 0 };

	(void)c;
	return s;
}

MlResult ml_console_write (const MlConsole *c, MlSlice text)
{
	(void)c;
	(void)text;
	return ML_ERR_IO;
}

int32_t ml_console_truecolor (void)
{
	return 1;
}
