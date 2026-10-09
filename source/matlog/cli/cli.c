#include <matlog/cli/cli.h>

#include <matlog/parse/parser.h>
#include <matlog/session/mlog.h>
#include <matlog/session/results.h>
#include <matlog/session/steps.h>

#define VERSION "matlog 0.3.0"

/* ---- text of the help ---- */

static const char *const help_overview =
	"matlog: an exact math shell. Without a command it opens the "
	"interactive editor.\n"
	"\n"
	"usage: matlog [FILE.mlog]\n"
	"       matlog COMMAND [OPTIONS] [ARGUMENTS]\n"
	"\n"
	"commands:\n"
	"  eval EXPR...   evaluate expressions, one result each\n"
	"  run FILE       evaluate every line of a session file\n"
	"  lex TEXT       the tokens of a text\n"
	"  parse TEXT     the syntax tree of a text\n"
	"  steps EXPR     the steps of an expression, or of FILE:LINE\n"
	"  help [CMD]     this text, or the help of one command\n"
	"  syntax         the syntax, functions and domains\n"
	"  schema         the shape of the JSON output\n"
	"  commands       the list of commands (--format json for tools)\n"
	"  version        the version\n"
	"\n"
	"options:\n"
	"  --format text|json   the output form (default text)\n"
	"  --session FILE       eval: the session whose definitions are visible\n"
	"  --write              eval: keep the new lines in the session file\n"
	"\n"
	"An argument - reads standard input. Exit codes: 0 success, 1 an "
	"expression\n"
	"was an error, 2 usage error, 3 file error.\n";

static const char *const help_eval =
	"matlog eval [--session FILE] [--write] [--format text|json] EXPR...\n"
	"\n"
	"Evaluates each EXPR in order, as lines of a session: a = 3 defines a "
	"for\n"
	"the expressions after it. With --session the definitions of that file "
	"are\n"
	"visible; with --write the new lines are appended to it. An EXPR of - "
	"takes\n"
	"one expression per line from standard input.\n"
	"\n"
	"examples:\n"
	"  matlog eval \"1/3 + 1/6\" \"sqrt(2)\"\n"
	"  matlog eval \"a = 3\" \"a^2 + 1\" --format json\n"
	"  matlog eval \"det([[1,2],[3,4]])\"\n";

static const char *const help_run =
	"matlog run FILE [--format text|json]\n"
	"\n"
	"Evaluates a session file and prints every line with its result. "
	"Includes\n"
	"are followed. Exit code 1 when any line is an error.\n";

static const char *const help_lex =
	"matlog lex TEXT [--format text|json]\n"
	"\n"
	"Prints the tokens of TEXT: kind, byte offset, byte length and text.\n";

static const char *const help_parse =
	"matlog parse TEXT [--format text|json]\n"
	"\n"
	"Prints the syntax tree of TEXT, e.g. (+ 1 (* 2 x)). Exit code 1 and the "
	"place\n"
	"of the problem when TEXT is not a valid statement.\n";

static const char *const help_steps =
	"matlog steps [--session FILE] [--format text|json] EXPR\n"
	"matlog steps [--format text|json] FILE:LINE\n"
	"\n"
	"Prints the steps of one line: the row operations of a matrix "
	"calculation, the\n"
	"rule used for each part of a derivative or an integral, the changes "
	"made by simplify.\n"
	"Exit code 1 when the line has no steps.\n";

static const char *const syntax_text =
	"Numbers      12  3.14  .5   exact fractions: 1/3 + 1/6 is 1/2\n"
	"Operators    + - * / ^ (right associative)  ! (factorial)  "
	"sqrt(x) or the root sign\n"
	"             -2^2 is -4; implicit product: 2x  2(x+1)  (a)(b)  2pi\n"
	"Unicode      the same as ASCII: x\xC2\xB2 for x^2, \xE2\x88\x9A for "
	"sqrt, \xCF\x80 for pi,\n"
	"             \xC3\x97 \xC2\xB7 for *, \xC3\xB7 for /, \xE2\x88\x92 "
	"for -\n"
	"Names        letters, digits and _, whole words: xy is one name\n"
	"Statements   expression | name = expression | f(x, y) = expression\n"
	"Constants    pi  e   (approximate)\n"
	"Functions    sqrt abs sin cos tan asin acos atan ln log exp floor "
	"ceil round\n"
	"             min max gcd lcm\n"
	"Matrices     [[1,2],[3,4]] rows; [1,2,3] a column vector; A^T A^-1 "
	"A*B\n"
	"             det inv transpose trace rank rref solve identity dot "
	"cross norm\n"
	"Symbolic     simplify(f) expand(f) subs(f, x, v) diff(f, x [, n]) "
	"integrate(f, x [, a, b])\n"
	"             names are symbols inside these: diff(x^3, x) is 3x\xC2\xB2; "
	"results can be stored\n"
	"Units        5 km + 300 m, 12 N * 3 m; a to b converts: 3 ft to m;\n"
	"             dim(x) is the dimension of x as one SI unit\n"
	"Results      exact (1/2) or approximate (\xE2\x89\x88 1.41421356); "
	"errors have a code\n"
	"Domains      algebra (evaluated now); equations matrix vector lambda "
	"pga2 pga3\n"
	"             boolean are stored but not evaluated yet\n";

