#include <meth/cli/cli.h>
#include <meth/core/heap.h>
#include <meth/web/web.h>

/*
 * What the page calls (build/web/meth.wasm), the C side of web/js/meth.js.
 * Text goes in through memory the page gets from meth_alloc (and gives back
 * with meth_free); every function returns 0 or the number of a failure
 * (MlResult), and the answer, JSON or text as described in meth/web/web.h, is
 * read from meth_result_ptr and meth_result_len until the next call. There is
 * one session, made on the first call.
 */

#define EXPORT(name) \
	__attribute__ ((export_name (name)))

static MlWeb   web;
static int32_t started = 0;

static MlWeb *session (void)
{
	if (!started)
	{
		ml_web_init (&web);
		started = 1;
	}

	return &web;
}

static MlSlice text (const uint8_t *data, uint32_t len)
{
	return ml_slice ((const char *)data, len);
}

static int32_t status (MlResult r)
{
	return (int32_t)r;
}

EXPORT ("meth_alloc")
uint8_t *meth_alloc (uint32_t size)
{
	return ml_heap_alloc (ml_choose (size > 0, size, 1));
}

EXPORT ("meth_free")
void meth_free (uint8_t *p)
{
	ml_heap_free (p);
}

EXPORT ("meth_result_ptr")
const char *meth_result_ptr (void)
{
	return ml_str_cstr (session ()->out);
}

EXPORT ("meth_result_len")
uint32_t meth_result_len (void)
{
	return (uint32_t)session ()->out.len;
}

/*
 * Runs `meth eval --format json EXPR` (the command line interface, a session
 * of its own). Returns the exit code of the command; the answer is the output
 * and then the standard error text.
 */
EXPORT ("meth_eval")
int32_t meth_eval (const uint8_t *expr, uint32_t len)
{
	MlSliceVec args;
	MlStr      out;
	MlStr      err;
	int32_t    code = ML_EXIT_USAGE;

	ml_str_init (&out);
	ml_str_init (&err);
	MlSliceVec_init (&args);

	if (
		   MlSliceVec_push (&args, ml_slice_cstr ("eval")) == ML_OK
		&& MlSliceVec_push (&args, ml_slice_cstr ("--format")) == ML_OK
		&& MlSliceVec_push (&args, ml_slice_cstr ("json")) == ML_OK
		&& MlSliceVec_push (&args, text (expr, len)) == ML_OK
	)
	{
		code = ml_cli_run (&args, ml_slice ("", 0), &out, &err);
	}

	ml_str_clear (&session ()->out);
	ml_str_append (&session ()->out, ml_str_view (out));
	ml_str_append (&session ()->out, ml_str_view (err));
	MlSliceVec_free (&args);
	ml_str_free (&out);
	ml_str_free (&err);
	return code;
}

EXPORT ("meth_load")
int32_t meth_load (const uint8_t *file, uint32_t len)
{
	return status (ml_web_load (session (), text (file, len)));
}

EXPORT ("meth_save")
int32_t meth_save (void)
{
	return status (ml_web_save (session ()));
}

EXPORT ("meth_lines")
int32_t meth_lines (uint32_t from)
{
	return status (ml_web_lines (session (), from));
}

EXPORT ("meth_edit")
int32_t meth_edit (
	int32_t       op,
	uint32_t      idx,
	const uint8_t *kind,
	uint32_t      kind_len,
	const uint8_t *domain,
	uint32_t      domain_len,
	const uint8_t *line,
	uint32_t      line_len
)
{
	return status (
		ml_web_edit (
			session (),
			(MlWebEdit)op,
			idx,
			text (kind, kind_len),
			text (domain, domain_len),
			text (line, line_len)
		)
	);
}

EXPORT ("meth_set_symbols")
int32_t meth_set_symbols (int32_t on)
{
	return status (ml_web_set_symbols (session (), on));
}

EXPORT ("meth_highlight")
int32_t meth_highlight (
	const uint8_t *domain,
	uint32_t      domain_len,
	uint32_t      idx,
	const uint8_t *line,
	uint32_t      line_len,
	uint32_t      err_start,
	uint32_t      err_len
)
{
	return status (
		ml_web_highlight (
			session (),
			text (domain, domain_len),
			idx,
			text (line, line_len),
			err_start,
			err_len
		)
	);
}

EXPORT ("meth_ghost")
int32_t meth_ghost (
	const uint8_t *domain,
	uint32_t      domain_len,
	uint32_t      idx,
	const uint8_t *line,
	uint32_t      line_len
)
{
	return status (
		ml_web_ghost (
			session (),
			text (domain, domain_len),
			idx,
			text (line, line_len)
		)
	);
}

EXPORT ("meth_complete")
int32_t meth_complete (
	const uint8_t *domain,
	uint32_t      domain_len,
	uint32_t      idx,
	const uint8_t *line,
	uint32_t      line_len
)
{
	return status (
		ml_web_complete (
			session (),
			text (domain, domain_len),
			idx,
			text (line, line_len)
		)
	);
}

EXPORT ("meth_steps")
int32_t meth_steps (uint32_t idx)
{
	return status (ml_web_steps (session (), idx));
}

EXPORT ("meth_hint")
int32_t meth_hint (uint32_t idx)
{
	return status (ml_web_hint (session (), idx));
}

EXPORT ("meth_export")
int32_t meth_export (
	const uint8_t *format,
	uint32_t      format_len,
	const uint8_t *words,
	uint32_t      words_len
)
{
	return status (
		ml_web_export (
			session (),
			text (format, format_len),
			text (words, words_len)
		)
	);
}

EXPORT ("meth_domains")
int32_t meth_domains (void)
{
	return status (ml_web_domains (session ()));
}

EXPORT ("meth_docs")
int32_t meth_docs (void)
{
	return status (ml_web_docs (session ()));
}

EXPORT ("meth_doc")
int32_t meth_doc (const uint8_t *id, uint32_t id_len)
{
	return status (ml_web_doc (session (), text (id, id_len)));
}

EXPORT ("meth_docs_search")
int32_t meth_docs_search (const uint8_t *query, uint32_t query_len)
{
	return status (ml_web_docs_search (session (), text (query, query_len)));
}

EXPORT ("meth_examples")
int32_t meth_examples (void)
{
	return status (ml_web_examples (session ()));
}

EXPORT ("meth_example")
int32_t meth_example (const uint8_t *name, uint32_t name_len)
{
	return status (ml_web_example (session (), text (name, name_len)));
}

EXPORT ("meth_snippets")
int32_t meth_snippets (void)
{
	return status (ml_web_snippets (session ()));
}

EXPORT ("meth_snippet")
int32_t meth_snippet (const uint8_t *name, uint32_t name_len)
{
	return status (ml_web_snippet (session (), text (name, name_len)));
}

EXPORT ("meth_lessons")
int32_t meth_lessons (void)
{
	return status (ml_web_lessons (session ()));
}

EXPORT ("meth_themes")
int32_t meth_themes (void)
{
	return status (ml_web_themes (session ()));
}

EXPORT ("meth_theme_text")
int32_t meth_theme_text (const uint8_t *theme, uint32_t theme_len)
{
	return status (ml_web_theme_text (session (), text (theme, theme_len)));
}

EXPORT ("meth_typeset")
int32_t meth_typeset (
	const uint8_t *domain,
	uint32_t      domain_len,
	const uint8_t *line,
	uint32_t      line_len
)
{
	return status (
		ml_web_typeset (
			session (),
			text (domain, domain_len),
			text (line, line_len)
		)
	);
}
