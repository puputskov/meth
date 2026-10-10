#include <meth/cli/cli.h>
#include <meth/docs/docs.h>
#include <meth/export/export.h>
#include <meth/platform/config.h>
#include <meth/platform/fs.h>

#include <meth/parse/parser.h>
#include <meth/session/mlog.h>
#include <meth/session/results.h>
#include <meth/session/steps.h>
#include <meth/docs/examples.h>
#include <meth/session/options.h>
#include <meth/session/settings.h>
#include <meth/session/watch.h>
#include <meth/proof/proof.h>
#include <meth/texts/texts.h>
#include <meth/ui/theme.h>

#define VERSION "meth 0.3.0"

/* ---- text of the help ---- */

/* The commands of the command line (summaries: section [cli] of the texts). */
static const char *const commands [] = {
	"eval",
	"run",
	"lex",
	"parse",
	"steps",
	"help",
	"syntax",
	"docs",
	"export",
	"theme",
	"config",
	"examples",
	"vars",
	"schema",
	"texts",
	"proof",
	"commands",
	"version"
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
		found = found || is (first, commands [i]);
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
	int32_t    markdown; /* --format markdown (docs show) */
	MlSlice    session;
	MlDomain   domain;   /* --domain: what eval and steps read the text as */
	int32_t    write;
	int32_t    symbols; /* --symbols: unknown names are symbols */
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

	o->json     = 0;
	o->markdown = 0;
	o->session  = ml_slice ("", 0);
	o->domain   = ML_DOMAIN_ALGEBRA;
	o->write   = 0;
	o->symbols = 0;
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
			o->json     = is (value, "json");
			o->markdown = is (value, "markdown");

			if (!is (value, "json") && !is (value, "text")
				&& !is (value, "markdown"))
			{
				o->problem = ml_text ("cli_msg.format_needs_text_or_json");
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
				o->problem = ml_text ("cli_msg.session_needs_a_file");
			}
		}

		else if (is (a, "--domain"))
		{
			MlDomainResult d = {
				.result = ML_ERR_PARSE,
				.domain = ML_DOMAIN_ALGEBRA
			};

			if (i + 1 < args->len)
			{
				d = ml_domain_from_name (args->data [i + 1]);
				i++;
			}

			if (d.result == ML_OK)
			{
				o->domain = d.domain;
			}

			else
			{
				o->problem = ml_text ("cli_msg.domain_needs_a_domain_name");
			}
		}

		else if (is (a, "--write"))
		{
			o->write = 1;
		}

		else if (is (a, "--symbols"))
		{
			o->symbols = 1;
		}

		else if (starts_with (a, "--"))
		{
			o->problem = ml_text ("cli_msg.unknown_option");
		}

		else
		{
			r = MlSliceVec_push (&o->rest, a);
		}

		i++;
	}

	return r;
}

/* A line on standard error, from the texts. */
static void err_line (MlStr *err, const char *code)
{
	add (err, ml_text (code));
	add (err, "\n");
}

/* A line on standard error about a file, {path} filled in. */
static void file_error (MlStr *err, const char *code, MlSlice path)
{
	MlTextArg arg = { .name = "path", .value = path };

	ml_text_fill (err, ml_text (code), &arg, 1);
	add (err, "\n");
}

