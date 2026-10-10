#include "test.h"

#include <meth/proof/proof.h>
#include <meth/session/file.h>
#include <meth/session/results.h>
#include <meth/texts/texts.h>
#include <meth/ui/ghost.h>

#include <string.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

/* The mark of one line (from 0) of a proof; the counts go to *proved. */
static MlProofMark mark_of (const char *source, uint64_t line, uint64_t *proved)
{
	MlProofReport rep;
	MlProofMark   m = ML_PROOF_MARK_COUNT;

	ml_proof_report_init (&rep);
	CHECK (ml_proof_check (&rep, lit (source)) == ML_OK);

	if (line < rep.lines.len)
	{
		m = rep.lines.data [line].mark;
	}

	if (proved != NULL)
	{
		*proved = rep.proved;
	}

	ml_proof_report_free (&rep);
	return m;
}

/* 1 when the note of the line has the text in it. */
static int32_t note_has (const char *source, uint64_t line, const char *text)
{
	MlProofReport rep;
	int32_t       has = 0;

	ml_proof_report_init (&rep);
	CHECK (ml_proof_check (&rep, lit (source)) == ML_OK);

	if (line < rep.lines.len)
	{
		has = strstr (ml_str_cstr (rep.lines.data [line].note), text) != NULL;
	}

	ml_proof_report_free (&rep);
	return has;
}

static void test_valid (void)
{
	uint64_t proved = 0;

	CHECK (
		mark_of (
			"theorem double: x + x = 2x\n"
			"x + x = 2x  by simplify\n"
			"qed\n",
			2,
			&proved
		)
		== ML_PROOF_VALID
	);
	CHECK (proved == 1);

	/* a chain of equalities continues the right hand side */
	CHECK (
		mark_of (
			"theorem square: (x + 1)^2 = x^2 + 2x + 1\n"
			"(x + 1)^2 = (x + 1)(x + 1)  by simplify\n"
			"= x^2 + 2x + 1             by expand\n"
			"qed\n",
			3,
			&proved
		)
		== ML_PROOF_VALID
	);
	CHECK (proved == 1);
	CHECK (
		mark_of (
			"theorem square: (x + 1)^2 = x^2 + 2x + 1\n"
			"(x + 1)^2 = (x + 1)(x + 1)  by simplify\n"
			"... = x^2 + 2x + 1          by expand\n"
			"qed\n",
			2,
			NULL
		)
		== ML_PROOF_VALID
	);

	/* comments and blank lines have no verdict */
	CHECK (
		mark_of ("# a note\n\ntheorem t: 1 + 1 = 2\n", 0, NULL) == ML_PROOF_NONE
	);
	CHECK (
		mark_of ("# a note\n\ntheorem t: 1 + 1 = 2\n", 1, NULL) == ML_PROOF_NONE
	);
}

static void test_reasons (void)
{
	const char *hyp = "theorem t: x = 2 given x = 2\n"
					  "x = 2  by hypothesis\n"
					  "qed\n";

	CHECK (mark_of (hyp, 1, NULL) == ML_PROOF_VALID);
	CHECK (mark_of (hyp, 2, NULL) == ML_PROOF_VALID);

	/* the hypothesis may be written the other way round */
	CHECK (
		mark_of (
			"theorem t: 2 = x\n"
			"given x = 2\n"
			"2 = x  by hypothesis\n",
			2,
			NULL
		)
		== ML_PROOF_VALID
	);
	CHECK (
		mark_of (
			"theorem t: x = 3\n"
			"given x = 2\n"
			"x = 3  by hypothesis\n",
			2,
			NULL
		)
		== ML_PROOF_UNCHECKED
	);

	/* a step of the proof, and a lemma proved above */
	CHECK (
		mark_of (
			"theorem t: x + x = 2x\n"
			"x + x = 2x  by simplify\n"
			"2x = x + x  by step 1\n",
			2,
			NULL
		)
		== ML_PROOF_VALID
	);
	CHECK (
		mark_of (
			"theorem t: x + x = 2x\n"
			"x + x = 2x  by simplify\n"
			"x = 2x  by step 3\n",
			2,
			NULL
		)
		== ML_PROOF_INVALID
	);
	CHECK (
		mark_of (
			"theorem t: x + x = 2x\n"
			"x + x = 2x  by simplify\n"
			"qed\n"
			"theorem u: y + y = 2y\n"
			"y = 2y  by lemma t\n",
			4,
			NULL
		)
		== ML_PROOF_UNCHECKED
	);
	CHECK (
		mark_of (
			"theorem t: x + x = 2x\n"
			"x + x = 2x  by simplify\n"
			"qed\n"
			"theorem u: x + x = 2x\n"
			"2x = x + x  by lemma t\n",
			4,
			NULL
		)
		== ML_PROOF_VALID
	);
	CHECK (
		mark_of (
			"theorem u: x + x = 2x\n"
			"2x = x + x  by lemma nothing\n",
			1,
			NULL
		)
		== ML_PROOF_INVALID
	);
}