static const char *const schema_text =
	"Every result is one JSON object on one line (JSON Lines).\n"
	"\n"
	"eval, run:\n"
	"  {\"line\":N, \"kind\":\"math|note|include|blank|unknown\", "
	"\"domain\":\"algebra\",\n"
	"   \"input\":\"text\", \"status\":\"value|error|none\",\n"
	"   \"type\":\"exact|approx|matrix|expr|quantity\", \"value\":\"7/2\", "
	"\"block\":[\"row\",...],\n"
	"   \"error\":{\"code\":\"div_zero\", \"message\":\"...\", "
	"\"start\":N, \"len\":N}}\n"
	"  type, value and block only with status value (block only for "
	"matrices);\n"
	"  error only with status error; domain only for kind math.\n"
	"lex:    {\"kind\":\"number\", \"start\":N, \"len\":N, "
	"\"text\":\"12\"}\n"
	"parse:  {\"ok\":true, \"tree\":\"(+ 1 2)\"} or {\"ok\":false, "
	"\"error\":{...}}\n"
	"\n"
	"error codes: syntax div_zero zero_pow_zero not_real domain "
	"not_integer\n"
	"  undefined_name not_function not_value arity too_large depth no_file\n"
	"  include_loop size type not_exact cannot_integrate not_symbolic\n"
	"  singular dimension_mismatch unknown_unit no_limit cannot_solve\n"
	"  no_memory, and for "
	"text that does\n"
	"  not parse parse_unexpected_end parse_unexpected_token "
	"parse_expected_expr\n"
	"  parse_bad_character parse_bad_superscript parse_assign_target\n"
	"  parse_duplicate_param parse_too_deep parse_no_memory\n";

typedef struct CommandInfo
{
	const char *name;
	const char *summary;
} CommandInfo;

static const CommandInfo commands [] =
{
	{ .name = "eval", .summary = "evaluate expressions" },
	{ .name = "run", .summary = "evaluate a session file" },
	{ .name = "lex", .summary = "tokens of a text" },
	{ .name = "parse", .summary = "syntax tree of a text" },
	{ .name = "steps", .summary = "steps of an expression or a line" },
	{ .name = "help", .summary = "usage" },
	{ .name = "syntax", .summary = "the syntax and functions" },
	{ .name = "schema", .summary = "shape of the JSON output" },
	{ .name = "commands", .summary = "list of commands" },
	{ .name = "version", .summary = "the version" }
};

/* ---- small helpers ---- */

static MlResult add (MlStr *out, const char *text)
{
	return ml_str_append (out, ml_slice_cstr (text));
}

static int32_t is (MlSlice a, const char *b)
{
	return ml_slice_equal (a, ml_slice_cstr (b));
}

static int32_t starts_with (MlSlice a, const char *prefix)
{
	MlSlice p = ml_slice_cstr (prefix);

	return a.len >= p.len && ml_slice_equal (ml_slice_sub (a, 0, p.len), p);
}

int32_t ml_cli_is_command (MlSlice first)
{
	int32_t found = (
		   is (first, "--version")
		|| is (first, "--help")
		|| is (first, "-h")
	);

	for (uint64_t i = 0; i < ml_count (commands); i++)
	{
		found = found || is (first, commands [i].name);
	}

	return found;
}

int32_t ml_cli_needs_stdin (const MlSliceVec *args)
{
	int32_t needs = 0;

	for (uint64_t i = 0; i < args->len; i++)
	{
		needs = needs || is (args->data [i], "-");
	}

	return needs;
}

