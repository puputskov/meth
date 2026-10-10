#include "test.h"

#include <meth/docs/docs.h>
#include <meth/docs/examples.h>
#include <meth/platform/fs.h>

#include <string.h>

static MlSlice lit (const char *text)
{
	return ml_slice_cstr (text);
}

static const char *const sample =
	"id: sample\n"
	"title: A sample\n"
	"summary: Something short.\n"
	"keywords: one, two words\n"
	"see: other\n"
	"\n"
	"# First heading\n"
	"A paragraph that\n"
	"goes on, with a [link](other) in it.\n"
	"\n"
	"- an item\n"
	"  that goes on\n"
	"- another\n"
	"\n"
	"```example\n"
	"1+1\n"
	"=> 2\n"
	"```\n"
	"\n"
	"## Smaller\n"
	"```keys\n"
	"Ctrl+Q   quit\n"
	"```\n"
	"```\n"
	"plain code\n"
	"```\n";

static void test_parse (void)
{
	MlDocPage p;
	uint64_t  line = 0;

	ml_doc_page_init (&p);
	CHECK (ml_doc_parse (&p, lit (sample), &line) == ML_OK);
	CHECK (ml_slice_equal (ml_str_view (p.id), lit ("sample")));
	CHECK (ml_slice_equal (ml_str_view (p.title), lit ("A sample")));
	CHECK (ml_slice_equal (ml_str_view (p.summary), lit ("Something short.")));
	CHECK (ml_slice_equal (ml_str_view (p.keywords), lit ("one, two words")));
	CHECK (ml_slice_equal (ml_str_view (p.see), lit ("other")));
	CHECK (p.blocks.len == 8);

	CHECK (
		p.blocks.data [0].kind == ML_DOC_HEADING && p.blocks.data [0].level == 1
	);
	CHECK (ml_slice_equal (
		ml_str_view (p.blocks.data [0].text),
		lit ("First heading")
	));

	/* the lines of a paragraph are one line of text */
	CHECK (p.blocks.data [1].kind == ML_DOC_PARAGRAPH);
	CHECK (ml_slice_equal (
		ml_str_view (p.blocks.data [1].text),
		lit ("A paragraph that goes on, with a [link](other) in it.")
	));

	/* items and their continuation lines */
	CHECK (p.blocks.data [2].kind == ML_DOC_ITEM);
	CHECK (ml_slice_equal (
		ml_str_view (p.blocks.data [2].text),
		lit ("an item that goes on")
	));
	CHECK (p.blocks.data [3].kind == ML_DOC_ITEM);

	/* fenced blocks keep their lines */
	CHECK (p.blocks.data [4].kind == ML_DOC_EXAMPLE);
	CHECK (
		ml_slice_equal (ml_str_view (p.blocks.data [4].text), lit ("1+1\n=> 2"))
	);
	CHECK (
		p.blocks.data [5].kind == ML_DOC_HEADING && p.blocks.data [5].level == 2
	);
	CHECK (p.blocks.data [6].kind == ML_DOC_KEYS);
	CHECK (p.blocks.data [7].kind == ML_DOC_CODE);

	ml_doc_page_free (&p);
}

/* Does the text fail to parse at the line? */
static int32_t fails_at (const char *text, uint64_t expect)
{
	MlDocPage p;
	uint64_t  line = 0;
	int32_t   same;

	ml_doc_page_init (&p);
	same = ml_doc_parse (&p, lit (text), &line) == ML_ERR_PARSE
		   && line == expect;
	ml_doc_page_free (&p);
	return same;
}

static void test_parse_errors (void)
{
	CHECK (fails_at ("title: T\n\ntext\n", 1));
	CHECK (fails_at ("id: a\n\ntext\n", 1));
	CHECK (fails_at ("id: a\ntitle: T\nnonsense\n", 3));
	CHECK (fails_at ("id: a\ntitle: T\ncolour: red\n", 3));
	CHECK (fails_at ("id: a\ntitle: T\n\ntext\n```example\n1+1\n", 5));
	CHECK (fails_at ("id: a\ntitle: T\n\n```nonsense\nx\n```\n", 4));
	CHECK (fails_at ("id: a\ntitle: T\n\n#### too deep\n", 4));
	CHECK (fails_at ("id: a\ntitle: T\n\n#nospace\n", 4));
}