static void test_calculus (void)
{
	/* a derivative is a formula like any other */
	CHECK (
		mark_of (
			"theorem d: diff (x^3, x) = 3x^2\n"
			"diff (x^3, x) = 3x^2  by simplify\n",
			1,
			NULL
		) == ML_PROOF_VALID
	);
	CHECK (
		mark_of (
			"theorem d: diff (x^3, x) = 2x^2\n"
			"diff (x^3, x) = 2x^2  by simplify\n",
			1,
			NULL
		) == ML_PROOF_INVALID
	);
}

static void test_rules (void)
{
	/* a step that is the rule, at the one place where the sides differ */
	CHECK (
		mark_of (
			"theorem t: x * (y + z) = x * y + x * z\n"
			"x * (y + z) = x * y + x * z  by rule distribute\n",
			1,
			NULL
		) == ML_PROOF_VALID
	);
	CHECK (
		mark_of (
			"theorem t: 2 + x * (y + z) = 2 + x * y + x * z\n"
			"2 + x * (y + z) = 2 + x * y + x * z  by rule distribute\n",
			1,
			NULL
		) == ML_PROOF_VALID
	);

	/* read the other way, and with bigger formulas in place of the letters */
	CHECK (
		mark_of (
			"theorem t: (x + y)^2 = x^2 + 2 * x * y + y^2\n"
			"x^2 + 2 * x * y + y^2 = (x + y)^2  by rule square_of_sum\n",
			1,
			NULL
		) == ML_PROOF_VALID
	);
	CHECK (
		mark_of (
			"theorem t: sin (3x)^2 + cos (3x)^2 = 1\n"
			"sin (3x)^2 + cos (3x)^2 = 1  by rule pythagoras\n",
			1,
			NULL
		) == ML_PROOF_VALID
	);

	/* the rule is not the step: not checked, or refuted when it is false */
	CHECK (
		mark_of (
			"theorem t: x * (y + z) = x * y + x * z\n"
			"x * (y + z) = x * y + x * z  by rule difference_of_squares\n",
			1,
			NULL
		) == ML_PROOF_UNCHECKED
	);
	CHECK (
		mark_of (
			"theorem t: x * (y + z) = x * y + z\n"
			"x * (y + z) = x * y + z  by rule distribute\n",
			1,
			NULL
		) == ML_PROOF_INVALID
	);

	/* a rule that does not exist, and no name at all */
	CHECK (
		mark_of ("theorem t: x = x\nx = x  by rule magic\n", 1, NULL)
		== ML_PROOF_SYNTAX
	);
	CHECK (
		mark_of ("theorem t: x = x\nx = x  by rule\n", 1, NULL)
		== ML_PROOF_SYNTAX
	);
}

/* Every rule of the table is an identity: no sample point refutes it. */
static void test_rule_table (void)
{
	uint64_t count = ml_text_count ("rules");

	CHECK (count >= 10);

	for (uint64_t i = 0; i < count; i++)
	{
		const char *name = ml_text_nth_name ("rules", i);
		const char *text = ml_text_nth ("rules", i);
		MlStr       src;

		ml_str_init (&src);
		CHECK (
			ml_str_appendf (
				&src,
				"theorem t: %s\n%s  by expand\n%s  by rule %s\nqed\n",
				text,
				text,
				text,
				name
			) == ML_OK
		);
		CHECK (mark_of (ml_str_cstr (src), 1, NULL) != ML_PROOF_INVALID);
		CHECK (mark_of (ml_str_cstr (src), 2, NULL) == ML_PROOF_VALID);
		ml_str_free (&src);
	}
}