/* A JSON string literal: quotes, escapes; UTF-8 passes through. */
static MlResult json_string (MlStr *out, MlSlice text)
{
	MlResult r = add (out, "\"");

	for (uint64_t i = 0; i < text.len && r == ML_OK; i++)
	{
		uint8_t c = (uint8_t)text.data [i];

		if (c == '"' || c == '\\')
		{
			r = ml_str_appendf (out, "\\%c", (int32_t)c);
		}

		else if (c == '\n')
		{
			r = add (out, "\\n");
		}

		else if (c == '\r')
		{
			r = add (out, "\\r");
		}

		else if (c == '\t')
		{
			r = add (out, "\\t");
		}

		else if (c < 0x20)
		{
			r = ml_str_appendf (out, "\\u%04x", (uint32_t)c);
		}

		else
		{
			r = ml_str_append (out, ml_slice_sub (text, i, 1));
		}
	}

	if (r == ML_OK)
	{
		r = add (out, "\"");
	}

	return r;
}

/* ---- options ---- */

typedef struct Options
{
	int32_t    json;
	MlSlice    session;
	int32_t    write;
	MlSliceVec rest;    /* the arguments that are not options */
	const char *problem; /* NULL, or what is wrong with the arguments */
} Options;

static void options_free (Options *o)
{
	MlSliceVec_free (&o->rest);
}

/* Reads the options of args from index first on. */
static MlResult options_read (
	Options          *o,
	const MlSliceVec *args,
	uint64_t         first
)
{
	MlResult r = ML_OK;
	uint64_t i = first;

	o->json    = 0;
	o->session = ml_slice ("", 0);
	o->write   = 0;
	o->problem = NULL;
	MlSliceVec_init (&o->rest);

	while (r == ML_OK && i < args->len)
	{
		MlSlice a = args->data [i];

		if (is (a, "--format") || starts_with (a, "--format="))
		{
			MlSlice value = ml_choose (
				is (a, "--format"),
				ml_choose (
					i + 1 < args->len,
					args->data [i + 1],
					ml_slice ("", 0)
				),
				ml_slice_sub (a, 9, a.len - 9)
			);

			i += ml_choose (is (a, "--format"), 1, 0);
			o->json = is (value, "json");

			if (!is (value, "json") && !is (value, "text"))
			{
				o->problem = "--format needs text or json";
			}
		}

		else if (is (a, "--session"))
		{
			if (i + 1 < args->len)
			{
				o->session = args->data [i + 1];
				i++;
			}

			else
			{
				o->problem = "--session needs a file";
			}
		}

		else if (is (a, "--write"))
		{
			o->write = 1;
		}

		else if (starts_with (a, "--"))
		{
			o->problem = "unknown option";
		}

		else
		{
			r = MlSliceVec_push (&o->rest, a);
		}

		i++;
	}

	return r;
}

/* A usage error: the message on standard error, exit code 2. */
static int32_t usage_error (MlStr *err, const char *what, MlSlice arg)
{
	add (err, "matlog: ");
	add (err, what);

	if (arg.len > 0)
	{
		add (err, ": ");
		ml_str_append (err, arg);
	}

	add (err, "\ntry: matlog help\n");
	return ML_EXIT_USAGE;
}

/* ---- the text from standard input ---- */

/* The next line of text from pos on (without its line break), trimmed. */
static MlSlice next_line (MlSlice text, uint64_t *pos)
{
	uint64_t start = *pos;
	uint64_t end   = start;

	while (end < text.len && text.data [end] != '\n')
	{
		end++;
	}

	*pos = ml_choose (end < text.len, end + 1, end);

	while (end > start && text.data [end - 1] == '\r')
	{
		end--;
	}

	return ml_slice_sub (text, start, end - start);
}

static int32_t is_blank (MlSlice s)
{
	int32_t blank = 1;

	for (uint64_t i = 0; i < s.len; i++)
	{
		blank = blank && (s.data [i] == ' ' || s.data [i] == '\t');
	}

	return blank;
}

/* ---- results as text and JSON ---- */

static const char *const kind_names [] =
{
	[ML_LINE_NONE]    = "none",
	[ML_LINE_MATH]    = "math",
	[ML_LINE_NOTE]    = "note",
	[ML_LINE_INCLUDE] = "include",
	[ML_LINE_BLANK]   = "blank",
	[ML_LINE_UNKNOWN] = "unknown"
};

