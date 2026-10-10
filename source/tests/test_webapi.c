#include "test.h"

#include <meth/texts/texts.h>
#include <meth/web/web.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static MlResult edit (
	MlWeb      *w,
	MlWebEdit  op,
	uint64_t   idx,
	const char *kind,
	const char *domain,
	const char *text
)
{
	return ml_web_edit (w, op, idx, lit (kind), lit (domain), lit (text));
}

/* 1 when the answer of the last call has the text in it. */
static int32_t has (const MlWeb *w, const char *text)
{
	return strstr (ml_str_cstr (w->out), text) != NULL;
}

static void test_load_and_lines (void)
{
	MlWeb w;

	CHECK (ml_web_init (&w) == ML_OK);
	CHECK (
		ml_web_load (
			&w,
			lit (
				"# A note\n"
				"m:algebra a = 3\n"
				"m:algebra a * 2 + 1/2\n"
				"m:algebra [[1, 2], [3, 4]]^-1\n"
				"m:algebra 1 +\n"
			)
		) == ML_OK
	);
	CHECK (has (&w, "\"count\":5"));
	CHECK (has (&w, "\"kind\":\"note\""));
	CHECK (has (&w, "\"text\":\"A note\""));
	CHECK (has (&w, "\"domain\":\"algebra\""));
	CHECK (has (&w, "\"text\":\"13/2\""));
	CHECK (has (&w, "\"type\":\"exact\""));
	CHECK (has (&w, "\"type\":\"matrix\""));
	CHECK (has (&w, "\"block\":[\""));
	CHECK (has (&w, "\"kind\":\"error\""));
	CHECK (has (&w, "\"code\":\"parse_unexpected_end\""));
	CHECK (has (&w, "\"first\":1,\"last\":4"));

	/* from a line on */
	CHECK (ml_web_lines (&w, 3) == ML_OK);
	CHECK (has (&w, "\"from\":3"));
	CHECK (!has (&w, "\"i\":2"));
	CHECK (has (&w, "\"i\":4"));

	/* save gives the file again */
	CHECK (ml_web_save (&w) == ML_OK);
	CHECK (has (&w, "m:algebra a * 2 + 1/2\n"));

	ml_web_free (&w);
}

static void test_edit (void)
{
	MlWeb w;

	CHECK (ml_web_init (&w) == ML_OK);
	CHECK (
		ml_web_load (
			&w,
			lit ("m:algebra a = 3\nm:algebra a + 1\n")
		) == ML_OK
	);
	CHECK (has (&w, "\"text\":\"4\""));

	/* changing the first line changes the results below it */
	CHECK (edit (&w, ML_WEB_REPLACE, 0, "math", "algebra", "a = 10") == ML_OK);
	CHECK (has (&w, "\"from\":0"));
	CHECK (has (&w, "\"text\":\"11\""));

	CHECK (edit (&w, ML_WEB_INSERT, 2, "note", "", "text") == ML_OK);
	CHECK (has (&w, "\"count\":3"));
	CHECK (edit (&w, ML_WEB_DELETE, 2, "", "", "") == ML_OK);
	CHECK (has (&w, "\"count\":2"));

	/* undo takes the last change back */
	CHECK (edit (&w, ML_WEB_UNDO, 0, "", "", "") == ML_OK);
	CHECK (has (&w, "\"count\":3"));
	CHECK (edit (&w, ML_WEB_REDO, 0, "", "", "") == ML_OK);
	CHECK (has (&w, "\"count\":2"));

	/* what is not valid is refused and changes nothing */
	CHECK (
		edit (
			&w,
			ML_WEB_REPLACE,
			7,
			"math",
			"algebra",
			"1"
		) == ML_ERR_INVALID
	);
	CHECK (
		edit (
			&w,
			ML_WEB_REPLACE,
			0,
			"sheet",
			"algebra",
			"1"
		) == ML_ERR_INVALID
	);
	CHECK (
		edit (
			&w,
			ML_WEB_REPLACE,
			0,
			"math",
			"poetry",
			"1"
		) == ML_ERR_INVALID
	);
	CHECK (ml_web_lines (&w, 0) == ML_OK);
	CHECK (has (&w, "\"count\":2"));

	/* typing joins the group of the change before it, up to a checkpoint */
	CHECK (
		ml_web_load (&w, lit ("m:algebra a = 3\nm:algebra a + 1\n")) == ML_OK
	);
	CHECK (edit (&w, ML_WEB_CHECKPOINT, 0, "", "", "") == ML_OK);
	CHECK (edit (&w, ML_WEB_TYPE, 0, "math", "algebra", "a = 5") == ML_OK);
	CHECK (edit (&w, ML_WEB_TYPE, 0, "math", "algebra", "a = 50") == ML_OK);
	CHECK (has (&w, "\"text\":\"51\""));
	CHECK (edit (&w, ML_WEB_UNDO, 0, "", "", "") == ML_OK);
	CHECK (has (&w, "\"text\":\"4\""));

	/* a name with no value is an error, or a symbol when asked for */
	CHECK (ml_web_load (&w, lit ("m:algebra y + y\n")) == ML_OK);
	CHECK (has (&w, "\"kind\":\"error\""));
	CHECK (ml_web_set_symbols (&w, 1) == ML_OK);
	CHECK (has (&w, "\"kind\":\"value\""));

	ml_web_free (&w);
}