static void test_broken (void)
{
	uint64_t proved = 9;

	/* a wrong step is refuted with values */
	CHECK (
		mark_of (
			"theorem t: x + x = 3x\n"
			"x + x = 3x  by simplify\n"
			"qed\n",
			1,
			NULL
		)
		== ML_PROOF_INVALID
	);
	CHECK (note_has (
		"theorem t: x + x = 3x\n"
		"x + x = 3x  by simplify\n",
		1,
		"x = 2"
	));

	/* qed of a proof with a wrong step proves nothing */
	CHECK (
		mark_of (
			"theorem t: x + x = 3x\n"
			"x + x = 3x  by simplify\n"
			"qed\n",
			2,
			&proved
		)
		== ML_PROOF_INVALID
	);
	CHECK (proved == 0);

	/* the steps must reach the claim */
	CHECK (
		mark_of (
			"theorem t: x + x = 2x\n"
			"x + x = 2x  by simplify\n"
			"qed\n",
			2,
			NULL
		)
		== ML_PROOF_VALID
	);
	CHECK (
		mark_of (
			"theorem t: x + x = 3x\n"
			"x + x = x * 2  by simplify\n"
			"qed\n",
			2,
			NULL
		)
		== ML_PROOF_INVALID
	);

	/* no reason, an unknown reason, a reason the engine cannot decide */
	CHECK (
		mark_of ("theorem t: x = x\nx = x\n", 1, NULL) == ML_PROOF_UNCHECKED
	);
	CHECK (
		mark_of ("theorem t: x = x\nx = x  by magic\n", 1, NULL)
		== ML_PROOF_SYNTAX
	);
	CHECK (
		mark_of (
			"theorem t: (x + 1)^2 = x^2 + 2x + 1\n"
			"(x + 1)^2 = x^2 + 2x + 1  by simplify\n",
			1,
			NULL
		)
		== ML_PROOF_UNCHECKED
	);

	/* sorry is reported and never counts as proved */
	CHECK (
		mark_of (
			"theorem t: x = x\n"
			"x = x  by sorry\n"
			"qed\n",
			1,
			NULL
		)
		== ML_PROOF_SORRY
	);
	CHECK (
		mark_of (
			"theorem t: x = x\n"
			"x = x  by sorry\n"
			"qed\n",
			2,
			&proved
		)
		== ML_PROOF_SORRY
	);
	CHECK (proved == 0);

	/* a step cannot use a step that is not valid */
	CHECK (
		mark_of (
			"theorem t: x = x\n"
			"x = x + 1  by sorry\n"
			"x = x + 1  by step 1\n",
			2,
			NULL
		)
		== ML_PROOF_UNCHECKED
	);
}

static void test_syntax (void)
{
	CHECK (mark_of ("x = 1  by simplify\n", 0, NULL) == ML_PROOF_SYNTAX);
	CHECK (mark_of ("qed\n", 0, NULL) == ML_PROOF_SYNTAX);
	CHECK (mark_of ("theorem t x = 1\n", 0, NULL) == ML_PROOF_SYNTAX);
	CHECK (mark_of ("theorem t: x\n", 0, NULL) == ML_PROOF_SYNTAX);
	CHECK (mark_of ("theorem t: x + = 1\n", 0, NULL) == ML_PROOF_SYNTAX);
	CHECK (
		mark_of ("theorem t: x = 1\n= 2  by simplify\n", 1, NULL)
		== ML_PROOF_SYNTAX
	);
	CHECK (
		mark_of ("theorem t: x = 1\nx + 1  by simplify\n", 1, NULL)
		== ML_PROOF_SYNTAX
	);

	/* a theorem without qed is open, not an error */
	CHECK (
		mark_of ("theorem t: x = x\nx = x  by simplify\n", 1, NULL)
		== ML_PROOF_VALID
	);
}