static const char *type_name (MlValueKind kind)
{
	return ml_choose (
		kind == ML_VALUE_MATRIX,
		"matrix",
		ml_choose (
			kind == ML_VALUE_EXPR,
			"expr",
			ml_choose (
				kind == ML_VALUE_QUANTITY,
				"quantity",
				ml_choose (kind == ML_VALUE_APPROX, "approx", "exact")
			)
		)
	);
}

/* The rows of a typeset block as a JSON array of strings. */
static MlResult json_block (MlStr *out, MlSlice block)
{
	MlResult r     = add (out, "[");
	uint64_t pos   = 0;
	int32_t  first = 1;

	while (r == ML_OK && pos < block.len)
	{
		MlSlice row = next_line (block, &pos);

		if (!first)
		{
			r = add (out, ",");
		}

		first = 0;

		if (r == ML_OK)
		{
			r = json_string (out, row);
		}
	}

	if (r == ML_OK)
	{
		r = add (out, "]");
	}

	return r;
}

/* One line of a session, as one JSON object. */
static MlResult json_line (
	MlStr        *out,
	uint64_t     number,
	MlLineView   line,
	MlResultView res
)
{
	MlResult r = ml_str_appendf (
		out,
		"{\"line\":%u,\"kind\":\"%s\"",
		(uint32_t)number,
		kind_names [line.kind]
	);

	if (r == ML_OK && line.kind == ML_LINE_MATH)
	{
		r = ml_str_appendf (
			out,
			",\"domain\":\"%s\"",
			ml_domain_name (line.domain)
		);
	}

	if (r == ML_OK)
	{
		r = add (out, ",\"input\":");
	}

	if (r == ML_OK)
	{
		r = json_string (out, line.text);
	}

	if (r == ML_OK && res.kind == ML_RESULT_VALUE)
	{
		r = ml_str_appendf (
			out,
			",\"status\":\"value\",\"type\":\"%s\",\"value\":",
			type_name (res.value)
		);

		if (r == ML_OK)
		{
			r = json_string (out, res.text);
		}

		if (r == ML_OK && res.block.len > 0)
		{
			r = add (out, ",\"block\":");

			if (r == ML_OK)
			{
				r = json_block (out, res.block);
			}
		}
	}

	else if (r == ML_OK && res.kind == ML_RESULT_ERROR)
	{
		r = add (out, ",\"status\":\"error\",\"error\":{\"code\":");

		if (r == ML_OK)
		{
			r = json_string (out, res.code);
		}

		if (r == ML_OK)
		{
			r = add (out, ",\"message\":");
		}

		if (r == ML_OK)
		{
			r = json_string (out, res.text);
		}

		if (r == ML_OK)
		{
			r = ml_str_appendf (
				out,
				",\"start\":%u,\"len\":%u}",
				(uint32_t)res.err_start,
				(uint32_t)res.err_len
			);
		}
	}

	else if (r == ML_OK)
	{
		r = add (out, ",\"status\":\"none\"");
	}

	if (r == ML_OK)
	{
		r = add (out, "}\n");
	}

	return r;
}

/* One line of a session as `N: input  = result`, matrices under it. */
static MlResult text_line (
	MlStr        *out,
	uint64_t     number,
	MlLineView   line,
	MlResultView res
)
{
	MlResult r = ml_str_appendf (out, "%u: ", (uint32_t)number);

	if (r == ML_OK && line.kind == ML_LINE_NOTE)
	{
		r = add (out, "# ");
	}

	else if (r == ML_OK && line.kind == ML_LINE_INCLUDE)
	{
		r = add (out, "include ");
	}

	if (r == ML_OK)
	{
		r = ml_str_append (out, line.text);
	}

	if (r == ML_OK && res.kind == ML_RESULT_VALUE)
	{
		r = add (out, "  = ");

		if (r == ML_OK)
		{
			r = ml_str_append (out, res.text);
		}
	}

	else if (r == ML_OK && res.kind == ML_RESULT_ERROR)
	{
		r = add (out, "  ! ");

		if (r == ML_OK)
		{
			r = ml_str_append (out, res.text);
		}
	}

	if (r == ML_OK)
	{
		r = add (out, "\n");
	}

	return r;
}