/* A usage error: the message on standard error, exit code 2. */
static int32_t usage_error (MlStr *err, const char *what, MlSlice arg)
{
	add (err, "meth: ");
	add (err, what);

	if (arg.len > 0)
	{
		add (err, ": ");
		ml_str_append (err, arg);
	}

	add (err, "\n");
	add (err, ml_text ("cli_msg.try_help"));
	add (err, "\n");
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

static const char *const kind_names [] = {
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
				ml_choose (
					kind == ML_VALUE_COMPLEX,
					"complex",
					ml_choose (
						kind == ML_VALUE_QUAT,
						"quaternion",
						ml_choose (
							kind == ML_VALUE_PLOT,
							"plot",
							ml_choose (
								kind == ML_VALUE_BOOL,
								"bool",
								ml_choose (
									kind == ML_VALUE_MV,
									"multivector",
								ml_choose (
									kind == ML_VALUE_LIST,
									"list",
									ml_choose (
										kind == ML_VALUE_APPROX,
										"approx",
										"exact"
									)
								)
								)
							)
						)
					)
				)
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
			ml_choose (
				   line.domain == ML_DOMAIN_BOOLEAN
				|| line.domain == ML_DOMAIN_LAMBDA,
				ml_domain_name (line.domain),
				type_name (res.value)
			)
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

		if (r == ML_OK && res.steps.len > 0)
		{
			r = add (out, ",\"steps\":");

			if (r == ML_OK)
			{
				r = json_block (out, res.steps);
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

/* The steps of a line that asks for them, indented under it. */
static MlResult add_steps_rows (MlStr *out, MlSlice steps)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	while (r == ML_OK && pos < steps.len)
	{
		MlSlice row = next_line (steps, &pos);

		r = add (out, "    ");
		r = ml_choose (r == ML_OK, ml_str_append (out, row), r);
		r = ml_choose (r == ML_OK, add (out, "\n"), r);
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

	if (r == ML_OK)
	{
		r = add_steps_rows (out, res.steps);
	}

	return r;
}

/* The error of a line on standard error: `line N: message (code)`. */
static MlResult report_error (MlStr *err, uint64_t number, MlResultView res)
{
	MlResult r = ml_str_appendf (err, "meth: line %u: ", (uint32_t)number);

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

			if (r == ML_OK)
			{
				r = add_steps_rows (out, rv.steps);
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
	res.free_symbols     = o->symbols;
	res.env.free_symbols = o->symbols;

	if (o->rest.len == 0)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.eval_needs_an_expression"),
			ml_slice ("", 0)
		);
	}

	else if (o->write && o->session.len == 0)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.write_needs_session"),
			ml_slice ("", 0)
		);
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
			file_error (err, "cli_msg.cannot_read_path", o->session);
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
					r = ml_session_append (&s, ML_LINE_MATH, o->domain, line);
				}
			}
		}

		else
		{
			r = ml_session_append (&s, ML_LINE_MATH, o->domain, a);
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
			err_line (err, "cli_msg.cannot_write_the_session_file");
		}
	}

	if (r == ML_ERR_NOMEM)
	{
		code = ML_EXIT_IO;
		err_line (err, "cli_msg.out_of_memory");
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
	res.free_symbols     = o->symbols;
	res.env.free_symbols = o->symbols;

	if (o->rest.len != 1)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.run_needs_one_file"),
			ml_slice ("", 0)
		);
	}

	else if (ml_mlog_load (&s, o->rest.data [0]) != ML_OK)
	{
		code = ML_EXIT_IO;
		file_error (err, "cli_msg.cannot_read_path", o->rest.data [0]);
	}

	else if (ml_results_update (&res, &s, 0) != ML_OK)
	{
		code = ML_EXIT_IO;
		err_line (err, "cli_msg.out_of_memory");
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
		code = usage_error (
			err,
			ml_text ("cli_msg.lex_needs_one_text"),
			ml_slice ("", 0)
		);
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
		code = usage_error (
			err,
			ml_text ("cli_msg.parse_needs_one_text"),
			ml_slice ("", 0)
		);
	}

	else
	{
		MlSlice       text = text_argument (o, stdin_text);
		MlParseResult p    = ml_parse_mode (
			text,
			ml_domain_units (o->domain),
			ml_domain_pga (o->domain)
		);
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
					"meth: %s (%s)\n",
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
	res.free_symbols     = o->symbols;
	res.env.free_symbols = o->symbols;

	if (o->rest.len != 1)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.steps_needs_one_expression_or_file_line"),
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
			err_line (err, "cli_msg.cannot_read_the_file");
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
			err_line (err, "cli_msg.cannot_read_the_session");
		}

		idx = s.lines.len;
		r   = ml_choose (
			r == ML_OK,
			ml_session_append (
				&s,
				ML_LINE_MATH,
				o->domain,
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
			err_line (err, "cli_msg.no_steps_for_this_line");
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
	const char *topic = ml_text ("help.overview");

	if (o->rest.len > 1)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.help_takes_one_command"),
			ml_slice ("", 0)
		);
	}

	else if (o->rest.len == 1)
	{
		MlSlice name = o->rest.data [0];
		MlStr   key;

		ml_str_init (&key);

		/* syntax and schema are texts of their own, not help of a command */
		if (
			   ml_str_appendf (&key, "help.%.*s", (int)name.len, name.data)
			== ML_OK
			&& ml_cli_is_command (name)
			&& !is (name, "syntax")
			&& !is (name, "schema")
			&& ml_text_find (ml_str_cstr (key)) != NULL
		)
		{
			topic = ml_text_find (ml_str_cstr (key));
		}

		else if (!ml_cli_is_command (name))
		{
			code = usage_error (err, ml_text ("cli_msg.no_such_command"), name);
		}

		ml_str_free (&key);
	}

	if (code == ML_EXIT_OK)
	{
		add (out, topic);
	}

	return code;
}

