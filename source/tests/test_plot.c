#include "test.h"

#include <matlog/ui/plot.h>

#include <math.h>
#include <string.h>

static MlDoubleVec vec (const double *v, uint64_t n)
{
	MlDoubleVec d;

	MlDoubleVec_init (&d);

	for (uint64_t i = 0; i < n; i++)
	{
		CHECK (MlDoubleVec_push (&d, v [i]) == ML_OK);
	}

	return d;
}

/* The number of lines of a picture. */
static uint64_t line_count (MlStr s)
{
	uint64_t n = 1;

	for (uint64_t i = 0; i < s.len; i++)
	{
		if (s.data [i] == '\n')
		{
			n++;
		}
	}

	return n;
}

static int32_t has (MlStr s, const char *text)
{
	return strstr (ml_str_cstr (s), text) != NULL;
}

static void test_histogram (void)
{
	const double counts [] = { 1, 4, 8, 4, 1 };
	MlDoubleVec  c         = vec (counts, ml_count (counts));
	MlStr        out;

	ml_str_init (&out);
	CHECK (ml_plot_histogram (&out, &c, 1, 9, 20, 6) == ML_OK);

	/* the whole size: 4 rows of cells, the axis and the labels */
	CHECK (line_count (out) == 6);

	/* the largest bar fills its column to the top, the axis and labels */
	CHECK (has (out, "8 │"));
	CHECK (has (out, "0 │"));
	CHECK (has (out, "█"));
	CHECK (has (out, "└"));
	CHECK (has (out, "1"));
	CHECK (has (out, "9"));

	ml_str_free (&out);
	MlDoubleVec_free (&c);
}

static void test_histogram_empty (void)
{
	MlDoubleVec c;
	MlStr       out;

	MlDoubleVec_init (&c);
	ml_str_init (&out);
	CHECK (ml_plot_histogram (&out, &c, 0, 1, 20, 6) == ML_OK);
	CHECK (line_count (out) == 6);
	CHECK (!has (out, "█"));

	ml_str_free (&out);
}

static void test_scatter (void)
{
	const double x [] = { 0, 1, 2, 3 };
	const double y [] = { 0, 1, 2, 3 };
	MlDoubleVec  xs   = vec (x, ml_count (x));
	MlDoubleVec  ys   = vec (y, ml_count (y));
	MlStr        out;

	ml_str_init (&out);
	CHECK (ml_plot_xy (&out, &xs, &ys, 0, 24, 8) == ML_OK);
	CHECK (line_count (out) == 8);
	CHECK (has (out, "3 │"));
	CHECK (has (out, "0 │"));

	/* the bottom left point is the dot in the lower left of its cell, and
	   the top right one in the upper right of its cell */
	CHECK (has (out, "⡀"));
	CHECK (has (out, "⠈"));

	ml_str_free (&out);
	MlDoubleVec_free (&xs);
	MlDoubleVec_free (&ys);
}

static void test_line_connects (void)
{
	const double x [] = { 0, 10 };
	const double y [] = { 0, 10 };
	MlDoubleVec  xs   = vec (x, ml_count (x));
	MlDoubleVec  ys   = vec (y, ml_count (y));
	MlStr        dots;
	MlStr        line;

	ml_str_init (&dots);
	ml_str_init (&line);
	CHECK (ml_plot_xy (&dots, &xs, &ys, 0, 24, 8) == ML_OK);
	CHECK (ml_plot_xy (&line, &xs, &ys, 1, 24, 8) == ML_OK);

	/* a line has more dots than its two end points */
	CHECK (line.len > dots.len);

	ml_str_free (&dots);
	ml_str_free (&line);
	MlDoubleVec_free (&xs);
	MlDoubleVec_free (&ys);
}

static void test_bad_numbers (void)
{
	const double x [] = { 0, INFINITY, 2 };
	const double y [] = { 0, 1, NAN };
	MlDoubleVec  xs   = vec (x, ml_count (x));
	MlDoubleVec  ys   = vec (y, ml_count (y));
	MlStr        out;

	ml_str_init (&out);
	CHECK (ml_plot_xy (&out, &xs, &ys, 1, 24, 8) == ML_OK);
	CHECK (line_count (out) == 8);

	ml_str_free (&out);
	MlDoubleVec_free (&xs);
	MlDoubleVec_free (&ys);
}

static void test_tiny_size (void)
{
	const double x [] = { 1, 2 };
	MlDoubleVec  xs   = vec (x, ml_count (x));
	MlStr        out;

	ml_str_init (&out);
	CHECK (ml_plot_xy (&out, &xs, &xs, 1, 1, 1) == ML_OK);
	CHECK (line_count (out) == 6);

	ml_str_free (&out);
	MlDoubleVec_free (&xs);
}

void run_plot_tests (void)
{
	test_histogram ();
	test_histogram_empty ();
	test_scatter ();
	test_line_connects ();
	test_bad_numbers ();
	test_tiny_size ();
}