/* The error of a line on standard error: `line N: message (code)`. */
static MlResult report_error (MlStr *err, uint64_t number, MlResultView res)
{
	MlResult r = ml_str_appendf (err, "matlog: line %u: ", (uint32_t)number);

	if (r == ML_OK)
	{
		r = ml_str_append (err, res.text);
	}

	if (r == ML_OK)
	{
		r = add (err, " (");
	}

	if (r == ML_OK)
	{
		r = ml_str_append (err, res.code);
	}

	if (r == ML_OK)
	{
		r = add (err, ")\n");
	}

	return r;
}

/*
 * Prints the lines of s from `first` on, with their results. Returns the
 * exit code: 1 when one of them is an error.
 */
static int32_t print_lines (
	MlStr           *out,
	MlStr           *err,
	const MlSession *s,
	const MlResults *res,
	uint64_t        first,
	int32_t         json,
	int32_t         eval_style
)
{
	int32_t  code = ML_EXIT_OK;
	MlResult r    = ML_OK;

	for (uint64_t i = first; i < s->lines.len && r == ML_OK; i++)
	{
		MlLineView   line = ml_session_get (s, i);
		MlResultView rv   = ml_results_get (res, i);

		if (json)
		{
			r = json_line (out, i + 1, line, rv);
		}

		else if (eval_style)
		{
			/* eval: just the result of each expression */
			r = ml_choose (
				rv.kind == ML_RESULT_VALUE,
				ml_str_append (out, rv.text),
				ml_choose (
					rv.kind == ML_RESULT_ERROR,
					add (out, "error: "),
					add (out, "ok")
				)
			);

			if (r == ML_OK && rv.kind == ML_RESULT_ERROR)
			{
				r = ml_str_append (out, rv.text);
			}

			if (r == ML_OK)
			{
				r = add (out, "\n");
			}
		}

		else
		{
			r = text_line (out, i + 1, line, rv);
		}

		if (r == ML_OK && rv.kind == ML_RESULT_ERROR)
		{
			code = ML_EXIT_ERROR;
			r    = report_error (err, i + 1, rv);
		}
	}

	return ml_choose (r == ML_OK, code, ML_EXIT_IO);
}

/* ---- commands ---- */

static int32_t cmd_eval (
	const Options *o,
	MlSlice       stdin_text,
	MlStr         *out,
	MlStr         *err
)
{
	int32_t   code = ML_EXIT_OK;
	MlSession s;
	MlResults res;
	MlResult  r    = ML_OK;
	uint64_t  first;

	ml_session_init (&s);
	ml_results_init (&res);

	if (o->rest.len == 0)
	{
		code = usage_error (err, "eval needs an expression", ml_slice ("", 0));
	}

	else if (o->write && o->session.len == 0)
	{
		code = usage_error (err, "--write needs --session", ml_slice ("", 0));
	}

	else if (o->session.len > 0)
	{
		r = ml_mlog_load (&s, o->session);

		if (r == ML_ERR_IO && o->write)
		{
			/* a new session file that --write creates */
			r = ml_session_set_path (&s, o->session);
		}

		else if (r != ML_OK)
		{
			code = ML_EXIT_IO;
			add (err, "matlog: cannot read ");
			ml_str_append (err, o->session);
			add (err, "\n");
		}
	}

	first = s.lines.len;

	for (uint64_t i = 0; i < o->rest.len && r == ML_OK && code == 0; i++)
	{
		MlSlice a   = o->rest.data [i];
		uint64_t pos = 0;

		if (is (a, "-"))
		{
			while (r == ML_OK && pos < stdin_text.len)
			{
				MlSlice line = next_line (stdin_text, &pos);

				if (!is_blank (line))
				{
					r = ml_session_append (
						&s,
						ML_LINE_MATH,
						ML_DOMAIN_ALGEBRA,
						line
					);
				}
			}
		}

		else
		{
			r = ml_session_append (&s, ML_LINE_MATH, ML_DOMAIN_ALGEBRA, a);
		}
	}

	if (r == ML_OK && code == 0)
	{
		r = ml_results_update (&res, &s, 0);
	}

	if (r == ML_OK && code == 0)
	{
		code = print_lines (out, err, &s, &res, first, o->json, 1);
	}

	if (r == ML_OK && code != ML_EXIT_IO && o->write && o->session.len > 0)
	{
		r = ml_mlog_save (&s);

		if (r != ML_OK)
		{
			code = ML_EXIT_IO;
			add (err, "matlog: cannot write the session file\n");
		}
	}

	if (r == ML_ERR_NOMEM)
	{
		code = ML_EXIT_IO;
		add (err, "matlog: out of memory\n");
	}

	ml_results_free (&res);
	ml_session_free (&s);
	return code;
}