/* ---- docs ---- */

static MlResult docs_json_page (MlStr *out, const MlDocPage *p, int32_t full)
{
	MlResult r = add (out, "{\"id\":");

	r = ml_choose (r == ML_OK, json_string (out, ml_str_view (p->id)), r);
	r = ml_choose (r == ML_OK, add (out, ",\"title\":"), r);
	r = ml_choose (r == ML_OK, json_string (out, ml_str_view (p->title)), r);
	r = ml_choose (r == ML_OK, add (out, ",\"summary\":"), r);
	r = ml_choose (r == ML_OK, json_string (out, ml_str_view (p->summary)), r);

	if (r == ML_OK && full)
	{
		r = add (out, ",\"keywords\":");
		r = ml_choose (
			r == ML_OK,
			json_string (out, ml_str_view (p->keywords)),
			r
		);
		r = ml_choose (r == ML_OK, add (out, ",\"see\":"), r);
		r = ml_choose (r == ML_OK, json_string (out, ml_str_view (p->see)), r);
		r = ml_choose (r == ML_OK, add (out, ",\"blocks\":["), r);

		for (uint64_t i = 0; i < p->blocks.len && r == ML_OK; i++)
		{
			r = add (out, ml_choose (i > 0, ",{\"kind\":\"", "{\"kind\":\""));
			r = ml_choose (
				r == ML_OK,
				add (out, ml_doc_block_name (p->blocks.data [i].kind)),
				r
			);
			r = ml_choose (r == ML_OK, add (out, "\",\"text\":"), r);
			r = ml_choose (
				r == ML_OK,
				json_string (out, ml_str_view (p->blocks.data [i].text)),
				r
			);
			r = ml_choose (r == ML_OK, add (out, "}"), r);
		}

		r = ml_choose (r == ML_OK, add (out, "]"), r);
	}

	return ml_choose (r == ML_OK, add (out, "}\n"), r);
}

/* One page as text: the laid out lines at 78 columns. */
static MlResult docs_show_text (MlStr *out, const MlDocPage *p)
{
	MlResult     r = ML_OK;
	MlDocLineVec lines;

	MlDocLineVec_init (&lines);
	r = ml_doc_layout (&lines, p, 78);

	for (uint64_t i = 0; i < lines.len && r == ML_OK; i++)
	{
		r = ml_str_append (out, ml_str_view (lines.data [i].text));
		r = ml_choose (r == ML_OK, add (out, "\n"), r);
	}

	ml_doc_lines_free (&lines);
	return r;
}

static MlResult docs_search_out (
	MlStr       *out,
	const MlDocs *d,
	MlSlice     query,
	int32_t     json
)
{
	MlResult    r = ML_OK;
	MlDocHitVec hits;

	MlDocHitVec_init (&hits);
	r = ml_docs_search (d, query, &hits);

	for (uint64_t i = 0; i < hits.len && r == ML_OK; i++)
	{
		const MlDocPage *p = &d->pages.data [hits.data [i].page];

		if (json)
		{
			r = ml_str_appendf (
				out,
				"{\"score\":%u,\"id\":",
				(uint32_t)hits.data [i].score
			);
			r = ml_choose (
				r == ML_OK,
				json_string (out, ml_str_view (p->id)),
				r
			);
			r = ml_choose (r == ML_OK, add (out, ",\"title\":"), r);
			r = ml_choose (
				r == ML_OK,
				json_string (out, ml_str_view (p->title)),
				r
			);
			r = ml_choose (r == ML_OK, add (out, ",\"where\":"), r);
			r = ml_choose (
				r == ML_OK,
				json_string (out, ml_str_view (hits.data [i].where)),
				r
			);
			r = ml_choose (r == ML_OK, add (out, "}\n"), r);
		}

		else
		{
			r = ml_str_appendf (
				out,
				"%u\t%.*s\t%.*s\t%.*s\n",
				(uint32_t)hits.data [i].score,
				(int)p->id.len,
				p->id.data,
				(int)p->title.len,
				p->title.data,
				(int)hits.data [i].where.len,
				hits.data [i].where.data
			);
		}
	}

	ml_doc_hits_free (&hits);
	return r;
}