static void test_services (void)
{
	MlWeb w;

	CHECK (ml_web_init (&w) == ML_OK);
	CHECK (
		ml_web_load (
			&w,
			lit (
				"m:algebra A = [[1, 2], [3, 4]]\n"
				"m:algebra steps (det (A))\n"
				"m:algebra velocity = 3\n"
			)
		) == ML_OK
	);
	CHECK (has (&w, "\"wants_steps\":true"));

	/* highlighting: runs that cover the text, with the class names */
	CHECK (
		ml_web_highlight (
			&w,
			lit ("algebra"),
			3,
			lit ("sin (A) + 2"),
			0,
			0
		) == ML_OK
	);
	CHECK (has (&w, "[0,3,\"function\"]"));
	CHECK (has (&w, "\"number\""));
	CHECK (has (&w, "\"operator\""));
	CHECK (
		ml_web_highlight (
			&w,
			lit ("poetry"),
			0,
			lit ("1"),
			0,
			0
		) == ML_ERR_INVALID
	);

	/* the preview of a line being typed */
	CHECK (ml_web_ghost (&w, lit ("algebra"), 3, lit ("det (A) * 2")) == ML_OK);
	CHECK (has (&w, "\"shown\":true"));
	CHECK (has (&w, "\"text\":\"-4\""));
	CHECK (ml_web_ghost (&w, lit ("algebra"), 3, lit ("2 +")) == ML_OK);
	CHECK (has (&w, "\"shown\":false"));
	CHECK (ml_web_ghost (&w, lit ("algebra"), 3, lit ("A * A")) == ML_OK);
	CHECK (has (&w, "\"block\":[\""));

	/* completion of the word at the end */
	CHECK (ml_web_complete (&w, lit ("algebra"), 3, lit ("2 * vel")) == ML_OK);
	CHECK (has (&w, "\"start\":4"));
	CHECK (has (&w, "\"items\":[\"velocity\"]"));
	CHECK (has (&w, "\"ghost\":\"ocity\""));
	CHECK (ml_web_complete (&w, lit ("algebra"), 3, lit ("2 * ")) == ML_OK);
	CHECK (has (&w, "\"items\":[]"));

	/* steps on request */
	CHECK (ml_web_steps (&w, 1) == ML_OK);
	CHECK (has (&w, "\"found\":true"));
	CHECK (ml_web_steps (&w, 0) == ML_OK);
	CHECK (has (&w, "\"found\":false"));
	CHECK (ml_web_steps (&w, 9) == ML_ERR_INVALID);

	/* the document */
	CHECK (ml_web_export (&w, lit ("latex"), lit ("body")) == ML_OK);
	CHECK (has (&w, "\\begin{"));
	CHECK (ml_web_export (&w, lit ("markdown"), lit ("")) == ML_OK);
	CHECK (has (&w, "$$"));
	CHECK (ml_web_export (&w, lit ("pdf"), lit ("")) == ML_ERR_INVALID);

	/* a value as MathML */
	CHECK (ml_web_typeset (&w, lit ("algebra"), lit ("1/2")) == ML_OK);
	CHECK (has (&w, "<math>") && has (&w, "<mfrac>"));
	CHECK (
		ml_web_typeset (&w, lit ("algebra"), lit ("[[1, 2], [3, 4]]")) == ML_OK
	);
	CHECK (has (&w, "<mtable>"));
	CHECK (
		ml_web_typeset (&w, lit ("algebra"), lit ("3x^2 + sqrt(2)")) == ML_OK
	);
	CHECK (has (&w, "<msqrt>") && has (&w, "<msup>"));
	CHECK (ml_web_typeset (&w, lit ("boolean"), lit ("a && b")) == ML_OK);
	CHECK (has (&w, "\xE2\x88\xA7"));
	CHECK (ml_web_typeset (&w, lit ("algebra"), lit ("2 +")) == ML_ERR_PARSE);
	CHECK (ml_web_typeset (&w, lit ("poetry"), lit ("1")) == ML_ERR_INVALID);

	CHECK (ml_web_domains (&w) == ML_OK);
	CHECK (has (&w, "[\"algebra\",\"lambda\""));
	CHECK (has (&w, "\"proof\"]"));

	ml_web_free (&w);
}