static int32_t cmd_run (const Options *o, MlStr *out, MlStr *err)
{
	int32_t   code = ML_EXIT_OK;
	MlSession s;
	MlResults res;

	ml_session_init (&s);
	ml_results_init (&res);

	if (o->rest.len != 1)
	{
		code = usage_error (err, "run needs one file", ml_slice ("", 0));
	}

	else if (ml_mlog_load (&s, o->rest.data [0]) != ML_OK)
	{
		code = ML_EXIT_IO;
		add (err, "matlog: cannot read ");
		ml_str_append (err, o->rest.data [0]);
		add (err, "\n");
	}

	else if (ml_results_update (&res, &s, 0) != ML_OK)
	{
		code = ML_EXIT_IO;
		add (err, "matlog: out of memory\n");
	}

	else
	{
		code = print_lines (out, err, &s, &res, 0, o->json, 0);
	}

	ml_results_free (&res);
	ml_session_free (&s);
	return code;
}

/* The one text argument of lex and parse: the argument, or all of stdin. */
static MlSlice text_argument (const Options *o, MlSlice stdin_text)
{
	return ml_choose (
		is (o->rest.data [0], "-"),
		stdin_text,
		o->rest.data [0]
	);
}

static int32_t cmd_lex (
	const Options *o,
	MlSlice       stdin_text,
	MlStr         *out,
	MlStr         *err
)
{
	int32_t code = ML_EXIT_OK;

	if (o->rest.len != 1)
	{
		code = usage_error (err, "lex needs one text", ml_slice ("", 0));
	}

	else
	{
		MlSlice     text  = text_argument (o, stdin_text);
		MlLexResult lexed = ml_lex (text);
		MlResult    r     = lexed.result;

		for (uint64_t i = 0; i < lexed.tokens.len && r == ML_OK; i++)
		{
			MlToken t = lexed.tokens.data [i];

			if (o->json)
			{
				r = ml_str_appendf (
					out,
					"{\"kind\":\"%s\",\"start\":%u,\"len\":%u,\"text\":",
					ml_token_kind_name (t.kind),
					(uint32_t)t.start,
					(uint32_t)t.len
				);

				if (r == ML_OK)
				{
					r = json_string (out, ml_token_text (text, t));
				}

				if (r == ML_OK)
				{
					r = add (out, "}\n");
				}
			}

			else
			{
				r = ml_str_appendf (
					out,
					"%s\t%u\t%u\t",
					ml_token_kind_name (t.kind),
					(uint32_t)t.start,
					(uint32_t)t.len
				);

				if (r == ML_OK)
				{
					r = ml_str_append (out, ml_token_text (text, t));
				}

				if (r == ML_OK)
				{
					r = add (out, "\n");
				}
			}
		}

		code = ml_choose (r == ML_OK, ML_EXIT_OK, ML_EXIT_IO);
		MlTokenVec_free (&lexed.tokens);
	}

	return code;
}

