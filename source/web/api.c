#include <meth/cli/cli.h>
#include <meth/core/heap.h>
#include <meth/core/mem.h>
#include <meth/web/web.h>

/*
 * What the page calls (build/web/meth.wasm), the C side of web/js/meth.js.
 * Text goes in through memory the page gets from meth_alloc (and gives back
 * with meth_free); every function returns 0 or the number of a failure
 * (MlResult), and the answer, JSON or text as described in meth/web/web.h, is
 * read from meth_result_ptr and meth_result_len until the next call on the
 * same session.
 *
 * The page keeps several sessions (one for each file that is open and one for
 * the content of the program: the help, the examples ...): meth_create gives a
 * handle, a number from 1, which is the first argument of every other call
 * that works on a session, and meth_destroy ends the session.
 */

#define EXPORT(name) \
	__attribute__ ((export_name (name)))

#define MAX_SESSIONS 64

/* A session of the page keeps its environment in 64 MB of address space. */
#define SESSION_RESERVE (1ULL << 26)

static MlWeb   *sessions [MAX_SESSIONS];
static int32_t sized = 0;

/* The session of a handle, NULL for a handle that is not in use. */
static MlWeb *session (uint32_t handle)
{
	return ml_choose (
		handle >= 1 && handle <= MAX_SESSIONS,
		sessions [handle - 1],
		NULL
	);
}

static MlSlice text (const uint8_t *data, uint32_t len)
{
	return ml_slice ((const char *)data, len);
}

/* The call on the session of the handle; ML_ERR_INVALID without one. */
#define WITH(handle, call) \
	int32_t status = ML_ERR_INVALID; \
	MlWeb   *w     = session (handle); \
	\
	if (w != NULL) \
	{ \
		status = (int32_t)(call); \
	} \
	\
	return status

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

/* A new session; 0 when there is no room for one. */
EXPORT ("meth_create")
uint32_t meth_create (void)
{
	uint32_t handle = 0;

	if (!sized)
	{
		ml_arena_set_default_reserve (SESSION_RESERVE);
		sized = 1;
	}

	for (uint32_t i = 0; handle == 0 && i < MAX_SESSIONS; i++)
	{
		if (sessions [i] == NULL)
		{
			MlWeb *w = ml_heap_alloc_zero (sizeof (MlWeb));

			if (w != NULL && ml_web_init (w) == ML_OK)
			{
				sessions [i] = w;
				handle       = i + 1;
			}

			else
			{
				ml_heap_free (w);
			}
		}
	}

	return handle;
}

EXPORT ("meth_destroy")
void meth_destroy (uint32_t handle)
{
	MlWeb *w = session (handle);

	if (w != NULL)
	{
		ml_web_free (w);
		ml_heap_free (w);
		sessions [handle - 1] = NULL;
	}
}

EXPORT ("meth_result_ptr")
const char *meth_result_ptr (uint32_t handle)
{
	MlWeb *w = session (handle);

	return ml_choose (w != NULL, ml_str_cstr (w->out), "");
}

EXPORT ("meth_result_len")
uint32_t meth_result_len (uint32_t handle)
{
	MlWeb *w = session (handle);

	return (uint32_t)ml_choose (w != NULL, w->out.len, 0);
}

/*
 * Runs `meth eval --format json EXPR` (the command line interface, a session
 * of its own). Returns the exit code of the command; the answer is the output
 * and then the standard error text.
 */
EXPORT ("meth_eval")
int32_t meth_eval (uint32_t handle, const uint8_t *expr, uint32_t len)
{
	MlWeb      *w    = session (handle);
	int32_t    code = ML_EXIT_USAGE;
	MlSliceVec args;
	MlStr      out;
	MlStr      err;

	ml_str_init (&out);
	ml_str_init (&err);
	MlSliceVec_init (&args);

	if (
		   w != NULL
		&& MlSliceVec_push (&args, ml_slice_cstr ("eval")) == ML_OK
		&& MlSliceVec_push (&args, ml_slice_cstr ("--format")) == ML_OK
		&& MlSliceVec_push (&args, ml_slice_cstr ("json")) == ML_OK
		&& MlSliceVec_push (&args, text (expr, len)) == ML_OK
	)
	{
		code = ml_cli_run (&args, ml_slice ("", 0), &out, &err);
		ml_str_clear (&w->out);
		ml_str_append (&w->out, ml_str_view (out));
		ml_str_append (&w->out, ml_str_view (err));
	}

	MlSliceVec_free (&args);
	ml_str_free (&out);
	ml_str_free (&err);
	return code;
}

EXPORT ("meth_load")
int32_t meth_load (uint32_t handle, const uint8_t *file, uint32_t len)
{
	WITH (handle, ml_web_load (w, text (file, len)));
}

EXPORT ("meth_save")
int32_t meth_save (uint32_t handle)
{
	WITH (handle, ml_web_save (w));
}

