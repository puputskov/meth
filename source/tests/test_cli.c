#include "test.h"

#include <matlog/cli/cli.h>
#include <matlog/math/value.h>
#include <matlog/parse/parser.h>
#include <matlog/platform/fs.h>

#include <string.h>

/* What a command printed, and how it ended. */
typedef struct Run
{
	int32_t code;
	MlStr   out;
	MlStr   err;
} Run;

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* Runs the command with up to eight arguments (NULL ends the list). */
static Run run (
	const char *a0,
	const char *a1,
	const char *a2,
	const char *a3,
	const char *a4,
	const char *a5,
	const char *input
)
{
	Run        r;
	MlSliceVec args;
	const char *list [6] = { a0, a1, a2, a3, a4, a5 };

	MlSliceVec_init (&args);
	ml_str_init (&r.out);
	ml_str_init (&r.err);

	for (uint64_t i = 0; i < 6 && list [i] != NULL; i++)
	{
		CHECK (MlSliceVec_push (&args, lit (list [i])) == ML_OK);
	}

	r.code = ml_cli_run (&args, lit (input), &r.out, &r.err);
	MlSliceVec_free (&args);
	return r;
}

static void run_free (Run *r)
{
	ml_str_free (&r->out);
	ml_str_free (&r->err);
}

static int32_t out_is (const Run *r, const char *expected)
{
	int32_t same = ml_slice_equal (ml_str_view (r->out), lit (expected));

	if (!same)
	{
		printf (
			"  stdout is:\n%s\n  expected:\n%s\n",
			ml_str_cstr (r->out),
			expected
		);
	}

	return same;
}

static int32_t err_has (const Run *r, const char *needle)
{
	return strstr (ml_str_cstr (r->err), needle) != NULL;
}

static void test_eval_text (void)
{
	Run r = run ("eval", "1/3 + 1/6", "sqrt(2)", "a = 3", "a^2 + 1", NULL, "");

	/* one result per expression, definitions flow to the next ones */
	CHECK (r.code == ML_EXIT_OK);
	CHECK (out_is (&r, "1/2\n\xE2\x89\x88 1.41421356\n3\n10\n"));
	CHECK (r.err.len == 0);
	run_free (&r);

	r = run ("eval", "1/0", "2 +", "1 + 1", NULL, NULL, "");
	CHECK (r.code == ML_EXIT_ERROR);
	CHECK (
		out_is (
			&r,
			"error: division by zero\n"
			"error: the expression ends too early\n"
			"2\n"
		)
	);
	CHECK (err_has (&r, "line 1: division by zero (div_zero)"));
	CHECK (err_has (&r, "line 2: the expression ends too early (parse_"));
	run_free (&r);

	/* matrices on one line, definitions without a value say ok */
	r = run ("eval", "inv([[1,2],[3,4]])", "f(x) = 2x", "f(4)", NULL, NULL, "");
	CHECK (r.code == ML_EXIT_OK);
	CHECK (out_is (&r, "[[-2, 1], [3/2, -1/2]]\nok\n8\n"));
	run_free (&r);

	/* standard input: one expression per line, blank lines skipped */
	r = run ("eval", "-", NULL, NULL, NULL, NULL, "2+2\n\n3*3\r\n");
	CHECK (r.code == ML_EXIT_OK);
	CHECK (out_is (&r, "4\n9\n"));
	run_free (&r);
}

