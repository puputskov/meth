#include "test.h"

#include <matlog/session/results.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* The lines run in one session, so that the earlier ones define names. */
typedef struct Run
{
	MlSession s;
	MlResults r;
} Run;

static void run_start (Run *run, const char *const *lines, uint64_t count)
{
	ml_session_init (&run->s);
	ml_results_init (&run->r);

	for (uint64_t i = 0; i < count; i++)
	{
		CHECK (
			ml_session_append (
				&run->s,
				ML_LINE_MATH,
				ML_DOMAIN_ALGEBRA,
				lit (lines [i])
			)
			== ML_OK
		);
	}

	CHECK (ml_results_update (&run->r, &run->s, 0) == ML_OK);
}

static void run_end (Run *run)
{
	ml_results_free (&run->r);
	ml_session_free (&run->s);
}

/* 1 when line idx gives the value text. */
static int32_t gives (const Run *run, uint64_t idx, const char *text)
{
	MlResultView v = ml_results_get (&run->r, idx);
	int32_t      ok
		= (v.kind == ML_RESULT_VALUE && ml_slice_equal (v.text, lit (text)));

	if (!ok)
	{
		printf (
			"  line %d \"%s\": \"%.*s\" (kind %d), expected \"%s\"\n",
			(int)idx,
			ml_str_cstr (
				ml_session_get (&run->s, idx).kind == ML_LINE_NONE
					? run->s.lines.data [0].text
					: run->s.lines.data [idx].text
			),
			(int)v.text.len,
			v.text.data,
			(int)v.kind,
			text
		);
	}

	return ok;
}

static int32_t fails (const Run *run, uint64_t idx, const char *code)
{
	MlResultView v = ml_results_get (&run->r, idx);

	return (v.kind == ML_RESULT_ERROR && ml_slice_equal (v.code, lit (code)));
}

static void test_descriptive (void)
{
	static const char *const lines [] = {
		"d = [2, 4, 4, 4, 5, 5, 7, 9]",
		"sum(d)", /* 1 */
		"count(d)",
		"mean(d)",
		"median(d)",
		"mode(d)", /* 5 */
		"var(d)",
		"pvar(d)",
		"pstd(d)",
		"std([1, 2, 3, 4, 5])",
		"quantile(d, 1/4)", /* 10 */
		"quantile(d, 0)",
		"quantile(d, 1)",
		"iqr(d)",
		"min(d)",
		"max(d)", /* 15 */
		"max(3, 9, 4)",
		"kurt(d)",
		"cumsum([1, 2, 3])",
		"sort([3, 1, 2])",
		"sort([[3, 1, 2]])", /* 20 */
		"median([1, 2, 3])",
		"mode([3, 1, 2])",
		"mean([[1, 2, 3, 4]])",
		"sum(range(1, 100))" /* 24: lists have no limit of 64 */
	};

	Run run;

	run_start (&run, lines, ml_count (lines));
	CHECK (gives (&run, 1, "40"));
	CHECK (gives (&run, 2, "8"));
	CHECK (gives (&run, 3, "5"));
	CHECK (gives (&run, 4, "9/2"));
	CHECK (gives (&run, 5, "4"));
	CHECK (gives (&run, 6, "32/7"));
	CHECK (gives (&run, 7, "4"));
	CHECK (gives (&run, 8, "2"));
	CHECK (gives (&run, 9, "\xE2\x89\x88 1.58113883"));
	CHECK (gives (&run, 10, "4"));
	CHECK (gives (&run, 11, "2"));
	CHECK (gives (&run, 12, "9"));
	CHECK (gives (&run, 13, "3/2"));
	CHECK (gives (&run, 14, "2"));
	CHECK (gives (&run, 15, "9"));
	CHECK (gives (&run, 16, "9"));
	CHECK (gives (&run, 17, "-7/32"));
	CHECK (gives (&run, 18, "[[1], [3], [6]]"));
	CHECK (gives (&run, 19, "[[1], [2], [3]]"));
	CHECK (gives (&run, 20, "[[1, 2, 3]]"));
	CHECK (gives (&run, 21, "2"));
	CHECK (gives (&run, 22, "1"));
	CHECK (gives (&run, 23, "5/2"));
	CHECK (gives (&run, 24, "5050"));
	run_end (&run);
}

static void test_errors (void)
{
	static const char *const lines [] = { "mean(5)",
										  "var([1])",
										  "mean([[1, 2], [3, 4]])",
										  "quantile([1, 2], 3/2)",
										  "quantile([1, 2], sqrt(1/2))",
										  "corr([1, 1, 1], [1, 2, 3])",
										  "cov([1, 2], [1, 2, 3])",
										  "histogram([1, 2], 0)",
										  "range(1, 5, 0)",
										  "range(5, 1)",
										  "binomial(3, 5)",
										  "binomial(2.5, 1)",
										  "mean()",
										  "median(sqrt(2))" };

	Run run;

	run_start (&run, lines, ml_count (lines));
	CHECK (fails (&run, 0, "type"));
	CHECK (fails (&run, 1, "domain"));
	CHECK (fails (&run, 2, "size"));
	CHECK (fails (&run, 3, "domain"));
	CHECK (fails (&run, 4, "type"));
	CHECK (fails (&run, 5, "div_zero"));
	CHECK (fails (&run, 6, "size"));
	CHECK (fails (&run, 7, "domain"));
	CHECK (fails (&run, 8, "domain"));
	CHECK (fails (&run, 9, "domain"));
	CHECK (fails (&run, 10, "not_integer"));
	CHECK (fails (&run, 11, "not_integer"));
	CHECK (fails (&run, 12, "arity"));
	CHECK (fails (&run, 13, "type"));
	run_end (&run);
}