EXPORT ("meth_lines")
int32_t meth_lines (uint32_t handle, uint32_t from)
{
	WITH (handle, ml_web_lines (w, from));
}

EXPORT ("meth_edit")
int32_t meth_edit (
	uint32_t      handle,
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
	WITH (
		handle,
		ml_web_edit (
			w,
			(MlWebEdit)op,
			idx,
			text (kind, kind_len),
			text (domain, domain_len),
			text (line, line_len)
		)
	);
}

EXPORT ("meth_set_symbols")
int32_t meth_set_symbols (uint32_t handle, int32_t on)
{
	WITH (handle, ml_web_set_symbols (w, on));
}

EXPORT ("meth_highlight")
int32_t meth_highlight (
	uint32_t      handle,
	const uint8_t *domain,
	uint32_t      domain_len,
	uint32_t      idx,
	const uint8_t *line,
	uint32_t      line_len,
	uint32_t      err_start,
	uint32_t      err_len
)
{
	WITH (
		handle,
		ml_web_highlight (
			w,
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
	uint32_t      handle,
	const uint8_t *domain,
	uint32_t      domain_len,
	uint32_t      idx,
	const uint8_t *line,
	uint32_t      line_len
)
{
	WITH (
		handle,
		ml_web_ghost (
			w,
			text (domain, domain_len),
			idx,
			text (line, line_len)
		)
	);
}

EXPORT ("meth_complete")
int32_t meth_complete (
	uint32_t      handle,
	const uint8_t *domain,
	uint32_t      domain_len,
	uint32_t      idx,
	const uint8_t *line,
	uint32_t      line_len
)
{
	WITH (
		handle,
		ml_web_complete (
			w,
			text (domain, domain_len),
			idx,
			text (line, line_len)
		)
	);
}

EXPORT ("meth_steps")
int32_t meth_steps (uint32_t handle, uint32_t idx)
{
	WITH (handle, ml_web_steps (w, idx));
}

EXPORT ("meth_hint")
int32_t meth_hint (uint32_t handle, uint32_t idx)
{
	WITH (handle, ml_web_hint (w, idx));
}

EXPORT ("meth_export")
int32_t meth_export (
	uint32_t      handle,
	const uint8_t *format,
	uint32_t      format_len,
	const uint8_t *words,
	uint32_t      words_len
)
{
	WITH (
		handle,
		ml_web_export (w, text (format, format_len), text (words, words_len))
	);
}

EXPORT ("meth_domains")
int32_t meth_domains (uint32_t handle)
{
	WITH (handle, ml_web_domains (w));
}

EXPORT ("meth_typeset")
int32_t meth_typeset (
	uint32_t      handle,
	const uint8_t *domain,
	uint32_t      domain_len,
	const uint8_t *line,
	uint32_t      line_len
)
{
	WITH (
		handle,
		ml_web_typeset (
			w,
			text (domain, domain_len),
			text (line, line_len)
		)
	);
}

EXPORT ("meth_docs")
int32_t meth_docs (uint32_t handle)
{
	WITH (handle, ml_web_docs (w));
}

EXPORT ("meth_doc")
int32_t meth_doc (uint32_t handle, const uint8_t *id, uint32_t id_len)
{
	WITH (handle, ml_web_doc (w, text (id, id_len)));
}

EXPORT ("meth_docs_search")
int32_t meth_docs_search (
	uint32_t      handle,
	const uint8_t *query,
	uint32_t      query_len
)
{
	WITH (handle, ml_web_docs_search (w, text (query, query_len)));
}

EXPORT ("meth_examples")
int32_t meth_examples (uint32_t handle)
{
	WITH (handle, ml_web_examples (w));
}

EXPORT ("meth_example")
int32_t meth_example (uint32_t handle, const uint8_t *name, uint32_t name_len)
{
	WITH (handle, ml_web_example (w, text (name, name_len)));
}

EXPORT ("meth_snippets")
int32_t meth_snippets (uint32_t handle)
{
	WITH (handle, ml_web_snippets (w));
}

EXPORT ("meth_snippet")
int32_t meth_snippet (uint32_t handle, const uint8_t *name, uint32_t name_len)
{
	WITH (handle, ml_web_snippet (w, text (name, name_len)));
}

EXPORT ("meth_lessons")
int32_t meth_lessons (uint32_t handle)
{
	WITH (handle, ml_web_lessons (w));
}

EXPORT ("meth_themes")
int32_t meth_themes (uint32_t handle)
{
	WITH (handle, ml_web_themes (w));
}

EXPORT ("meth_theme_text")
int32_t meth_theme_text (
	uint32_t      handle,
	const uint8_t *theme,
	uint32_t      theme_len
)
{
	WITH (handle, ml_web_theme_text (w, text (theme, theme_len)));
}