/* meth docs list | show ID | search WORDS... | check */
static int32_t cmd_docs (const Options *o, MlStr *out, MlStr *err)
{
	int32_t  code = ML_EXIT_OK;
	MlResult r    = ML_OK;
	MlDocs   d;
	uint64_t bad_page;
	uint64_t bad_line;
	MlSlice  sub
		= ml_choose (o->rest.len > 0, o->rest.data [0], ml_slice ("", 0));

	ml_docs_init (&d);

	if (o->rest.len == 0)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.docs_needs_list_show_search_or_check"),
			ml_slice ("", 0)
		);
	}

	else if (ml_docs_load (&d, NULL, &bad_page, &bad_line) != ML_OK)
	{
		code = ML_EXIT_IO;
		ml_str_appendf (
			err,
			"meth: page %u of the documentation is broken at line %u\n",
			(uint32_t)bad_page,
			(uint32_t)bad_line
		);
	}

	else if (is (sub, "list"))
	{
		for (uint64_t i = 0; i < d.pages.len && r == ML_OK; i++)
		{
			const MlDocPage *p = &d.pages.data [i];

			if (o->json)
			{
				r = docs_json_page (out, p, 0);
			}

			else
			{
				r = ml_str_appendf (
					out,
					"%.*s\t%.*s\t%.*s\n",
					(int)p->id.len,
					p->id.data,
					(int)p->title.len,
					p->title.data,
					(int)p->summary.len,
					p->summary.data
				);
			}
		}
	}

	else if (is (sub, "show") && o->rest.len == 2)
	{
		uint64_t at = ml_docs_find (&d, o->rest.data [1]);

		if (at >= d.pages.len)
		{
			code = usage_error (
				err,
				ml_text ("cli_msg.no_such_page"),
				o->rest.data [1]
			);
		}

		else if (o->json)
		{
			r = docs_json_page (out, &d.pages.data [at], 1);
		}

		else if (o->markdown)
		{
			add (out, ml_doc_sources [at].text);
		}

		else
		{
			r = docs_show_text (out, &d.pages.data [at]);
		}
	}

	else if (is (sub, "search") && o->rest.len >= 2)
	{
		MlStr query;

		ml_str_init (&query);

		for (uint64_t i = 1; i < o->rest.len; i++)
		{
			ml_str_append (&query, o->rest.data [i]);
			ml_str_append (&query, ml_slice_cstr (" "));
		}

		r = docs_search_out (out, &d, ml_str_view (query), o->json);
		ml_str_free (&query);
	}

	else if (is (sub, "check"))
	{
		uint64_t problems = 0;

		r = ml_docs_check (&d, out, &problems);

		if (r == ML_OK && problems > 0)
		{
			code = ML_EXIT_ERROR;
		}

		else if (r == ML_OK)
		{
			r = ml_str_appendf (
				out,
				"%u pages, no problems\n",
				(uint32_t)d.pages.len
			);
		}
	}

	else
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.docs_list_show_id_search_words_or_check"),
			ml_slice ("", 0)
		);
	}

	ml_docs_free (&d);
	return ml_choose (r == ML_OK, code, ML_EXIT_IO);
}

/* meth export FILE FORMAT [WORDS...]: the session as a document. */
static int32_t cmd_export (const Options *o, MlStr *out, MlStr *err)
{
	int32_t         code = ML_EXIT_OK;
	MlSession       s;
	MlResults       res;
	MlExportFormat  fmt  = ML_EXPORT_COUNT;
	MlExportOptions opt;
	MlStr           words;
	MlStr           doc;
	MlSlice         path = ml_slice ("", 0);

	ml_session_init (&s);
	ml_results_init (&res);
	ml_str_init (&words);
	ml_str_init (&doc);
	res.free_symbols     = o->symbols;
	res.env.free_symbols = o->symbols;

	if (o->rest.len >= 2)
	{
		fmt = ml_export_format_from_name (o->rest.data [1]);
	}

	opt = ml_export_options (fmt);

	for (uint64_t i = 2; i < o->rest.len; i++)
	{
		ml_str_append (&words, o->rest.data [i]);
		ml_str_append (&words, ml_slice_cstr (" "));
	}

	if (o->rest.len < 2)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.export_needs_a_file_and_a_format"),
			ml_slice ("", 0)
		);
	}

	else if (fmt == ML_EXPORT_COUNT)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.no_such_format"),
			o->rest.data [1]
		);
	}

	else if (ml_export_read_words (&opt, ml_str_view (words), &path) != ML_OK)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.bad_export_option"),
			ml_str_view (words)
		);
	}

	else if (ml_mlog_load (&s, o->rest.data [0]) != ML_OK)
	{
		code = ML_EXIT_IO;
		file_error (err, "cli_msg.cannot_read_path", o->rest.data [0]);
	}

	else if (
		   ml_results_update (&res, &s, 0) != ML_OK
		|| ml_export (&doc, &s, &res, opt) != ML_OK
	)
	{
		code = ML_EXIT_IO;
		err_line (err, "cli_msg.out_of_memory");
	}

	else if (path.len > 0)
	{
		if (ml_fs_write_file_atomic (path, ml_str_view (doc)) != ML_OK)
		{
			code = ML_EXIT_IO;
			file_error (err, "cli_msg.cannot_write_path", path);
		}
	}

	else
	{
		ml_str_append (out, ml_str_view (doc));
	}

	ml_str_free (&doc);
	ml_str_free (&words);
	ml_results_free (&res);
	ml_session_free (&s);
	return code;
}