static void test_links (void)
{
	MlStr        plain;
	MlDocLinkVec links;
	MlSlice      text
		= lit ("see [the syntax](syntax) and [x](y), `[not](this)`, [open");

	ml_str_init (&plain);
	MlDocLinkVec_init (&links);
	CHECK (ml_doc_plain (&plain, &links, text) == ML_OK);
	CHECK (ml_slice_equal (
		ml_str_view (plain),
		lit ("see the syntax and x, `[not](this)`, [open")
	));
	CHECK (links.len == 2);
	CHECK (links.data [0].start == 4 && links.data [0].len == 10);
	CHECK (ml_slice_equal (links.data [0].target, lit ("syntax")));
	CHECK (ml_slice_equal (links.data [1].target, lit ("y")));
	MlDocLinkVec_free (&links);
	ml_str_free (&plain);
}

static void test_layout (void)
{
	MlDocPage    p;
	MlDocLineVec lines;
	uint64_t     line = 0;

	ml_doc_page_init (&p);
	MlDocLineVec_init (&lines);
	CHECK (ml_doc_parse (&p, lit (sample), &line) == ML_OK);
	CHECK (ml_doc_layout (&lines, &p, 20) == ML_OK);

	/* the title, the summary, a blank line, the heading */
	CHECK (lines.data [0].style == ML_DOC_STYLE_TITLE);
	CHECK (ml_slice_equal (ml_str_view (lines.data [0].text), lit ("A sample"))
	);
	CHECK (lines.data [1].style == ML_DOC_STYLE_TEXT);
	CHECK (lines.data [2].style == ML_DOC_STYLE_BLANK);
	CHECK (lines.data [3].style == ML_DOC_STYLE_HEADING);

	/* the paragraph is wrapped at 20 columns, every line fits */
	for (uint64_t i = 0; i < lines.len; i++)
	{
		CHECK (lines.data [i].text.len <= 20);
	}

	/* the link is where its words landed, with its target */
	{
		int32_t found = 0;

		for (uint64_t i = 0; i < lines.len; i++)
		{
			for (uint64_t k = 0; k < lines.data [i].links.len; k++)
			{
				MlDocLink l = lines.data [i].links.data [k];

				found = ml_slice_equal (
					ml_doc_line_target (&lines.data [i], k),
					lit ("other")
				);
				CHECK (found);
				CHECK (ml_slice_equal (
					ml_slice_sub (
						ml_str_view (lines.data [i].text),
						l.start,
						l.len
					),
					lit ("link")
				));
			}
		}

		CHECK (found);
	}

	/* items have a bullet and a hanging indent; examples show their results */
	{
		int32_t item = 0;
		int32_t hang = 0;
		int32_t input = 0;
		int32_t result = 0;

		for (uint64_t i = 0; i < lines.len; i++)
		{
			MlSlice t = ml_str_view (lines.data [i].text);

			item = item
				   || (lines.data [i].style == ML_DOC_STYLE_ITEM && t.len > 2
					   && t.data [0] == '-');
			hang = hang
				   || (lines.data [i].style == ML_DOC_STYLE_ITEM && t.len > 2
					   && t.data [0] == ' ');
			input = input
					|| (lines.data [i].style == ML_DOC_STYLE_INPUT
						&& ml_slice_equal (t, lit ("1+1")));
			result = result
					 || (lines.data [i].style == ML_DOC_STYLE_RESULT
						 && ml_slice_equal (t, lit ("= 2")));
		}

		CHECK (item && hang && input && result);
	}

	ml_doc_lines_free (&lines);
	ml_doc_page_free (&p);
}

static void test_builtin_pages (void)
{
	MlDocs   d;
	uint64_t page = 0;
	uint64_t line = 0;

	ml_docs_init (&d);
	CHECK (ml_docs_load (&d, NULL, &page, &line) == ML_OK);
	CHECK (d.pages.len >= 10);

	/* the pages of the files, then the table of contents and the examples */
	CHECK (d.pages.len == ml_doc_source_count + 2 + ml_example_count ());

	/* every page is found by its id, which is the name of its file */
	for (uint64_t i = 0; i < ml_doc_source_count; i++)
	{
		CHECK (ml_docs_find (&d, ml_str_view (d.pages.data [i].id)) == i);
		CHECK (ml_slice_equal (
			ml_str_view (d.pages.data [i].id),
			lit (ml_doc_sources [i].name)
		));
	}

	CHECK (ml_docs_find (&d, lit ("no-such-page")) == d.pages.len);
	ml_docs_free (&d);
}

