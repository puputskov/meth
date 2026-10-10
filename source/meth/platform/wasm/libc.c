/*
 * The C library functions the program uses, for the WebAssembly build (the
 * headers are in include/meth/platform/wasm/libc/). Built with -fno-builtin so
 * a loop here is never turned into a call of the function it implements.
 */

#include <meth/core/types.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int errno = 0;

/* ---- memory and strings ------------------------------------------------ */

void *memcpy (void *dst, const void *src, size_t n)
{
	uint8_t       *d = (uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;

	for (size_t i = 0; i < n; i++)
	{
		d [i] = s [i];
	}

	return dst;
}

void *memmove (void *dst, const void *src, size_t n)
{
	uint8_t       *d = (uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;

	if (d < s)
	{
		for (size_t i = 0; i < n; i++)
		{
			d [i] = s [i];
		}
	}

	else
	{
		for (size_t i = n; i > 0; i--)
		{
			d [i - 1] = s [i - 1];
		}
	}

	return dst;
}

void *memset (void *dst, int c, size_t n)
{
	uint8_t *d = (uint8_t *)dst;

	for (size_t i = 0; i < n; i++)
	{
		d [i] = (uint8_t)c;
	}

	return dst;
}

int memcmp (const void *a, const void *b, size_t n)
{
	const uint8_t *x = (const uint8_t *)a;
	const uint8_t *y = (const uint8_t *)b;
	int            r = 0;

	for (size_t i = 0; i < n && r == 0; i++)
	{
		r = (int)x [i] - (int)y [i];
	}

	return r;
}

size_t strlen (const char *s)
{
	size_t n = 0;

	while (s [n] != '\0')
	{
		n++;
	}

	return n;
}

int strncmp (const char *a, const char *b, size_t n)
{
	int    r    = 0;
	size_t i    = 0;
	int    more = 1;

	while (more && i < n && r == 0)
	{
		r    = (int)(uint8_t)a [i] - (int)(uint8_t)b [i];
		more = a [i] != '\0';
		i++;
	}

	return r;
}

int strcmp (const char *a, const char *b)
{
	return strncmp (a, b, (size_t)-1);
}

char *strchr (const char *s, int c)
{
	const char *at   = NULL;
	size_t      i    = 0;
	int         more = 1;

	while (more && at == NULL)
	{
		if (s [i] == (char)c)
		{
			at = s + i;
		}

		more = s [i] != '\0';
		i++;
	}

	return (char *)at;
}

char *strstr (const char *hay, const char *needle)
{
	size_t n   = strlen (needle);
	char   *at = NULL;

	for (size_t i = 0; at == NULL && hay [i] != '\0'; i++)
	{
		if (strncmp (hay + i, needle, n) == 0)
		{
			at = (char *)hay + i;
		}
	}

	return at;
}

char *getenv (const char *name)
{
	(void)name;
	return NULL;
}

int atoi (const char *s)
{
	int n    = 0;
	int sign = 1;

	if (*s == '-')
	{
		sign = -1;
		s++;
	}

	for (; *s >= '0' && *s <= '9'; s++)
	{
		n = n * 10 + (*s - '0');
	}

	return sign * n;
}

int abs (int n)
{
	return ml_choose (n < 0, -n, n);
}

/* ---- math -------------------------------------------------------------- */

double round (double x)
{
	double r = trunc (x);

	if (fabs (x - r) >= 0.5)
	{
		r += ml_choose (x < 0, -1.0, 1.0);
	}

	return r;
}

double hypot (double x, double y)
{
	return sqrt (x * x + y * y);
}

double ldexp (double x, int n)
{
	double r = x;

	for (int i = 0; i < n; i++)
	{
		r *= 2.0;
	}

	for (int i = 0; i > n; i--)
	{
		r *= 0.5;
	}

	return r;
}

/*
 * ln |gamma (x)| by the Lanczos approximation (g = 7). For x < 1/2 the
 * reflection formula turns it into the same sum at 1 - x, which is written
 * out below instead of calling this function again (nothing recurses).
 */
static double lanczos (double x)
{
	static const double c [9] = {
		0.99999999999980993,  676.5203681218851,    -1259.1392167224028,
		771.32342877765313,   -176.61502916214059,  12.507343278686905,
		-0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7
	};

	const double pi = 3.14159265358979323846;
	double       a  = c [0];
	double       t  = x - 1.0 + 7.5;

	for (int i = 1; i < 9; i++)
	{
		a += c [i] / (x - 1.0 + (double)i);
	}

	return 0.5 * log (2.0 * pi) + (x - 0.5) * log (t) - t + log (a);
}

double lgamma (double x)
{
	const double pi = 3.14159265358979323846;
	double       r  = 0.0;

	if (x < 0.5)
	{
		r = log (pi / fabs (sin (pi * x))) - lanczos (1.0 - x);
	}

	else
	{
		r = lanczos (x);
	}

	return r;
}

/*
 * erfc (x). For x below 1 it is 1 - erf (x) with the series
 * erf (x) = 2/sqrt (pi) * exp (-x^2) * sum 2^n x^(2n+1) / (1 * 3 * ... (2n+1))
 * whose terms are all positive; from 1 on, the continued fraction
 * erfc (x) = exp (-x^2) / sqrt (pi) / (x + (1/2) / (x + 1 / (x + (3/2) / ...)))
 * evaluated from the tail up. Negative x uses erfc (-x) = 2 - erfc (x).
 */
double erfc (double x)
{
	const double sqrt_pi = 1.7724538509055160273;
	double       ax      = fabs (x);
	double       v       = 0.0;

	if (ax < 1.0)
	{
		double term = ax;
		double sum  = ax;

		for (int n = 1; n < 100; n++)
		{
			term *= 2.0 * ax * ax / (2.0 * (double)n + 1.0);
			sum  += term;
		}

		v = 1.0 - 2.0 / sqrt_pi * exp (-ax * ax) * sum;
	}

	else
	{
		double f = ax;

		for (int k = 200; k >= 1; k--)
		{
			f = ax + 0.5 * (double)k / f;
		}

		v = exp (-ax * ax) / sqrt_pi / f;
	}

	return ml_choose (x < 0.0, 2.0 - v, v);
}

/* ---- printf ------------------------------------------------------------ */

/*
 * The page formats a double like printf's %.<prec>f or %.<prec>g (conv is the
 * letter) and writes it into buf; the length comes back.
 */
__attribute__ ((import_module ("env"), import_name ("format_double")))
int32_t format_double (
	double  v,
	int32_t conv,
	int32_t prec,
	char    *buf,
	int32_t cap
);

#define TEXT_MAX 400

typedef struct Out
{
	char     *buf;
	uint64_t cap;
	uint64_t len; /* what the whole text needs, past cap too */
} Out;

typedef struct Spec
{
	int32_t left;  /* - */
	int32_t zero;  /* 0 */
	int32_t width;
	int32_t prec;  /* -1 if none */
	int32_t wide;  /* ll: the argument is 64 bits, long and size_t are 32 */
	char    conv;
} Spec;

static void put (Out *o, char c)
{
	if (o->len + 1 < o->cap)
	{
		o->buf [o->len] = c;
	}

	o->len++;
}

/* n bytes of text, padded to the width with pad (or with spaces at right). */
static void put_field (
	Out        *o,
	const char *text,
	uint64_t    n,
	const Spec *sp,
	char        pad
)
{
	uint64_t fill = 0;

	if (sp->width > 0 && (uint64_t)sp->width > n)
	{
		fill = (uint64_t)sp->width - n;
	}

	for (uint64_t i = 0; !sp->left && i < fill; i++)
	{
		put (o, pad);
	}

	for (uint64_t i = 0; i < n; i++)
	{
		put (o, text [i]);
	}

	for (uint64_t i = 0; sp->left && i < fill; i++)
	{
		put (o, ' ');
	}
}

/* An integer conversion: d, i, u, x or X. */
static void put_integer (Out *o, const Spec *sp, va_list *ap)
{
	uint64_t v    = 0;
	uint64_t base = ml_choose (sp->conv == 'x' || sp->conv == 'X', 16, 10);
	int32_t  neg  = 0;
	char     rev [24];
	char     text [26];
	uint64_t k    = 0;
	uint64_t n    = 0;
	char     ten  = ml_choose (sp->conv == 'X', 'A', 'a');

	if (sp->conv == 'd' || sp->conv == 'i')
	{
		int64_t s = ml_choose (
			sp->wide,
			va_arg (*ap, long long),
			va_arg (*ap, int)
		);

		neg = s < 0;
		v   = (uint64_t)ml_choose (neg, -s, s);
	}

	else
	{
		v = ml_choose (
			sp->wide,
			va_arg (*ap, unsigned long long),
			va_arg (*ap, unsigned int)
		);
	}

	do
	{
		uint64_t d = v % base;

		rev [k++] = (char)ml_choose (d < 10, '0' + d, ten + d - 10);
		v        /= base;
	} while (v > 0);

	if (neg)
	{
		text [n++] = '-';
	}

	while (k > 0)
	{
		text [n++] = rev [--k];
	}

	put_field (o, text, n, sp, ml_choose (sp->zero && !sp->left, '0', ' '));
}

/* Reads the flags, width, precision and length that follow a %. */
static const char *read_spec (const char *p, Spec *sp, va_list *ap)
{
	sp->left  = 0;
	sp->zero  = 0;
	sp->width = 0;
	sp->prec  = -1;

	for (; *p == '-' || *p == '0'; p++)
	{
		sp->left |= *p == '-';
		sp->zero |= *p == '0';
	}

	if (*p == '*')
	{
		sp->width = va_arg (*ap, int);
		sp->left |= sp->width < 0;
		sp->width = ml_choose (sp->width < 0, -sp->width, sp->width);
		p++;
	}

	for (; *p >= '0' && *p <= '9'; p++)
	{
		sp->width = sp->width * 10 + (*p - '0');
	}

	if (*p == '.')
	{
		p++;
		sp->prec = 0;

		if (*p == '*')
		{
			sp->prec = va_arg (*ap, int);
			p++;
		}

		for (; *p >= '0' && *p <= '9'; p++)
		{
			sp->prec = sp->prec * 10 + (*p - '0');
		}
	}

	sp->wide = 0;

	while (*p == 'l' || *p == 'z' || *p == 'h')
	{
		sp->wide = ml_choose (*p == 'l' && p [-1] == 'l', 1, sp->wide);
		p++;
	}

	sp->conv = *p;
	return p;
}

/* One conversion, after its % has been read. */
static void put_conversion (Out *o, const Spec *sp, va_list *ap)
{
	switch (sp->conv)
	{
		case 'd':
		case 'i':
		case 'u':
		case 'x':
		case 'X':
		{
			put_integer (o, sp, ap);
		} break;

		case 'c':
		{
			char c = (char)va_arg (*ap, int);

			put_field (o, &c, 1, sp, ' ');
		} break;

		case 's':
		{
			const char *s = va_arg (*ap, const char *);
			uint64_t    n = strlen (s);

			if (sp->prec >= 0 && (uint64_t)sp->prec < n)
			{
				n = (uint64_t)sp->prec;
			}

			put_field (o, s, n, sp, ' ');
		} break;

		case 'f':
		case 'g':
		{
			double  v = va_arg (*ap, double);
			char    text [TEXT_MAX];
			int32_t n = format_double (
				v,
				sp->conv,
				ml_choose (sp->prec < 0, 6, sp->prec),
				text,
				TEXT_MAX
			);

			put_field (
				o,
				text,
				(uint64_t)n,
				sp,
				ml_choose (sp->zero && !sp->left, '0', ' ')
			);
		} break;

		case '%':
		{
			put (o, '%');
		} break;

		default:
		{
			put (o, '%');

			if (sp->conv != '\0')
			{
				put (o, sp->conv);
			}
		} break;
	}
}

int vsnprintf (char *buf, size_t cap, const char *fmt, va_list ap)
{
	Out        o = { .buf = buf, .cap = cap, .len = 0 };
	const char *p = fmt;
	va_list    args;

	va_copy (args, ap);

	while (*p != '\0')
	{
		if (*p != '%')
		{
			put (&o, *p);
		}

		else
		{
			Spec sp;

			p = read_spec (p + 1, &sp, &args);
			put_conversion (&o, &sp, &args);
		}

		if (*p != '\0')
		{
			p++;
		}
	}

	va_end (args);

	if (cap > 0)
	{
		buf [ml_choose (o.len < cap, o.len, cap - 1)] = '\0';
	}

	return (int)o.len;
}

int snprintf (char *buf, size_t cap, const char *fmt, ...)
{
	va_list ap;
	int     n;

	va_start (ap, fmt);
	n = vsnprintf (buf, cap, fmt, ap);
	va_end (ap);
	return n;
}

/* ---- output and the environment ---------------------------------------- */

/* The page shows the text (the only output of a module). */
__attribute__ ((import_module ("env"), import_name ("console_write")))
void console_write (const char *text, int32_t len);

#define PRINT_MAX 2048

int printf (const char *fmt, ...)
{
	va_list ap;
	char    text [PRINT_MAX];
	int     n;

	va_start (ap, fmt);
	n = vsnprintf (text, PRINT_MAX, fmt, ap);
	va_end (ap);
	console_write (text, ml_choose (n < PRINT_MAX, n, PRINT_MAX - 1));
	return n;
}

int fflush (void *stream)
{
	(void)stream;
	return 0;
}

int setenv (const char *name, const char *value, int overwrite)
{
	(void)name;
	(void)value;
	(void)overwrite;
	return 0;
}