/* meth theme: the names; meth theme NAME: the text of a theme file. */
static int32_t cmd_theme (const Options *o, MlStr *out, MlStr *err)
{
	MlResult r    = ML_OK;
	int32_t  code = ML_EXIT_OK;

	if (o->rest.len > 1)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.theme_takes_one_name"),
			ml_slice ("", 0)
		);
	}

	else if (o->rest.len == 0)
	{
		for (uint64_t i = 0; i < ml_theme_builtin_count () && r == ML_OK; i++)
		{
			r = ml_str_appendf (out, "%s\n", ml_theme_builtin_name (i));
		}
	}

	else
	{
		MlTheme t;

		if (ml_theme_builtin (&t, o->rest.data [0]) == ML_OK)
		{
			r = ml_theme_format (out, &t);
		}

		else
		{
			code = usage_error (
				err,
				ml_text ("cli_msg.no_such_theme"),
				o->rest.data [0]
			);
		}
	}

	return ml_choose (r == ML_OK, code, ML_EXIT_IO);
}

/* ---- config ---- */

/* One setting, as a text line or a JSON object. */
static MlResult config_entry (
	MlStr    *out,
	int32_t  json,
	uint64_t index,
	MlSlice  value,
	int32_t  from_file
)
{
	MlOption o = ml_option_at (index);
	MlResult r = ML_OK;

	if (json)
	{
		r = ml_str_appendf (
			out,
			"{\"name\":\"%s\",\"section\":\"%s\",\"value\":",
			o.name,
			o.section
		);
		r = ml_choose (r == ML_OK, json_string (out, value), r);
		r = ml_choose (
			r == ML_OK,
			ml_str_appendf (
				out,
				",\"default\":\"%s\",\"source\":\"%s\"}\n",
				o.initial,
				ml_choose (from_file, "file", "default")
			),
			r
		);
	}

	else
	{
		r = ml_str_appendf (out, "%s = ", o.name);
		r = ml_choose (r == ML_OK, ml_str_append (out, value), r);
		r = ml_choose (r == ML_OK, add (out, "\n"), r);
	}

	return r;
}

/* The value of a setting in the file, else its default. */
static MlResult config_value (uint64_t index, MlStr *value, int32_t *from_file)
{
	MlOption o = ml_option_at (index);
	MlStr    raw;
	MlStr    good;
	MlResult r;

	ml_str_init (&raw);
	ml_str_init (&good);
	*from_file = 0;
	r = ml_settings_get (
		&raw,
		ml_slice_cstr (o.section),
		ml_slice_cstr (o.name),
		from_file
	);

	if (
		   r == ML_OK
		&& *from_file
		&& ml_option_check (index, ml_str_view (raw), &good) == ML_OK
	)
	{
		r = ml_str_append (value, ml_str_view (good));
	}

	else if (r == ML_OK)
	{
		*from_file = 0;
		r = add (value, o.initial);
	}

	ml_str_free (&good);
	ml_str_free (&raw);
	return r;
}