static int32_t cmd_parse (
	const Options *o,
	MlSlice       stdin_text,
	MlStr         *out,
	MlStr         *err
)
{
	int32_t code = ML_EXIT_OK;

	if (o->rest.len != 1)
	{
		code = usage_error (err, "parse needs one text", ml_slice ("", 0));
	}

	else
	{
		MlSlice       text = text_argument (o, stdin_text);
		MlParseResult p    = ml_parse (text);
		MlStr         tree;
		MlResult      r    = ML_OK;

		ml_str_init (&tree);

		if (p.error.code == ML_PARSE_OK)
		{
			r = ml_ast_dump (&tree, text, &p.ast);
		}

		if (r == ML_OK && p.error.code == ML_PARSE_OK && o->json)
		{
			r = add (out, "{\"ok\":true,\"tree\":");

			if (r == ML_OK)
			{
				r = json_string (out, ml_str_view (tree));
			}

			if (r == ML_OK)
			{
				r = add (out, "}\n");
			}
		}

		else if (r == ML_OK && p.error.code == ML_PARSE_OK)
		{
			r = ml_str_append (out, ml_str_view (tree));

			if (r == ML_OK)
			{
				r = add (out, "\n");
			}
		}

		else if (r == ML_OK)
		{
			code = ML_EXIT_ERROR;

			if (o->json)
			{
				r = ml_str_appendf (
					out,
					"{\"ok\":false,\"error\":{\"code\":\"%s\","
					"\"message\":\"%s\",\"start\":%u,\"len\":%u}}\n",
					ml_parse_code_name (p.error.code),
					ml_parse_error_text (p.error.code),
					(uint32_t)p.error.start,
					(uint32_t)p.error.len
				);
			}

			else
			{
				r = ml_str_appendf (
					out,
					"error: %s at %u+%u\n",
					ml_parse_error_text (p.error.code),
					(uint32_t)p.error.start,
					(uint32_t)p.error.len
				);
			}

			if (r == ML_OK)
			{
				r = ml_str_appendf (
					err,
					"matlog: %s (%s)\n",
					ml_parse_error_text (p.error.code),
					ml_parse_code_name (p.error.code)
				);
			}
		}

		code = ml_choose (r == ML_OK, code, ML_EXIT_IO);
		ml_str_free (&tree);
		ml_ast_free (&p.ast);
	}

	return code;
}

/* The number after the last colon of "file:12", 0 if there is none. */
static uint64_t line_after_colon (MlSlice a, uint64_t *colon)
{
	uint64_t pos = a.len;
	uint64_t n   = 0;

	while (pos > 0 && a.data [pos - 1] != ':')
	{
		pos--;
	}

	*colon = ml_choose (pos > 0, pos - 1, a.len);

	for (uint64_t i = pos; i < a.len && pos > 0; i++)
	{
		n = ml_choose (
			a.data [i] >= '0' && a.data [i] <= '9',
			n * 10 + (uint64_t)(a.data [i] - '0'),
			0
		);
	}

	return ml_choose (pos > 0 && pos < a.len, n, 0);
}

static int32_t cmd_steps (const Options *o, MlStr *out, MlStr *err)
{
	int32_t   code = ML_EXIT_OK;
	MlSession s;
	MlResults res;
	MlResult  r    = ML_OK;
	uint64_t  idx  = 0;
	uint64_t  colon = 0;
	uint64_t  num  = ml_choose (
		o->rest.len == 1,
		line_after_colon (o->rest.data [0], &colon),
		0
	);

	ml_session_init (&s);
	ml_results_init (&res);

	if (o->rest.len != 1)
	{
		code = usage_error (
			err,
			"steps needs one expression or FILE:LINE",
			ml_slice ("", 0)
		);
	}

	else if (num > 0)
	{
		/* FILE:LINE */
		r = ml_mlog_load (&s, ml_slice_sub (o->rest.data [0], 0, colon));

		if (r != ML_OK)
		{
			code = ML_EXIT_IO;
			add (err, "matlog: cannot read the file\n");
		}

		idx = num - 1;
	}

	else
	{
		if (o->session.len > 0)
		{
			r = ml_mlog_load (&s, o->session);
		}

		if (r != ML_OK)
		{
			code = ML_EXIT_IO;
			add (err, "matlog: cannot read the session\n");
		}

		idx = s.lines.len;
		r   = ml_choose (
			r == ML_OK,
			ml_session_append (
				&s,
				ML_LINE_MATH,
				ML_DOMAIN_ALGEBRA,
				o->rest.data [0]
			),
			r
		);
	}

	if (code == ML_EXIT_OK)
	{
		r = ml_results_update (&res, &s, 0);
	}

	if (code == ML_EXIT_OK && r == ML_OK)
	{
		MlStr       text;
		MlLineSteps st;

		ml_str_init (&text);
		st = ml_line_steps (&text, &res, &s, idx);

		if (st.result != ML_OK)
		{
			code = ML_EXIT_IO;
		}

		else if (!st.found)
		{
			code = ML_EXIT_ERROR;
			add (err, "matlog: no steps for this line\n");
		}

		else if (o->json)
		{
			MlLineView line = ml_session_get (&s, idx);
			uint64_t   pos  = 0;
			int32_t    first = 1;

			r = ml_str_appendf (
				out,
				"{\"line\":%u,\"input\":",
				(uint32_t)(idx + 1)
			);
			r = ml_choose (r == ML_OK, json_string (out, line.text), r);
			r = ml_choose (r == ML_OK, add (out, ",\"steps\":["), r);

			while (r == ML_OK && pos < text.len)
			{
				MlSlice row = next_line (ml_str_view (text), &pos);

				r = ml_choose (r == ML_OK && !first, add (out, ","), r);
				first = 0;
				r = ml_choose (r == ML_OK, json_string (out, row), r);
			}

			r = ml_choose (r == ML_OK, add (out, "]}\n"), r);
		}

		else
		{
			r = ml_str_append (out, ml_str_view (text));
			r = ml_choose (r == ML_OK, add (out, "\n"), r);
		}

		ml_str_free (&text);
	}

	code = ml_choose (r == ML_ERR_NOMEM, ML_EXIT_IO, code);
	ml_results_free (&res);
	ml_session_free (&s);
	return code;
}