static void test_eval_json (void)
{
	Run r = run (
		"eval",
		"--format",
		"json",
		"7/2",
		"1/0",
		"[[1,2],[3,4]]",
		""
	);

	CHECK (r.code == ML_EXIT_ERROR);
	CHECK (
		out_is (
			&r,
			"{\"line\":1,\"kind\":\"math\",\"domain\":\"algebra\","
			"\"input\":\"7/2\",\"status\":\"value\",\"type\":\"exact\","
			"\"value\":\"7/2\"}\n"
			"{\"line\":2,\"kind\":\"math\",\"domain\":\"algebra\","
			"\"input\":\"1/0\",\"status\":\"error\",\"error\":"
			"{\"code\":\"div_zero\",\"message\":\"division by zero\","
			"\"start\":0,\"len\":3}}\n"
			"{\"line\":3,\"kind\":\"math\",\"domain\":\"algebra\","
			"\"input\":\"[[1,2],[3,4]]\",\"status\":\"value\","
			"\"type\":\"matrix\",\"value\":\"[[1, 2], [3, 4]]\","
			"\"block\":[\"\xE2\x8E\xA1 1 2 \xE2\x8E\xA4\","
			"\"\xE2\x8E\xA3 3 4 \xE2\x8E\xA6\"]}\n"
		)
	);
	run_free (&r);

	/* --format=json is the same, an approximation says so */
	r = run ("eval", "--format=json", "pi", NULL, NULL, NULL, "");
	CHECK (strstr (ml_str_cstr (r.out), "\"type\":\"approx\"") != NULL);
	run_free (&r);

	/* quotes and backslashes in the input are escaped */
	r = run ("eval", "--format=json", "x\"y\\z", NULL, NULL, NULL, "");
	CHECK (strstr (ml_str_cstr (r.out), "\"input\":\"x\\\"y\\\\z\"") != NULL);
	CHECK (strstr (ml_str_cstr (r.out), "parse_bad_character") != NULL);
	run_free (&r);
}

static void test_session_files (void)
{
	MlSlice path = lit ("matlog_test_cli.tmp");
	Run     r;
	MlStr   file;

	CHECK (
		ml_fs_write_file_atomic (
			path,
			lit ("# constants\nm:algebra k = 4\nm:algebra g(x) = x + k\n")
		) == ML_OK
	);

	/* the definitions of the session are visible, the file is not changed */
	r = run ("eval", "--session", "matlog_test_cli.tmp", "g(1)", "k", NULL, "");
	CHECK (r.code == ML_EXIT_OK);
	CHECK (out_is (&r, "5\n4\n"));
	run_free (&r);
	ml_str_init (&file);
	CHECK (ml_fs_read_file (&file, path) == ML_OK);
	CHECK (strstr (ml_str_cstr (file), "g(1)") == NULL);
	ml_str_free (&file);

	/* --write keeps the new lines */
	r = run (
		"eval",
		"--session",
		"matlog_test_cli.tmp",
		"--write",
		"m = k * 2",
		NULL,
		""
	);
	CHECK (r.code == ML_EXIT_OK);
	CHECK (out_is (&r, "8\n"));
	run_free (&r);
	ml_str_init (&file);
	CHECK (ml_fs_read_file (&file, path) == ML_OK);
	CHECK (strstr (ml_str_cstr (file), "m:algebra m = k * 2") != NULL);
	ml_str_free (&file);

	/* run prints every line with its result */
	r = run ("run", "matlog_test_cli.tmp", NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_OK);
	CHECK (
		out_is (
			&r,
			"1: # constants\n"
			"2: k = 4  = 4\n"
			"3: g(x) = x + k\n"
			"4: m = k * 2  = 8\n"
		)
	);
	run_free (&r);
	r = run ("run", "--format", "json", "matlog_test_cli.tmp", NULL, NULL, "");
	CHECK (
		strstr (ml_str_cstr (r.out), "{\"line\":1,\"kind\":\"note\"")
		!= NULL
	);
	CHECK (strstr (ml_str_cstr (r.out), "\"status\":\"none\"}") != NULL);
	run_free (&r);

	CHECK (ml_fs_remove (path) == ML_OK);

	/* a missing file is an I/O error, with or without --write for eval */
	r = run ("run", "matlog_no_such_file.tmp", NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_IO);
	CHECK (err_has (&r, "cannot read matlog_no_such_file.tmp"));
	run_free (&r);
	r = run (
		"eval",
		"--session",
		"matlog_no_such_file.tmp",
		"1",
		NULL,
		NULL,
		""
	);
	CHECK (r.code == ML_EXIT_IO);
	run_free (&r);
}