static void test_proof_hint (void)
{
	MlWeb w;

	CHECK (ml_web_init (&w) == ML_OK);
	CHECK (
		ml_web_load (
			&w,
			lit (
				"m:proof theorem t: x + x = 2x\n"
				"m:proof x + x = 2x\n"
			)
		) == ML_OK
	);
	CHECK (ml_web_hint (&w, 1) == ML_OK);
	CHECK (has (&w, "{\"reason\":\"simplify\"}"));
	CHECK (ml_web_hint (&w, 0) == ML_OK);
	CHECK (has (&w, "{\"reason\":\"\"}"));
	CHECK (ml_web_hint (&w, 5) == ML_ERR_INVALID);

	/* the ghost of a proof line is its verdict */
	CHECK (
		ml_web_ghost (
			&w,
			lit ("proof"),
			2,
			lit ("x + x = 3x  by simplify")
		) == ML_OK
	);
	CHECK (has (&w, "\"shown\":true"));

	ml_web_free (&w);
}

static void test_content (void)
{
	MlWeb w;

	CHECK (ml_web_init (&w) == ML_OK);
	CHECK (ml_web_docs (&w) == ML_OK);
	CHECK (has (&w, "{\"id\":\"index\""));
	CHECK (ml_web_doc (&w, lit ("syntax")) == ML_OK);
	CHECK (has (&w, "\"blocks\":[{\"kind\":\""));
	CHECK (ml_web_doc (&w, lit ("no-such-page")) == ML_ERR_INVALID);
	CHECK (ml_web_docs_search (&w, lit ("matrix")) == ML_OK);
	CHECK (has (&w, "\"score\":"));
	CHECK (ml_web_docs_search (&w, lit ("")) == ML_OK);
	CHECK (has (&w, "[]"));

	CHECK (ml_web_examples (&w) == ML_OK);
	CHECK (has (&w, "\"area\":"));
	CHECK (ml_web_example (&w, lit ("no-such-example")) == ML_ERR_INVALID);
	CHECK (ml_web_snippets (&w) == ML_OK);
	CHECK (has (&w, "\"summary\":"));
	CHECK (ml_web_snippet (&w, lit ("no-such-snippet")) == ML_ERR_INVALID);
	CHECK (ml_web_lessons (&w) == ML_OK);
	CHECK (has (&w, "\"steps\":[{"));

	CHECK (ml_web_themes (&w) == ML_OK);
	CHECK (has (&w, "{\"name\":\"dark\""));
	CHECK (ml_web_theme_text (&w, lit ("number = #ff0000 bold")) == ML_OK);
	CHECK (has (&w, "\"number\":{\"fg\":\"#ff0000\""));
	CHECK (has (&w, "\"bad\":0"));

	ml_web_free (&w);
}

void run_webapi_tests (void);

void run_webapi_tests (void)
{
	CHECK (ml_texts_reload ("en", NULL) == ML_OK);
	test_load_and_lines ();
	test_edit ();
	test_services ();
	test_proof_hint ();
	test_content ();
}
