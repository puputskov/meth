#include <matlog/ui/plot.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PLOT_MIN_WIDTH  8
#define PLOT_MIN_HEIGHT 4
#define BRAILLE_BASE    0x2800

ML_VEC_DEFINE (PlotCells, uint32_t)

/* The labels around the picture and the size of the area for the cells. */
typedef struct PlotFrame
{
	char     ytop [32];
	char     ybot [32];
	char     xlo [32];
	char     xhi [32];
	uint64_t gutter; /* columns of the y labels */
	uint64_t pw;     /* columns of cells */
	uint64_t ph;     /* rows of cells */
} PlotFrame;

static uint64_t umax (uint64_t a, uint64_t b)
{
	return ml_choose (a > b, a, b);
}

static int64_t iabs (int64_t a)
{
	return ml_choose (a < 0, -a, a);
}

static int64_t isign (int64_t a)
{
	return ml_choose (a < 0, -1, 1);
}

static int64_t round_to_i64 (double x)
{
	return (int64_t)floor (x + 0.5);
}

/* Sets the labels and the sizes; the size is the whole picture. */
static void frame_init (
	PlotFrame *f,
	double    xlo,
	double    xhi,
	double    ylo,
	double    yhi,
	uint64_t  width,
	uint64_t  height
)
{
	uint64_t top;
	uint64_t bot;

	snprintf (f->ytop, sizeof f->ytop, "%.4g", yhi);
	snprintf (f->ybot, sizeof f->ybot, "%.4g", ylo);
	snprintf (f->xlo, sizeof f->xlo, "%.4g", xlo);
	snprintf (f->xhi, sizeof f->xhi, "%.4g", xhi);

	top       = strlen (f->ytop);
	bot       = strlen (f->ybot);
	f->gutter = umax (top, bot);
	f->pw     = umax (width, f->gutter + 2 + PLOT_MIN_WIDTH) - f->gutter - 2;
	f->ph     = umax (height, PLOT_MIN_HEIGHT + 2) - 2;
}

/* The picture: the y labels and the left axis, the cells, the bottom axis. */
static MlResult frame_emit (
	MlStr           *out,
	const PlotFrame *f,
	const PlotCells *cells
)
{
	MlResult r   = ML_OK;
	uint64_t lxl = strlen (f->xlo);
	uint64_t lxh = strlen (f->xhi);

	for (uint64_t row = 0; row < f->ph && r == ML_OK; row++)
	{
		const char *label = "";

		if (row == 0)
		{
			label = f->ytop;
		}

		else if (row + 1 == f->ph)
		{
			label = f->ybot;
		}

		r = ml_str_appendf (out, "%*s │", (int32_t)f->gutter, label);

		for (uint64_t c = 0; c < f->pw && r == ML_OK; c++)
		{
			uint32_t cp = cells->data [row * f->pw + c];

			r = ml_str_append_cp (
				out,
				ml_choose (cp == BRAILLE_BASE, ' ', cp)
			);
		}

		if (r == ML_OK)
		{
			r = ml_str_append_cp (out, '\n');
		}
	}

	if (r == ML_OK)
	{
		r = ml_str_appendf (out, "%*s └", (int32_t)f->gutter, "");
	}

	for (uint64_t c = 0; c < f->pw && r == ML_OK; c++)
	{
		r = ml_str_append (out, ml_slice_cstr ("─"));
	}

	if (r == ML_OK)
	{
		r = ml_str_appendf (out, "\n%*s  %s", (int32_t)f->gutter, "", f->xlo);
	}

	if (r == ML_OK && lxl + lxh + 1 <= f->pw)
	{
		r = ml_str_appendf (
			out,
			"%*s%s",
			(int32_t)(f->pw - lxl - lxh),
			"",
			f->xhi
		);
	}

	return r;
}

static PlotCells cells_new (uint64_t count, uint32_t fill, MlResult *r)
{
	PlotCells cells;

	PlotCells_init (&cells);
	*r = PlotCells_reserve (&cells, count);

	for (uint64_t i = 0; i < count && *r == ML_OK; i++)
	{
		*r = PlotCells_push (&cells, fill);
	}

	return cells;
}

/* The range of the finite numbers of a list, widened when it is one point. */
static void range_of (const MlDoubleVec *v, uint64_t n, double *lo, double *hi)
{
	double a     = 0;
	double b     = 0;
	int32_t seen = 0;

	for (uint64_t i = 0; i < n; i++)
	{
		double x = v->data [i];

		if (isfinite (x))
		{
			if (!seen || x < a)
			{
				a = x;
			}

			if (!seen || x > b)
			{
				b = x;
			}

			seen = 1;
		}
	}

	if (!seen)
	{
		b = 1;
	}

	if (a == b)
	{
		a -= 1;
		b += 1;
	}

	*lo = a;
	*hi = b;
}