static void test_lex_parse (void)
{
	Run r = run ("parse", "1+2x", NULL, NULL, NULL, NULL, "");

	CHECK (r.code == ML_EXIT_OK);
	CHECK (out_is (&r, "(+ 1 (* 2 x))\n"));
	run_free (&r);

	r = run ("parse", "--format", "json", "[1,2]", NULL, NULL, "");
	CHECK (out_is (&r, "{\"ok\":true,\"tree\":\"(list 1 2)\"}\n"));
	run_free (&r);

	r = run ("parse", "2 +", NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_ERROR);
	CHECK (out_is (&r, "error: the expression ends too early at 3+0\n"));
	run_free (&r);
	r = run ("parse", "--format=json", "(1", NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_ERROR);
	CHECK (
		out_is (
			&r,
			"{\"ok\":false,\"error\":{\"code\":\"parse_unexpected_end\","
			"\"message\":\"the expression ends too early\","
			"\"start\":2,\"len\":0}}\n"
		)
	);
	run_free (&r);

	/* the text from standard input */
	r = run ("parse", "-", NULL, NULL, NULL, NULL, "a = 1 + 1");
	CHECK (out_is (&r, "(= a (+ 1 1))\n"));
	run_free (&r);

	r = run ("lex", "x^2", NULL, NULL, NULL, NULL, "");
	CHECK (
		out_is (
			&r,
			"name\t0\t1\tx\n^\t1\t1\t^\nnumber\t2\t1\t2\nend\t3\t0\t\n"
		)
	);
	run_free (&r);
	r = run ("lex", "--format", "json", "$", NULL, NULL, "");
	CHECK (
		strstr (ml_str_cstr (r.out), "{\"kind\":\"error\",\"start\":0")
		!= NULL
	);
	run_free (&r);
}

static void test_usage (void)
{
	Run r = run ("eval", NULL, NULL, NULL, NULL, NULL, "");

	CHECK (r.code == ML_EXIT_USAGE);
	CHECK (err_has (&r, "eval needs an expression"));
	run_free (&r);

	r = run ("eval", "--bogus", "1", NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_USAGE);
	CHECK (err_has (&r, "unknown option"));
	run_free (&r);

	r = run ("eval", "--format", "xml", "1", NULL, NULL, "");
	CHECK (r.code == ML_EXIT_USAGE);
	run_free (&r);
	r = run ("eval", "--write", "1", NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_USAGE);
	CHECK (err_has (&r, "--write needs --session"));
	run_free (&r);
	r = run ("run", NULL, NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_USAGE);
	run_free (&r);
	r = run ("lex", NULL, NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_USAGE);
	run_free (&r);
	r = run ("frobnicate", NULL, NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_USAGE);
	CHECK (err_has (&r, "unknown command: frobnicate"));
	run_free (&r);
}

static void test_help (void)
{
	Run r = run ("help", NULL, NULL, NULL, NULL, NULL, "");

	CHECK (r.code == ML_EXIT_OK);
	CHECK (strstr (ml_str_cstr (r.out), "eval EXPR...") != NULL);
	CHECK (strstr (ml_str_cstr (r.out), "Exit codes") != NULL);
	run_free (&r);

	r = run ("help", "eval", NULL, NULL, NULL, NULL, "");
	CHECK (strstr (ml_str_cstr (r.out), "--session FILE") != NULL);
	run_free (&r);
	r = run ("help", "nothing", NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_USAGE);
	run_free (&r);

	r = run ("syntax", NULL, NULL, NULL, NULL, NULL, "");
	CHECK (strstr (ml_str_cstr (r.out), "Functions") != NULL);
	CHECK (strstr (ml_str_cstr (r.out), "det inv") != NULL);
	run_free (&r);
	r = run ("schema", NULL, NULL, NULL, NULL, NULL, "");
	CHECK (strstr (ml_str_cstr (r.out), "div_zero") != NULL);
	run_free (&r);
	r = run ("version", NULL, NULL, NULL, NULL, NULL, "");
	CHECK (out_is (&r, "matlog 0.3.0\n"));
	run_free (&r);
	r = run ("--version", NULL, NULL, NULL, NULL, NULL, "");
	CHECK (out_is (&r, "matlog 0.3.0\n"));
	run_free (&r);
	r = run ("commands", "--format", "json", NULL, NULL, NULL, "");
	CHECK (strstr (ml_str_cstr (r.out), "{\"name\":\"eval\"") != NULL);
	run_free (&r);

	/* what is a command */
	CHECK (ml_cli_is_command (lit ("eval")));
	CHECK (ml_cli_is_command (lit ("--version")));
	CHECK (!ml_cli_is_command (lit ("scratch.mlog")));
	CHECK (!ml_cli_is_command (lit ("")));
}