static void test_wrong_step_everywhere (void)
{
	/* a wrong step at each position is found at its own line */
	const char *good [3] = { "x + x = 2x  by simplify\n",
							 "2x = x * 2  by simplify\n",
							 "x * 2 = 2 * x  by simplify\n" };
	const char *bad [3]  = { "x + x = 3x  by simplify\n",
							 "2x = x * 3  by simplify\n",
							 "x * 2 = 5 * x  by simplify\n" };

	for (uint64_t pos = 0; pos < 3; pos++)
	{
		MlStr src;

		ml_str_init (&src);
		CHECK (
			ml_str_append (&src, lit ("theorem t: x + x = 2 * x\n")) == ML_OK
		);

		for (uint64_t i = 0; i < 3; i++)
		{
			CHECK (
				ml_str_append (
					&src,
					lit (ml_choose (i == pos, bad [i], good [i]))
				)
				== ML_OK
			);
		}

		for (uint64_t i = 0; i < 3; i++)
		{
			MlProofMark m = mark_of (ml_str_cstr (src), 1 + i, NULL);

			CHECK (m == ml_choose (i == pos, ML_PROOF_INVALID, ML_PROOF_VALID));
		}

		ml_str_free (&src);
	}
}

/* The proof domain: the verdicts are the results of the lines. */
static void test_domain (void)
{
	MlSession s;
	MlResults res;
	MlResultView v;

	ml_session_init (&s);
	ml_results_init (&res);
	CHECK (
		ml_file_parse (
			&s,
			lit (
				"m:proof theorem t: x + x = 3x\n"
				"m:proof x + x = 2x  by simplify\n"
				"m:proof x + x = 3x  by simplify\n"
				"m:proof qed\n"
			)
		) == ML_OK
	);
	CHECK (ml_results_update (&res, &s, 0) == ML_OK);
	CHECK (ml_results_get (&res, 0).kind == ML_RESULT_NONE);
	v = ml_results_get (&res, 1);
	CHECK (v.kind == ML_RESULT_VALUE);
	CHECK (v.text.len > 0 && (uint8_t)v.text.data [0] == 0xE2);
	v = ml_results_get (&res, 2);
	CHECK (v.kind == ML_RESULT_ERROR);
	CHECK (strstr (v.text.data, "x = 2") != NULL);
	CHECK (ml_results_get (&res, 3).kind == ML_RESULT_ERROR);

	/* fixing the line from the changed line down gives the verdicts again */
	CHECK (
		ml_session_replace (
			&s,
			2,
			ML_LINE_MATH,
			ML_DOMAIN_PROOF,
			lit ("x + x = 2x  by simplify")
		) == ML_OK
	);
	CHECK (ml_results_update (&res, &s, 2) == ML_OK);
	CHECK (ml_results_get (&res, 2).kind == ML_RESULT_VALUE);

	ml_results_free (&res);
	ml_session_free (&s);
}

/* The preview of the step being typed. */
static void test_ghost (void)
{
	MlSession s;
	MlStr     out;
	MlGhost   g;

	ml_session_init (&s);
	ml_str_init (&out);
	CHECK (
		ml_file_parse (&s, lit ("m:proof theorem t: x + x = 2x\n"))
		== ML_OK
	);

	g = ml_ghost_proof (&out, &s, 1, lit ("x + x = 2x  by simplify"));
	CHECK (g.result == ML_OK && g.shown);
	CHECK ((uint8_t)out.data [0] == 0xE2);

	ml_str_clear (&out);
	g = ml_ghost_proof (&out, &s, 1, lit ("x + x = 3x  by simplify"));
	CHECK (g.shown && strstr (ml_str_cstr (out), "x = 2") != NULL);

	/* nothing while the line does not read yet */
	ml_str_clear (&out);
	g = ml_ghost_proof (&out, &s, 1, lit ("x + x = 2x  by si"));
	CHECK (g.shown == 0 || out.data [0] != '\xE2');
	ml_str_clear (&out);
	g = ml_ghost_proof (&out, &s, 1, lit ("x + "));
	CHECK (!g.shown);

	ml_str_free (&out);
	ml_session_free (&s);
}

void run_proof_tests (void);

void run_proof_tests (void)
{
	CHECK (ml_texts_reload ("en", NULL) == ML_OK);
	test_valid ();
	test_reasons ();
	test_calculus ();
	test_rules ();
	test_rule_table ();
	test_broken ();
	test_syntax ();
	test_wrong_step_everywhere ();
	test_domain ();
	test_ghost ();
}