/* The documentation must be right: links, reachability and every example. */
static void test_check_builtin (void)
{
	MlDocs   d;
	MlStr    report;
	uint64_t page = 0;
	uint64_t line = 0;
	uint64_t problems = 0;

	ml_docs_init (&d);
	ml_str_init (&report);
	CHECK (ml_docs_load (&d, NULL, &page, &line) == ML_OK);
	CHECK (ml_docs_check (&d, &report, &problems) == ML_OK);

	if (problems != 0)
	{
		printf ("%s", ml_str_cstr (report));
	}

	CHECK (problems == 0);
	ml_str_free (&report);
	ml_docs_free (&d);
}

static void test_check_examples (void)
{
	MlStr    report;
	uint64_t problems = 0;

	ml_str_init (&report);

	/* results as written, a definition, an error */
	CHECK (
		ml_docs_check_example (
			lit ("1/3+1/6\n=> 1/2\na = 2\na^2\n=> 4"),
			0,
			&report,
			&problems
		)
		== ML_OK
	);
	CHECK (problems == 0);
	CHECK (
		ml_docs_check_example (
			lit ("1/0\n=> ! division by zero"),
			0,
			&report,
			&problems
		)
		== ML_OK
	);
	CHECK (problems == 0);

	/* units are names of the physics domain: the same example is an error in
	   algebra and fine in physics */
	ml_str_clear (&report);
	CHECK (
		ml_docs_check_example (
			lit ("3 ft to m\n=> 1143/1250 m"),
			1,
			&report,
			&problems
		)
		== ML_OK
	);
	CHECK (problems == 0);
	CHECK (
		ml_docs_check_example (
			lit ("5 km\n=> 5000 m"),
			0,
			&report,
			&problems
		)
		== ML_OK
	);
	CHECK (problems == 1);
	ml_str_clear (&report);
	problems = 0;

	/* a result that is not what the evaluator gives is reported */
	CHECK (
		ml_docs_check_example (lit ("1+1\n=> 3"), 0, &report, &problems)
		== ML_OK
	);
	CHECK (problems == 1);
	CHECK (
		strstr (ml_str_cstr (report), "`1+1` gives `2`, the page says `3`")
		!= NULL
	);
	ml_str_free (&report);
}

/* Broken links, a page nobody links to, an example that is wrong. */
static void test_check_problems (void)
{
	MlDocs      d;
	MlStr       report;
	uint64_t    problems = 0;
	const char *texts [] = {
		"id: index\ntitle: Home\n\nSee [a](a) and [b](missing).\n",
		"id: a\ntitle: A\nsee: nowhere\n\n```example\n1+1\n=> 3\n```\n",
		"id: lonely\ntitle: Lonely\n\ntext\n"
	};

	ml_docs_init (&d);
	ml_str_init (&report);

	for (uint64_t i = 0; i < ml_count (texts); i++)
	{
		MlDocPage p;
		uint64_t  line;

		ml_doc_page_init (&p);
		CHECK (ml_doc_parse (&p, lit (texts [i]), &line) == ML_OK);
		CHECK (MlDocPageVec_push (&d.pages, p) == ML_OK);
	}

	CHECK (ml_docs_check (&d, &report, &problems) == ML_OK);
	CHECK (problems >= 4);
	CHECK (
		strstr (ml_str_cstr (report), "index: link: no such page missing")
		!= NULL
	);
	CHECK (
		strstr (ml_str_cstr (report), "a: see: no such page nowhere") != NULL
	);
	CHECK (
		strstr (
			ml_str_cstr (report),
			"lonely: cannot be reached from the index"
		)
		!= NULL
	);
	CHECK (
		strstr (ml_str_cstr (report), "`1+1` gives `2`, the page says `3`")
		!= NULL
	);
	ml_str_free (&report);
	ml_docs_free (&d);
}