/* Every error code the library can give is in the schema text. */
static void test_codes_documented (void)
{
	Run r = run ("schema", NULL, NULL, NULL, NULL, NULL, "");

	for (uint32_t k = 1; k < ML_EVAL_KIND_COUNT; k++)
	{
		const char *code = ml_error_code ((MlErrorKind)k);

		CHECK (strcmp (code, "unknown") != 0);
		CHECK (strstr (ml_str_cstr (r.out), code) != NULL);
	}

	for (uint32_t k = 1; k < ML_PARSE_CODE_COUNT; k++)
	{
		const char *code = ml_parse_code_name ((MlParseCode)k);

		CHECK (strstr (ml_str_cstr (r.out), code) != NULL);
	}

	run_free (&r);
}

static void test_steps_command (void)
{
	MlSlice path = lit ("matlog_test_steps.tmp");
	Run     r    = run ("steps", "diff(x^2, x)", NULL, NULL, NULL, NULL, "");

	CHECK (r.code == ML_EXIT_OK);
	CHECK (
		strstr (ml_str_cstr (r.out), "Differentiate, rule by rule:") != NULL
	);
	CHECK (strstr (ml_str_cstr (r.out), "(power rule)") != NULL);
	run_free (&r);

	r = run ("steps", "--format", "json", "det([[1,2],[3,4]])", NULL, NULL, "");
	CHECK (r.code == ML_EXIT_OK);
	CHECK (
		strstr (
			ml_str_cstr (r.out),
			"{\"line\":1,\"input\":\"det([[1,2],[3,4]])\""
		) == ml_str_cstr (r.out)
	);
	CHECK (
		strstr (
			ml_str_cstr (r.out),
			"\"steps\":[\"Eliminate below the diagonal:\""
		) != NULL
	);
	run_free (&r);

	/* a line without steps */
	r = run ("steps", "1 + 1", NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_ERROR);
	CHECK (err_has (&r, "no steps for this line"));
	run_free (&r);

	/* a line of a file, the definitions above it are visible */
	CHECK (
		ml_fs_write_file_atomic (
			path,
			lit ("m:algebra a = 3\nm:algebra simplify(a*x + x)\nm:algebra 7\n")
		) == ML_OK
	);
	r = run ("steps", "matlog_test_steps.tmp:2", NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_OK);
	CHECK (strstr (ml_str_cstr (r.out), "Simplify:") != NULL);
	run_free (&r);
	r = run ("steps", "matlog_test_steps.tmp:3", NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_ERROR);
	run_free (&r);
	r = run ("steps", "matlog_no_such.tmp:1", NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_IO);
	run_free (&r);
	r = run ("steps", NULL, NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_USAGE);
	run_free (&r);
	CHECK (ml_fs_remove (path) == ML_OK);

	/* a symbolic result has the type expr */
	r = run ("eval", "--format", "json", "diff(x^3, x)", NULL, NULL, "");
	CHECK (
		strstr (
			ml_str_cstr (r.out),
			"\"type\":\"expr\",\"value\":\"3x\xC2\xB2\""
		) != NULL
	);
	run_free (&r);
}