static int32_t cmd_config (const Options *o, MlStr *out, MlStr *err)
{
	MlResult r    = ML_OK;
	int32_t  code = ML_EXIT_OK;
	MlSlice  sub  = ml_choose (
		o->rest.len > 0,
		o->rest.data [0],
		ml_slice ("", 0)
	);

	if (is (sub, "path") && o->rest.len == 1)
	{
		r = ml_settings_path (out);
		r = ml_choose (r == ML_OK, add (out, "\n"), r);

		if (r == ML_ERR_IO)
		{
			code = ML_EXIT_IO;
			r    = ML_OK;
			err_line (err, "cli_msg.no_settings_folder");
		}
	}

	else if (is (sub, "list") && o->rest.len == 1)
	{
		for (uint64_t i = 0; i < ml_option_count () && r == ML_OK; i++)
		{
			MlStr   value;
			int32_t from_file = 0;

			ml_str_init (&value);
			r = config_value (i, &value, &from_file);
			r = ml_choose (
				r == ML_OK,
				config_entry (out, o->json, i, ml_str_view (value), from_file),
				r
			);
			ml_str_free (&value);
		}
	}

	else if (
		   (is (sub, "get") && o->rest.len == 2)
		|| (is (sub, "set") && o->rest.len == 3)
	)
	{
		uint64_t index = ml_option_find (o->rest.data [1]);

		if (index >= ml_option_count ())
		{
			code = usage_error (
				err,
				ml_text ("cli_msg.no_such_setting"),
				o->rest.data [1]
			);
		}

		else if (is (sub, "set"))
		{
			MlStr good;

			ml_str_init (&good);

			if (ml_option_check (index, o->rest.data [2], &good) != ML_OK)
			{
				code = usage_error (
					err,
					ml_text ("cli_msg.not_a_value_for_the_setting"),
					o->rest.data [2]
				);
			}

			else
			{
				r = ml_settings_set (
					ml_slice_cstr (ml_option_at (index).section),
					ml_slice_cstr (ml_option_at (index).name),
					ml_str_view (good)
				);

				if (r == ML_ERR_IO)
				{
					code = ML_EXIT_IO;
					r    = ML_OK;
					err_line (err, "cli_msg.cannot_write_settings");
				}
			}

			ml_str_free (&good);
		}

		else
		{
			MlStr   value;
			int32_t from_file = 0;

			ml_str_init (&value);
			r = config_value (index, &value, &from_file);
			r = ml_choose (
				r == ML_OK,
				config_entry (
					out,
					o->json,
					index,
					ml_str_view (value),
					from_file
				),
				r
			);
			ml_str_free (&value);
		}
	}

	else
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.config_needs_list_get_name_set_name_value_or"),
			ml_slice ("", 0)
		);
	}

	return ml_choose (r == ML_OK, code, ML_EXIT_IO);
}

/* ---- vars ---- */

/* One name of the watch as a JSON object. */
static MlResult vars_json (MlStr *out, MlWatchRow w)
{
	MlResult r = add (out, "{\"name\":");

	r = ml_choose (r == ML_OK, json_string (out, ml_str_view (w.name)), r);
	r = ml_choose (r == ML_OK, add (out, ",\"type\":"), r);
	r = ml_choose (r == ML_OK, json_string (out, ml_str_view (w.type)), r);
	r = ml_choose (r == ML_OK, add (out, ",\"value\":"), r);
	r = ml_choose (r == ML_OK, json_string (out, ml_str_view (w.value)), r);
	r = ml_choose (
		r == ML_OK,
		add (out, ml_choose (w.fresh, ",\"fresh\":true", ",\"fresh\":false")),
		r
	);
	r = ml_choose (r == ML_OK, add (out, "}\n"), r);
	return r;
}

static int32_t cmd_vars (const Options *o, MlStr *out, MlStr *err)
{
	int32_t   code  = ML_EXIT_OK;
	MlSession s;
	MlResults res;
	MlResult  r     = ML_OK;
	uint64_t  colon = 0;
	uint64_t  num   = ml_choose (
		o->rest.len == 1,
		line_after_colon (o->rest.data [0], &colon),
		0
	);

	ml_session_init (&s);
	ml_results_init (&res);
	res.free_symbols     = o->symbols;
	res.env.free_symbols = o->symbols;

	if (o->rest.len != 1)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.vars_needs_file_or_file_line"),
			ml_slice ("", 0)
		);
	}

	else if (
		ml_mlog_load (&s, ml_slice_sub (o->rest.data [0], 0, colon)) != ML_OK
	)
	{
		code = ML_EXIT_IO;
		err_line (err, "cli_msg.cannot_read_the_file");
	}

	else
	{
		MlWatchRowVec rows;

		MlWatchRowVec_init (&rows);
		r = ml_results_update (&res, &s, 0);

		if (r == ML_OK)
		{
			r = ml_watch_rows (
				&rows,
				&res,
				ml_choose (num > 0, num - 1, s.lines.len)
			);
		}

		for (uint64_t i = 0; i < rows.len && r == ML_OK; i++)
		{
			MlWatchRow w = rows.data [i];

			if (o->json)
			{
				r = vars_json (out, w);
			}

			else
			{
				r = ml_str_appendf (
					out,
					"%s%-8s %-12s %s\n",
					ml_choose (w.fresh, "*", " "),
					ml_str_cstr (w.name),
					ml_str_cstr (w.type),
					ml_str_cstr (w.value)
				);
			}
		}

		ml_watch_rows_free (&rows);
	}

	ml_results_free (&res);
	ml_session_free (&s);
	return ml_choose (r == ML_OK, code, ML_EXIT_IO);
}