static void test_pairs_and_fits (void)
{
	static const char *const lines []
		= { "x = [1, 2, 3, 4]",
			"y = [2, 4, 5, 9]",
			"cov(x, y)", /* 2 */
			"corr(x, y)",
			"rsq(x, y)",
			"linfit(x, y)", /* 5 */
			"polyfit(x, y, 1)",
			"polyfit(x, y, 2)",
			"polyfit(x, y, 3)",
			"lstsq([[1, 0], [1, 1], [1, 2]], [1, 3, 4])",
			"corr([1, 2, 3], [3, 2, 1])", /* 10 */
			"corr([1, 2, 3], [2, 4, 6])",
			"X = [[1, 0], [1, 1], [1, 2], [1, 3]]",
			"lstsq(X, [1, 3, 4, 8])",
			"range(1, 5)",
			"range(0, 1, 1/4)", /* 15 */
			"range(5, 1, -2)",
			"histogram([1, 2, 3, 4, 5, 6], 3)",
			"histogram([2, 2, 2], 2)" };

	Run run;

	run_start (&run, lines, ml_count (lines));
	CHECK (gives (&run, 2, "11/3"));
	CHECK (gives (&run, 3, "\xE2\x89\x88 0.964763821"));
	CHECK (gives (&run, 4, "121/130"));
	CHECK (gives (&run, 5, "[[-1/2], [11/5]]"));

	/* a line is a degree 1 polynomial: the same coefficients */
	CHECK (gives (&run, 6, "[[-1/2], [11/5]]"));
	CHECK (gives (&run, 7, "[[2], [-3/10], [1/2]]"));

	/* four points fit a cubic exactly */
	CHECK (gives (&run, 8, "[[-5], [65/6], [-9/2], [2/3]]"));
	CHECK (gives (&run, 9, "[[7/6], [3/2]]"));
	CHECK (gives (&run, 10, "-1"));
	CHECK (gives (&run, 11, "1"));
	CHECK (gives (&run, 13, "[[7/10], [11/5]]"));
	CHECK (gives (&run, 14, "[1, 2, 3, 4, 5]"));
	CHECK (gives (&run, 15, "[0, 1/4, 1/2, 3/4, 1]"));
	CHECK (gives (&run, 16, "[5, 3, 1]"));
	CHECK (gives (&run, 17, "[[2], [2], [2]]"));
	CHECK (gives (&run, 18, "[[3], [0]]"));
	run_end (&run);
}

static void test_counting_and_distributions (void)
{
	static const char *const lines [] = { "binomial(10, 3)",
										  "binomial(5, 0)",
										  "perm(5, 2)",
										  "binomial(52, 5)",
										  "binomial_pdf(10, 3, 1/2)", /* 4 */
										  "binomial_pdf(4, 4, 1)",
										  "binomial_pdf(10, 3, cos(pi)^2/2)",
										  "poisson_pdf(2, 3)",
										  "normal_pdf(0)",
										  "normal_cdf(0)", /* 9 */
										  "normal_cdf(1.96)",
										  "normal_cdf(110, 100, 10)",
										  "normal_inv(0.975)",
										  "normal_inv(1/2)",
										  "normal_inv(0)", /* 14 */
										  "normal_pdf(0, 0, 0)",
										  "zscore(7, 5, 2)",
										  "zscore(80, 70, 15)" };

	Run run;

	run_start (&run, lines, ml_count (lines));
	CHECK (gives (&run, 0, "120"));
	CHECK (gives (&run, 1, "1"));
	CHECK (gives (&run, 2, "20"));
	CHECK (gives (&run, 3, "2598960"));
	CHECK (gives (&run, 4, "15/128"));
	CHECK (gives (&run, 5, "1"));
	CHECK (gives (&run, 6, "\xE2\x89\x88 0.1171875"));
	CHECK (gives (&run, 7, "\xE2\x89\x88 0.224041808"));
	CHECK (gives (&run, 8, "\xE2\x89\x88 0.39894228"));
	CHECK (gives (&run, 9, "\xE2\x89\x88 0.5"));
	CHECK (gives (&run, 10, "\xE2\x89\x88 0.975002105"));
	CHECK (gives (&run, 11, "\xE2\x89\x88 0.841344746"));
	CHECK (gives (&run, 12, "\xE2\x89\x88 1.95996398"));
	CHECK (fails (&run, 14, "domain"));
	CHECK (fails (&run, 15, "domain"));
	CHECK (gives (&run, 16, "1"));
	CHECK (gives (&run, 17, "2/3"));
	run_end (&run);
}

void run_stats_tests (void);

void run_stats_tests (void)
{
	test_descriptive ();
	test_errors ();
	test_pairs_and_fits ();
	test_counting_and_distributions ();
}
