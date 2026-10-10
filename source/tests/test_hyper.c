#include "test.h"

#include <matlog/session/results.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

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

static int32_t gives (const Run *run, uint64_t idx, const char *text)
{
	MlResultView v = ml_results_get (&run->r, idx);
	int32_t      ok
		= (v.kind == ML_RESULT_VALUE && ml_slice_equal (v.text, lit (text)));

	if (!ok)
	{
		printf (
			"  line %d: \"%.*s\" (kind %d), expected \"%s\"\n",
			(int)idx,
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

static void test_complex (void)
{
	static const char *const lines [] = {
		"3 + 4i",      "i*i",       "(1+2i)*(3-i)", "(1+2i)/(3-i)", "abs(3+4i)",
		"conj(3+4i)",  "re(3+4i)",  "im(3+4i)",     "(1+i)^2",      "(1+i)^8",
		"(1+i)^-2",    "i^2",       "i^3",          "-i",           "3 - i",
		"1/2 + 3/4*i", "-1/2 - i",  "i + 1.5",      "2i^2",         "i - i",
		"z = 2 - 3i",  "z*conj(z)", "z + 1",        "i/0",          "0^i"
	};

	Run run;

	run_start (&run, lines, ml_count (lines));
	CHECK (gives (&run, 0, "3 + 4i"));
	CHECK (gives (&run, 1, "-1"));
	CHECK (gives (&run, 2, "5 + 5i"));
	CHECK (gives (&run, 3, "1/10 + (7/10)i"));
	CHECK (gives (&run, 4, "5"));
	CHECK (gives (&run, 5, "3 - 4i"));
	CHECK (gives (&run, 6, "3"));
	CHECK (gives (&run, 7, "4"));
	CHECK (gives (&run, 8, "2i"));
	CHECK (gives (&run, 9, "16"));
	CHECK (gives (&run, 10, "-(1/2)i"));
	CHECK (gives (&run, 11, "-1"));
	CHECK (gives (&run, 12, "-i"));
	CHECK (gives (&run, 13, "-i"));
	CHECK (gives (&run, 14, "3 - i"));
	CHECK (gives (&run, 15, "1/2 + (3/4)i"));
	CHECK (gives (&run, 16, "-1/2 - i"));
	CHECK (gives (&run, 17, "3/2 + i"));
	CHECK (gives (&run, 18, "-2"));
	CHECK (gives (&run, 19, "0"));
	CHECK (gives (&run, 20, "2 - 3i"));
	CHECK (gives (&run, 21, "13"));
	CHECK (gives (&run, 22, "3 - 3i"));
	CHECK (fails (&run, 23, "div_zero"));
	CHECK (fails (&run, 24, "div_zero") || fails (&run, 24, "type"));
	run_end (&run);
}

static void test_approximate (void)
{
	static const char *const lines []
		= { "sqrt(-4)",       "sqrt(-2)",    "sqrt(i)",      "exp(i*pi)",
			"arg(1+i)",       "arg(-1)",     "arg(2)",       "(-8)^(1/3)",
			"polar(2, pi/2)", "(1+i)^(1/2)", "i^i",          "sin(i)",
			"ln(i)",          "abs(1+i)",    "cos(pi) + 0*i" };

	Run run;

	run_start (&run, lines, ml_count (lines));
	CHECK (gives (&run, 0, "2i"));
	CHECK (gives (&run, 1, "\xE2\x89\x88 1.41421356i"));
	CHECK (gives (&run, 2, "\xE2\x89\x88 0.707106781 + 0.707106781i"));
	CHECK (gives (&run, 3, "\xE2\x89\x88 -1"));
	CHECK (gives (&run, 4, "\xE2\x89\x88 0.785398163"));
	CHECK (gives (&run, 5, "\xE2\x89\x88 3.14159265"));
	CHECK (gives (&run, 6, "0"));
	CHECK (gives (&run, 7, "\xE2\x89\x88 1 + 1.73205081i"));
	CHECK (gives (&run, 8, "\xE2\x89\x88 2i"));
	CHECK (gives (&run, 9, "\xE2\x89\x88 1.09868411 + 0.455089861i"));
	CHECK (gives (&run, 10, "\xE2\x89\x88 0.207879576"));
	CHECK (gives (&run, 11, "\xE2\x89\x88 1.17520119i"));
	CHECK (gives (&run, 12, "\xE2\x89\x88 1.57079633i"));
	CHECK (gives (&run, 13, "\xE2\x89\x88 1.41421356"));
	CHECK (gives (&run, 14, "\xE2\x89\x88 -1"));
	run_end (&run);
}

static void test_quaternions (void)
{
	static const char *const lines [] = { "1 + 2i + 3j + 4k",
										  "i*j",
										  "j*i",
										  "j*k",
										  "k*i",
										  "(1+j)*(1-j)",
										  "quat(1,2,3,4)*quat(5,6,7,8)",
										  "quat(5,6,7,8)*quat(1,2,3,4)",
										  "abs(quat(1,2,3,4))",
										  "abs(quat(1,1,1,1))",
										  "conj(quat(1,2,3,4))",
										  "1/quat(1,2,3,4)",
										  "quat(1,2,3,4)/quat(1,2,3,4)",
										  "quat(1,2,3,4)^2",
										  "quat(1,2,3,4)^-1",
										  "re(quat(1,2,3,4))",
										  "im(quat(1,2,3,4))",
										  "rotate(1+k, [1,0,0])",
										  "rotate(1+k, [0,1,0])",
										  "rotate(1+k, [0,0,1])",
										  "rotate(quat(0,0,0,1), [1,0,0])",
										  "rotate(1+k, [1,0,0,0])",
										  "rotate(0, [1,0,0])",
										  "2*quat(1,0,0,1)",
										  "quat(1,0,0,0)",
										  "quat(1,2,0,0)",
										  "i + j",
										  "k*k",
										  "quat(0.5,1,1,1)" };

	Run run;

	run_start (&run, lines, ml_count (lines));
	CHECK (gives (&run, 0, "1 + 2i + 3j + 4k"));
	CHECK (gives (&run, 1, "k"));
	CHECK (gives (&run, 2, "-k"));
	CHECK (gives (&run, 3, "i"));
	CHECK (gives (&run, 4, "j"));
	CHECK (gives (&run, 5, "2"));
	CHECK (gives (&run, 6, "-60 + 12i + 30j + 24k"));
	CHECK (gives (&run, 7, "-60 + 20i + 14j + 32k"));
	CHECK (gives (&run, 8, "\xE2\x89\x88 5.47722558"));
	CHECK (gives (&run, 9, "2"));
	CHECK (gives (&run, 10, "1 - 2i - 3j - 4k"));
	CHECK (gives (&run, 11, "1/30 - (1/15)i - (1/10)j - (2/15)k"));
	CHECK (gives (&run, 12, "1"));
	CHECK (gives (&run, 13, "-28 + 4i + 6j + 8k"));
	CHECK (gives (&run, 14, "1/30 - (1/15)i - (1/10)j - (2/15)k"));
	CHECK (gives (&run, 15, "1"));
	CHECK (gives (&run, 16, "[[2], [3], [4]]"));

	/* 1 + k turns the x axis a quarter turn about z */
	CHECK (gives (&run, 17, "[[0], [1], [0]]"));
	CHECK (gives (&run, 18, "[[-1], [0], [0]]"));
	CHECK (gives (&run, 19, "[[0], [0], [1]]"));
	CHECK (gives (&run, 20, "[[-1], [0], [0]]"));
	CHECK (fails (&run, 21, "type"));
	CHECK (fails (&run, 22, "div_zero"));
	CHECK (gives (&run, 23, "2 + 2k"));
	CHECK (gives (&run, 24, "1"));
	CHECK (gives (&run, 25, "1 + 2i"));
	CHECK (gives (&run, 26, "i + j"));
	CHECK (gives (&run, 27, "-1"));
	CHECK (gives (&run, 28, "1/2 + i + j + k"));
	run_end (&run);
}

static void test_names (void)
{
	static const char *const lines []
		= { "k = 5",          "2k",    "f(i) = i + 1", "f(3)",
			"diff(k*x^2, x)", "i = 2", "i*i",          "type(1)" };

	Run run;

	/* a variable or a parameter hides the unit */
	run_start (&run, lines, ml_count (lines));
	CHECK (gives (&run, 1, "10"));
	CHECK (gives (&run, 3, "4"));
	CHECK (gives (&run, 4, "10x"));
	CHECK (gives (&run, 6, "4"));
	run_end (&run);
}

void run_hyper_tests (void);

void run_hyper_tests (void)
{
	test_complex ();
	test_approximate ();
	test_quaternions ();
	test_names ();
}