MlResult ml_plot_histogram (
	MlStr             *out,
	const MlDoubleVec *counts,
	double            lo,
	double            hi,
	uint64_t          width,
	uint64_t          height
)
{
	MlResult  r    = ML_OK;
	uint64_t  n    = counts->len;
	double    vmax = 0;
	PlotFrame f;
	PlotCells cells;

	for (uint64_t i = 0; i < n; i++)
	{
		double x = counts->data [i];

		if (isfinite (x) && x > vmax)
		{
			vmax = x;
		}
	}

	frame_init (&f, lo, hi, 0, vmax, width, height);
	cells = cells_new (f.pw * f.ph, ' ', &r);

	for (uint64_t c = 0; c < f.pw && r == ML_OK && n > 0; c++)
	{
		uint64_t first = c * n / f.pw;
		uint64_t last  = umax (first + 1, (c + 1) * n / f.pw);
		int32_t  gap   = (f.pw / n >= 3 && (c + 1) * n / f.pw != first);
		double   v     = 0;
		int64_t  level;

		for (uint64_t i = first; i < last; i++)
		{
			double x = counts->data [i];

			if (isfinite (x) && x > v)
			{
				v = x;
			}
		}

		level = ml_choose (
			vmax > 0 && !gap,
			round_to_i64 (v / vmax * (double)(f.ph * 8)),
			0
		);

		for (uint64_t row = 0; row < f.ph; row++)
		{
			int64_t fill = level - (int64_t)(f.ph - 1 - row) * 8;

			if (fill > 0)
			{
				cells.data [row * f.pw + c]
					= 0x2580 + (uint32_t)ml_choose (fill > 8, 8, fill);
			}
		}
	}

	if (r == ML_OK)
	{
		r = frame_emit (out, &f, &cells);
	}

	PlotCells_free (&cells);

	return r;
}

/* One braille dot at (x, y) in dot units, y down; outside is ignored. */
static void dot_set (PlotCells *cells, const PlotFrame *f, int64_t x, int64_t y)
{
	static const uint32_t bits [4] [2] =
	{
		{ 0x01, 0x08 },
		{ 0x02, 0x10 },
		{ 0x04, 0x20 },
		{ 0x40, 0x80 }
	};

	if (
		   x >= 0
		&& y >= 0
		&& x < (int64_t)(f->pw * 2)
		&& y < (int64_t)(f->ph * 4)
	)
	{
		cells->data [(uint64_t)(y / 4) * f->pw + (uint64_t)(x / 2)]
			|= bits [y % 4] [x % 2];
	}
}

/* The dots of the straight line from (x0, y0) to (x1, y1) (Bresenham). */
static void dot_line (
	PlotCells       *cells,
	const PlotFrame *f,
	int64_t         x0,
	int64_t         y0,
	int64_t         x1,
	int64_t         y1
)
{
	int64_t dx   = iabs (x1 - x0);
	int64_t dy   = -iabs (y1 - y0);
	int64_t sx   = isign (x1 - x0);
	int64_t sy   = isign (y1 - y0);
	int64_t err  = dx + dy;
	int32_t more = 1;

	while (more)
	{
		dot_set (cells, f, x0, y0);

		if (x0 == x1 && y0 == y1)
		{
			more = 0;
		}

		else
		{
			int64_t e2 = 2 * err;

			if (e2 >= dy)
			{
				err += dy;
				x0  += sx;
			}

			if (e2 <= dx)
			{
				err += dx;
				y0  += sy;
			}
		}
	}
}

MlResult ml_plot_xy (
	MlStr             *out,
	const MlDoubleVec *xs,
	const MlDoubleVec *ys,
	int32_t           connect,
	uint64_t          width,
	uint64_t          height
)
{
	MlResult  r = ML_OK;
	uint64_t  n = ml_choose (
		xs->len < ys->len,
		xs->len,
		ys->len
	);
	double    xlo;
	double    xhi;
	double    ylo;
	double    yhi;
	int64_t   px   = 0;
	int64_t   py   = 0;
	int32_t   have = 0;
	PlotFrame f;
	PlotCells cells;

	range_of (xs, n, &xlo, &xhi);
	range_of (ys, n, &ylo, &yhi);
	frame_init (&f, xlo, xhi, ylo, yhi, width, height);
	cells = cells_new (f.pw * f.ph, BRAILLE_BASE, &r);

	for (uint64_t i = 0; i < n && r == ML_OK; i++)
	{
		double x = xs->data [i];
		double y = ys->data [i];

		if (isfinite (x) && isfinite (y))
		{
			int64_t dx = round_to_i64 (
				(x - xlo) / (xhi - xlo) * (double)(f.pw * 2 - 1)
			);
			int64_t dy = (int64_t)(f.ph * 4) - 1 - round_to_i64 (
				(y - ylo) / (yhi - ylo) * (double)(f.ph * 4 - 1)
			);

			if (connect && have)
			{
				dot_line (&cells, &f, px, py, dx, dy);
			}

			else
			{
				dot_set (&cells, &f, dx, dy);
			}

			px   = dx;
			py   = dy;
			have = 1;
		}
	}

	if (r == ML_OK)
	{
		r = frame_emit (out, &f, &cells);
	}

	PlotCells_free (&cells);

	return r;
}