static void test_domains (void)
{
	Run r = run (
		"eval",
		"--domain",
		"boolean",
		"p = a & b",
		"p | a",
		"a -> a",
		""
	);

	/* boolean lines share their definitions */
	CHECK (r.code == ML_EXIT_OK);
	CHECK (out_is (&r, "a \xE2\x88\xA7 b\na\ntrue\n"));
	run_free (&r);

	r = run (
		"eval",
		"--domain",
		"lambda",
		"succ = \\n f x. f (n f x)",
		"succ 1",
		NULL,
		""
	);
	CHECK (r.code == ML_EXIT_OK);
	CHECK (out_is (&r, "\xCE\xBBn f x. f (n f x)\n\xCE\xBB" "f x. f (f x)\n"));
	run_free (&r);

	/* the errors of the engines have their own codes */
	r = run ("eval", "--domain", "boolean", "a &", NULL, NULL, "");
	CHECK (r.code == ML_EXIT_ERROR);
	CHECK (err_has (&r, "(bool_expected_operand)"));
	run_free (&r);

	/* JSON: the type is the domain, a table is the block */
	r = run (
		"eval",
		"--format=json",
		"--domain",
		"boolean",
		"table a & b",
		NULL,
		""
	);
	CHECK (strstr (ml_str_cstr (r.out), "\"domain\":\"boolean\"") != NULL);
	CHECK (strstr (ml_str_cstr (r.out), "\"type\":\"boolean\"") != NULL);
	CHECK (strstr (ml_str_cstr (r.out), "\"block\":[\"a b") != NULL);
	run_free (&r);

	r = run (
		"eval",
		"--format=json",
		"--domain",
		"lambda",
		"trace (\\x. x) y",
		NULL,
		""
	);
	CHECK (strstr (ml_str_cstr (r.out), "\"type\":\"lambda\"") != NULL);
	CHECK (strstr (ml_str_cstr (r.out), "\"block\":[") != NULL);
	run_free (&r);

	/* steps: the beta steps and the truth table */
	r = run ("steps", "--domain", "lambda", "(\\x. x x) y", NULL, NULL, "");
	CHECK (r.code == ML_EXIT_OK);
	CHECK (strstr (ml_str_cstr (r.out), "\xE2\x86\x92 y y") != NULL);
	run_free (&r);

	r = run ("steps", "--domain", "boolean", "a | b", NULL, NULL, "");
	CHECK (r.code == ML_EXIT_OK);
	CHECK (strstr (ml_str_cstr (r.out), "1 1 \xE2\x94\x82 1") != NULL);
	run_free (&r);

	/* a bad domain is a usage error */
	r = run ("eval", "--domain", "nonsense", "1", NULL, NULL, "");
	CHECK (r.code == ML_EXIT_USAGE);
	run_free (&r);

	r = run ("eval", "--domain", NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_USAGE);
	run_free (&r);
}

static void test_vars (void)
{
	const char *arm   = "examples/robotics-two-link-arm.mlog";
	const char *line9 = "examples/robotics-two-link-arm.mlog:9";
	Run        r    = run ("vars", arm, NULL, NULL, NULL, NULL, "");

	CHECK (r.code == ML_EXIT_OK);
	CHECK (
		strstr (ml_str_cstr (r.out), " c2       exact        3/5\n") != NULL
	);
	CHECK (strstr (ml_str_cstr (r.out), "p        vector 2") != NULL);
	run_free (&r);

	/* a line number: what is defined after it, and what that line defined */
	r = run ("vars", "--format=json", line9, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_OK);
	CHECK (strstr (ml_str_cstr (r.out), "{\"name\":\"R2\"") != NULL);
	CHECK (strstr (ml_str_cstr (r.out), "\"fresh\":true}") != NULL);
	CHECK (strstr (ml_str_cstr (r.out), "\"name\":\"p\"") == NULL);
	run_free (&r);

	r = run ("vars", "no-such-file.mlog", NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_IO);
	run_free (&r);
	r = run ("vars", NULL, NULL, NULL, NULL, NULL, "");
	CHECK (r.code == ML_EXIT_USAGE);
	run_free (&r);
}

void run_cli_tests (void);

void run_cli_tests (void)
{
	test_steps_command ();
	test_eval_text ();
	test_eval_json ();
	test_domains ();
	test_vars ();
	test_session_files ();
	test_lex_parse ();
	test_usage ();
	test_help ();
	test_codes_documented ();
}