/* ---- examples ---- */

/* One example of the list as a JSON object. */
static MlResult example_json (
	MlStr    *out,
	uint64_t i,
	MlSlice  title,
	MlSlice  summary
)
{
	MlResult r = add (out, "{\"name\":");

	r = ml_choose (r == ML_OK, json_string (out, ml_example_name (i)), r);
	r = ml_choose (r == ML_OK, add (out, ",\"area\":"), r);
	r = ml_choose (r == ML_OK, json_string (out, ml_example_area (i)), r);
	r = ml_choose (r == ML_OK, add (out, ",\"title\":"), r);
	r = ml_choose (r == ML_OK, json_string (out, title), r);
	r = ml_choose (r == ML_OK, add (out, ",\"summary\":"), r);
	r = ml_choose (r == ML_OK, json_string (out, summary), r);
	r = ml_choose (r == ML_OK, add (out, "}\n"), r);
	return r;
}

/* meth texts check [FILE]: the gaps and mistakes of a translation. */
static int32_t cmd_texts (const Options *o, MlStr *out, MlStr *err)
{
	int32_t  code = ML_EXIT_OK;
	MlStr    path;
	MlStr    text;
	uint64_t problems = 0;
	MlResult r        = ML_OK;

	ml_str_init (&path);
	ml_str_init (&text);

	if (o->rest.len < 1 || o->rest.len > 2 || !is (o->rest.data [0], "check"))
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.texts_needs_check"),
			ml_slice ("", 0)
		);
	}

	else if (o->rest.len == 2)
	{
		r = ml_str_append (&path, o->rest.data [1]);
	}

	else
	{
		MlStr dir;

		ml_str_init (&dir);
		ml_texts_use_settings (NULL);
		r = ml_config_dir (&dir);
		r = ml_choose (
			r == ML_OK,
			ml_str_appendf (
				&path,
				"%s/texts/%s.mltext",
				ml_str_cstr (dir),
				ml_texts_language ()
			),
			r
		);
		ml_str_free (&dir);
	}

	if (code == ML_EXIT_OK && r == ML_OK)
	{
		r = ml_fs_read_file (&text, ml_str_view (path));

		if (r != ML_OK)
		{
			code = ML_EXIT_IO;
			file_error (err, "cli_msg.cannot_read_path", ml_str_view (path));
		}
	}

	if (code == ML_EXIT_OK && r == ML_OK)
	{
		r = ml_texts_check (out, ml_str_view (text), &problems);

		if (r == ML_OK && problems == 0)
		{
			r = add (out, ml_text ("texts_check.ok"));
			r = ml_choose (r == ML_OK, add (out, "\n"), r);
		}

		code = ml_choose (problems > 0, ML_EXIT_ERROR, ML_EXIT_OK);
	}

	if (r != ML_OK && code == ML_EXIT_OK)
	{
		code = ML_EXIT_IO;
		err_line (err, "cli_msg.out_of_memory");
	}

	ml_str_free (&path);
	ml_str_free (&text);
	return code;
}

/* The code of the text that names a verdict. */
static const char *proof_mark_code (MlProofMark m)
{
	const char *code = "proof_marks.valid";

	switch (m)
	{
		case ML_PROOF_INVALID:
		{
			code = "proof_marks.invalid";
		} break;

		case ML_PROOF_UNCHECKED:
		{
			code = "proof_marks.unchecked";
		} break;

		case ML_PROOF_SORRY:
		{
			code = "proof_marks.sorry";
		} break;

		case ML_PROOF_SYNTAX:
		{
			code = "proof_marks.syntax";
		} break;

		default:
		{
			code = "proof_marks.valid";
		} break;
	}

	return code;
}