static int32_t cmd_help (const Options *o, MlStr *out, MlStr *err)
{
	int32_t     code  = ML_EXIT_OK;
	const char *topic = help_overview;

	if (o->rest.len > 1)
	{
		code = usage_error (err, "help takes one command", ml_slice ("", 0));
	}

	else if (o->rest.len == 1)
	{
		MlSlice name = o->rest.data [0];

		topic = ml_choose (
			is (name, "eval"),
			help_eval,
			ml_choose (
				is (name, "run"),
				help_run,
				ml_choose (
					is (name, "lex"),
					help_lex,
					ml_choose (
						is (name, "parse"),
						help_parse,
						ml_choose (is (name, "steps"), help_steps, NULL)
					)
				)
			)
		);

		if (topic == NULL && ml_cli_is_command (name))
		{
			topic = help_overview;
		}

		else if (topic == NULL)
		{
			code = usage_error (err, "no such command", name);
		}
	}

	if (code == ML_EXIT_OK)
	{
		add (out, topic);
	}

	return code;
}

static int32_t cmd_commands (const Options *o, MlStr *out)
{
	MlResult r = ML_OK;

	for (uint64_t i = 0; i < ml_count (commands) && r == ML_OK; i++)
	{
		if (o->json)
		{
			r = ml_str_appendf (
				out,
				"{\"name\":\"%s\",\"summary\":\"%s\"}\n",
				commands [i].name,
				commands [i].summary
			);
		}

		else
		{
			r = ml_str_appendf (
				out,
				"%s\t%s\n",
				commands [i].name,
				commands [i].summary
			);
		}
	}

	return ml_choose (r == ML_OK, ML_EXIT_OK, ML_EXIT_IO);
}

int32_t ml_cli_run (
	const MlSliceVec *args,
	MlSlice          stdin_text,
	MlStr            *out,
	MlStr            *err
)
{
	int32_t code = ML_EXIT_OK;
	Options o;
	MlSlice cmd  = ml_choose (
		args->len > 0,
		args->data [0],
		ml_slice ("", 0)
	);

	if (options_read (&o, args, 1) != ML_OK)
	{
		code = ML_EXIT_IO;
		add (err, "matlog: out of memory\n");
	}

	else if (o.problem != NULL)
	{
		code = usage_error (err, o.problem, ml_slice ("", 0));
	}

	else if (is (cmd, "eval"))
	{
		code = cmd_eval (&o, stdin_text, out, err);
	}

	else if (is (cmd, "run"))
	{
		code = cmd_run (&o, out, err);
	}

	else if (is (cmd, "lex"))
	{
		code = cmd_lex (&o, stdin_text, out, err);
	}

	else if (is (cmd, "parse"))
	{
		code = cmd_parse (&o, stdin_text, out, err);
	}

	else if (is (cmd, "steps"))
	{
		code = cmd_steps (&o, out, err);
	}

	else if (is (cmd, "help") || is (cmd, "--help") || is (cmd, "-h"))
	{
		code = cmd_help (&o, out, err);
	}

	else if (is (cmd, "syntax"))
	{
		add (out, syntax_text);
	}

	else if (is (cmd, "schema"))
	{
		add (out, schema_text);
	}

	else if (is (cmd, "commands"))
	{
		code = cmd_commands (&o, out);
	}

	else if (is (cmd, "version") || is (cmd, "--version"))
	{
		add (out, VERSION "\n");
	}

	else
	{
		code = usage_error (err, "unknown command", cmd);
	}

	options_free (&o);
	return code;
}
