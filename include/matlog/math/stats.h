#ifndef MATLOG_MATH_STATS_H
#define MATLOG_MATH_STATS_H

#include <matlog/math/value.h>

/*
 * The statistics functions of the algebra domain (plan section 4.22). Data is
 * a vector: [1, 2, 3] or a row [[1, 2, 3]] (a matrix has at most ML_MAT_MAX_DIM
 * rows or columns), or a list: range (1, 3), list (1, 2, 3), up to
 * ML_LIST_MAX numbers. The items of a list must be exact. Everything is exact
 * while the data is exact:
 *
 *	sum mean median mode        the usual; mode is the smallest of the most
 *	                            frequent values
 *	var std                     the sample variance and standard deviation
 *	                            (divided by n - 1), pvar pstd the population
 *	                            ones (divided by n)
 *	min max                     of a vector too, min ([3, 1, 2])
 *	quantile (v, p)            linear between the ordered values, p from 0
 *	                            to 1; iqr (v) is quantile 3/4 less quantile 1/4
 *	cov (x, y)  corr (x, y)     sample covariance, correlation coefficient
 *	skew  kurt                  skewness (approximate) and excess kurtosis
 *	count cumsum sort           the length, running sums, the data in order
 *	histogram (v, k)            how many values are in each of k equal bins
 *	range (a, b [, step])       the list a, a + step, ... up to b
 *	prod (v)                    the product of the items
 *	linfit (x, y)               [a, b] so that y is about a + b x
 *	rsq (x, y)                  the coefficient of determination
 *	polyfit (x, y, n)           the coefficients c0 ... cn of the degree n
 *	                            polynomial that fits best
 *	lstsq (A, b)                the x that makes A x closest to b
 *	binomial (n, k)  perm (n, k) choose and arrange
 *	binomial_pdf (n, k, p)      exact for an exact p
 *	poisson_pdf (k, l)
 *	normal_pdf normal_cdf normal_inv (x [, mu, sigma])
 *	zscore (x, mu, sigma)
 *	hist (v, k)                 the histogram of histogram (v, k) as a plot
 *	scatter (x, y)  plot (x, y)  the points, and the points joined by a line,
 *	                            as a plot (see plotdata.h and ui/plot.h)
 *
 * A standard deviation is exact when the variance is a perfect square, else
 * approximate. Errors are the ones of the evaluator: ML_EVAL_TYPE for what is
 * not a vector, ML_EVAL_DOMAIN for too little data or a probability outside
 * 0..1, ML_EVAL_DIV_ZERO when the spread is zero.
 */

/* name, fewest and most arguments */
#define ML_STAT_LIST(X) \
	X (SUM,          "sum",          1, 1) \
	X (LENGTH,       "count",        1, 1) \
	X (MEAN,         "mean",         1, 1) \
	X (MEDIAN,       "median",       1, 1) \
	X (MODE,         "mode",         1, 1) \
	X (VAR,          "var",          1, 1) \
	X (STD,          "std",          1, 1) \
	X (PVAR,         "pvar",         1, 1) \
	X (PSTD,         "pstd",         1, 1) \
	X (QUANTILE,     "quantile",     2, 2) \
	X (IQR,          "iqr",          1, 1) \
	X (COV,          "cov",          2, 2) \
	X (CORR,         "corr",         2, 2) \
	X (SKEW,         "skew",         1, 1) \
	X (KURT,         "kurt",         1, 1) \
	X (CUMSUM,       "cumsum",       1, 1) \
	X (SORT,         "sort",         1, 1) \
	X (HISTOGRAM,    "histogram",    2, 2) \
	X (RANGE,        "range",        2, 3) \
	X (LINFIT,       "linfit",       2, 2) \
	X (RSQ,          "rsq",          2, 2) \
	X (POLYFIT,      "polyfit",      3, 3) \
	X (LSTSQ,        "lstsq",        2, 2) \
	X (BINOMIAL,     "binomial",     2, 2) \
	X (PERM,         "perm",         2, 2) \
	X (BINOMIAL_PDF, "binomial_pdf", 3, 3) \
	X (POISSON_PDF,  "poisson_pdf",  2, 2) \
	X (NORMAL_PDF,   "normal_pdf",   1, 3) \
	X (NORMAL_CDF,   "normal_cdf",   1, 3) \
	X (NORMAL_INV,   "normal_inv",   1, 3) \
	X (ZSCORE,       "zscore",       3, 3) \
	X (HIST,         "hist",         2, 2) \
	X (SCATTER,      "scatter",      2, 2) \
	X (PLOT,         "plot",         2, 2) \
	X (PROD,         "prod",         1, 1)

typedef enum MlStat
{
#define ML_STAT_ENUM(id, name, lo, hi) ML_STAT_##id,
	ML_STAT_LIST (ML_STAT_ENUM)
#undef ML_STAT_ENUM
	ML_STAT_TOTAL
} MlStat;

/*
 * The result of a statistics function for the arguments (already evaluated);
 * which is one of the MlStat values. Also the minimum and the maximum of a
 * vector, for min and max with one argument.
 */
MlValue ml_stats_apply (
	MlArena       *arena,
	MlStat        which,
	const MlValue *args,
	uint64_t      argc
);

/* The smallest (is_max 0) or the largest of the numbers of a vector. */
MlValue ml_stats_extreme (MlArena *arena, MlValue v, int32_t is_max);

#endif /* MATLOG_MATH_STATS_H */