/* The search, over pages made for it. */
static void test_search (void)
{
	MlDocs      d;
	MlDocHitVec hits;
	const char *texts [] = {
		"id: units\ntitle: Units\nsummary: Quantities.\nkeywords: meter, "
		"conversion\n\nA unit is a number with a name.\n",
		"id: syntax\ntitle: The syntax\nsummary: About unit names "
		"too.\nkeywords: operators\n\n# Operators\nThe plus and the minus.\n",
		"id: misc\ntitle: Miscellaneous\n\nNothing about it, except the word "
		"units here.\n"
	};

	ml_docs_init (&d);
	MlDocHitVec_init (&hits);

	for (uint64_t i = 0; i < ml_count (texts); i++)
	{
		MlDocPage p;
		uint64_t  line;

		ml_doc_page_init (&p);
		CHECK (ml_doc_parse (&p, lit (texts [i]), &line) == ML_OK);
		CHECK (MlDocPageVec_push (&d.pages, p) == ML_OK);
	}

	/* the title counts most, the text least, all words must match */
	CHECK (ml_docs_search (&d, lit ("unit"), &hits) == ML_OK);
	CHECK (hits.len == 3);
	CHECK (
		hits.data [0].page == 0 && hits.data [0].score > hits.data [1].score
	);
	CHECK (
		hits.data [1].page == 1 && hits.data [1].score > hits.data [2].score
	);
	CHECK (hits.data [2].page == 2);
	ml_doc_hits_free (&hits);

	/* the last word may be the beginning of a word, the others must be whole */
	CHECK (ml_docs_search (&d, lit ("oper"), &hits) == ML_OK);
	CHECK (hits.len == 1 && hits.data [0].page == 1);
	ml_doc_hits_free (&hits);
	CHECK (ml_docs_search (&d, lit ("oper minus"), &hits) == ML_OK);
	CHECK (hits.len == 0);
	ml_doc_hits_free (&hits);
	CHECK (ml_docs_search (&d, lit ("plus minus"), &hits) == ML_OK);
	CHECK (hits.len == 1 && hits.data [0].page == 1);

	/* the match shows the text around it */
	CHECK (
		strstr (ml_str_cstr (hits.data [0].where), "plus and the minus") != NULL
	);
	ml_doc_hits_free (&hits);

	/* case does not matter, nothing typed finds nothing */
	CHECK (
		ml_docs_search (&d, lit ("  UNIT "), &hits) == ML_OK && hits.len == 3
	);
	ml_doc_hits_free (&hits);
	CHECK (ml_docs_search (&d, lit (""), &hits) == ML_OK && hits.len == 0);
	ml_doc_hits_free (&hits);
	CHECK (ml_docs_search (&d, lit ("zebra"), &hits) == ML_OK && hits.len == 0);
	ml_doc_hits_free (&hits);

	ml_docs_free (&d);
}

/* The built in pages are the files of data/docs/, compiled in. */
static void test_pages_are_current (void)
{
	for (uint64_t i = 0; i < ml_doc_source_count; i++)
	{
		MlStr path;
		MlStr file;
		MlStr flat;

		ml_str_init (&path);
		ml_str_init (&file);
		ml_str_init (&flat);
		ml_str_appendf (&path, "data/docs/%s.md", ml_doc_sources [i].name);

		if (ml_fs_exists (ml_str_view (path)))
		{
			ml_fs_read_file (&file, ml_str_view (path));

			for (uint64_t k = 0; k < file.len; k++)
			{
				if (file.data [k] != '\r')
				{
					ml_str_append (
						&flat,
						ml_slice_sub (ml_str_view (file), k, 1)
					);
				}
			}

			if (!ml_slice_equal (
					ml_str_view (flat),
					lit (ml_doc_sources [i].text)
				))
			{
				printf (
					"  data/docs/%s.md is not what is compiled in: run python "
					"tools/gen_docs.py\n",
					ml_doc_sources [i].name
				);
			}

			CHECK (ml_slice_equal (
				ml_str_view (flat),
				lit (ml_doc_sources [i].text)
			));
		}

		ml_str_free (&flat);
		ml_str_free (&file);
		ml_str_free (&path);
	}
}

void run_docs_tests (void)
{
	test_parse ();
	test_parse_errors ();
	test_links ();
	test_layout ();
	test_builtin_pages ();
	test_check_builtin ();
	test_check_examples ();
	test_check_problems ();
	test_search ();
	test_pages_are_current ();
}