/* meth proof FILE: the verdict of the proof checker on every line. */
static int32_t cmd_proof (const Options *o, MlStr *out, MlStr *err)
{
	int32_t       code = ML_EXIT_OK;
	MlResult      r    = ML_OK;
	MlStr         text;
	MlProofReport rep;

	ml_str_init (&text);
	ml_proof_report_init (&rep);

	if (o->rest.len != 1)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.proof_needs_file"),
			ml_slice ("", 0)
		);
	}

	else
	{
		r = ml_fs_read_file (&text, o->rest.data [0]);

		if (r != ML_OK)
		{
			code = ML_EXIT_IO;
			file_error (err, "cli_msg.cannot_read_path", o->rest.data [0]);
		}
	}

	if (code == ML_EXIT_OK)
	{
		r = ml_proof_check (&rep, ml_str_view (text));
	}

	if (code == ML_EXIT_OK && r == ML_OK)
	{
		uint64_t bad = 0;

		for (uint64_t i = 0; i < rep.lines.len && r == ML_OK; i++)
		{
			const MlProofLine *l = &rep.lines.data [i];

			if (l->mark != ML_PROOF_NONE)
			{
				const char *words = ml_text (proof_mark_code (l->mark));

				r = ml_str_appendf (
					out,
					"%llu: %s\n",
					(unsigned long long)(i + 1),
					ml_choose (l->note.len > 0, ml_str_cstr (l->note), words)
				);
			}

			bad += (
				   l->mark == ML_PROOF_INVALID
				|| l->mark == ML_PROOF_SYNTAX
			);
		}

		code = ml_choose (
			bad > 0 || rep.proved < rep.theorems,
			ML_EXIT_ERROR,
			ML_EXIT_OK
		);
	}

	if (r != ML_OK && code == ML_EXIT_OK)
	{
		code = ML_EXIT_IO;
		err_line (err, "cli_msg.out_of_memory");
	}

	ml_proof_report_free (&rep);
	ml_str_free (&text);
	return code;
}

static int32_t cmd_examples (const Options *o, MlStr *out, MlStr *err)
{
	MlResult r    = ML_OK;
	int32_t  code = ML_EXIT_OK;

	if (o->rest.len > 1)
	{
		code = usage_error (
			err,
			ml_text ("cli_msg.examples_takes_one_name"),
			ml_slice ("", 0)
		);
	}

	else if (o->rest.len == 1)
	{
		uint64_t idx = ml_example_find (o->rest.data [0]);

		if (idx >= ml_example_count ())
		{
			code = usage_error (
				err,
				ml_text ("cli_msg.no_such_example"),
				o->rest.data [0]
			);
		}

		else
		{
			r = ml_str_append (out, ml_example_text (idx));
		}
	}

	else
	{
		for (uint64_t i = 0; i < ml_example_count () && r == ML_OK; i++)
		{
			MlStr title;
			MlStr summary;

			ml_str_init (&title);
			ml_str_init (&summary);
			r = ml_example_title (&title, i);
			r = ml_choose (r == ML_OK, ml_example_summary (&summary, i), r);

			if (r == ML_OK && o->json)
			{
				r = example_json (
					out,
					i,
					ml_str_view (title),
					ml_str_view (summary)
				);
			}

			else if (r == ML_OK)
			{
				r = ml_str_appendf (
					out,
					"%-26.*s %s\n",
					(int)ml_example_name (i).len,
					ml_example_name (i).data,
					ml_str_cstr (title)
				);
			}

			ml_str_free (&summary);
			ml_str_free (&title);
		}
	}

	return ml_choose (r == ML_OK, code, ML_EXIT_IO);
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
				commands [i],
				ml_text_in ("cli", commands [i])
			);
		}

		else
		{
			r = ml_str_appendf (
				out,
				"%s\t%s\n",
				commands [i],
				ml_text_in ("cli", commands [i])
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
		err_line (err, "cli_msg.out_of_memory");
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
		add (out, ml_text ("help.syntax"));
	}

	else if (is (cmd, "docs"))
	{
		code = cmd_docs (&o, out, err);
	}

	else if (is (cmd, "export"))
	{
		code = cmd_export (&o, out, err);
	}

	else if (is (cmd, "theme"))
	{
		code = cmd_theme (&o, out, err);
	}

	else if (is (cmd, "config"))
	{
		code = cmd_config (&o, out, err);
	}

	else if (is (cmd, "examples"))
	{
		code = cmd_examples (&o, out, err);
	}

	else if (is (cmd, "vars"))
	{
		code = cmd_vars (&o, out, err);
	}

	else if (is (cmd, "schema"))
	{
		add (out, ml_text ("help.schema"));
	}

	else if (is (cmd, "texts"))
	{
		code = cmd_texts (&o, out, err);
	}

	else if (is (cmd, "proof"))
	{
		code = cmd_proof (&o, out, err);
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
		code = usage_error (err, ml_text ("cli_msg.unknown_command"), cmd);
	}

	options_free (&o);
	return code;
}
