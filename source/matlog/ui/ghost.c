#include <matlog/ui/ghost.h>

#include <matlog/core/mem.h>
#include <matlog/logic/bool.h>
#include <matlog/logic/lambda.h>
#include <matlog/math/eval.h>
#include <matlog/proof/proof.h>

/* The preview of a line of the boolean or the lambda domain. */
static MlGhost ghost_logic (
	MlStr           *out,
	const MlResults *results,
	MlDomain        domain,
	uint64_t        idx,
	MlSlice         text
)
{
	MlGhost g = {
		.result = ML_OK,
		.shown  = 0
	};

	MlLogicOut o;
	uint64_t   count = ml_choose (
		idx < results->lines.len,
		results->lines.data [idx].logic,
		results->defs.len
	);

	ml_logic_out_init (&o);
	g.result = ml_choose (
		domain == ML_DOMAIN_BOOLEAN,
		ml_bool_run (&results->defs, count, text, &o),
		ml_lambda_run (&results->defs, count, text, &o)
	);

	if (g.result == ML_OK && o.error == ML_LOGIC_OK && o.value)
	{
		g.result = ml_str_append (out, ml_str_view (o.text));
		g.shown  = g.result == ML_OK;
	}

	ml_logic_out_free (&o);
	return g;
}

/* The preview of a line of the algebra domain. */
static MlGhost ghost_algebra (
	MlStr           *out,
	MlStr           *block,
	const MlResults *results,
	uint64_t        idx,
	MlSlice         text,
	MlDomain        domain
)
{
	MlGhost g = {
		.result = ML_OK,
		.shown  = 0
	};

	MlParseResult p     = ml_parse_mode (
		text,
		ml_domain_units (domain),
		ml_domain_pga (domain)
	);
	MlArena       *arena = ml_arena_app ();
	MlArenaMark   mark  = ml_arena_mark (arena);

	if (p.error.code == ML_PARSE_OK)
	{
		uint64_t snapshot = ml_choose (
			idx < results->lines.len,
			results->lines.data [idx].snapshot,
			ml_env_snapshot (&results->env)
		);
		MlValue v = ml_eval (
			arena,
			&results->env,
			snapshot,
			text,
			&p.ast
		);

		if (
			   v.kind == ML_VALUE_EXACT
			|| v.kind == ML_VALUE_APPROX
			|| v.kind == ML_VALUE_MATRIX
			|| v.kind == ML_VALUE_EXPR
			|| v.kind == ML_VALUE_QUANTITY
			|| v.kind == ML_VALUE_COMPLEX
			|| v.kind == ML_VALUE_QUAT
			|| v.kind == ML_VALUE_BOOL
			|| v.kind == ML_VALUE_LIST
			|| v.kind == ML_VALUE_MV
		)
		{
			g.result = ml_value_format (out, arena, v);

			if (g.result == ML_OK && block != NULL && v.kind == ML_VALUE_MATRIX)
			{
				g.result = ml_mat_typeset (block, v.matrix);
			}

			g.shown = g.result == ML_OK;
		}
	}

	ml_ast_free (&p.ast);
	ml_arena_rewind (arena, mark);
	return g;
}

MlGhost ml_ghost_block (
	MlStr           *out,
	MlStr           *block,
	const MlResults *results,
	MlDomain        domain,
	uint64_t        idx,
	MlSlice         text
)
{
	MlGhost g = {
		.result = ML_OK,
		.shown  = 0
	};

	if (domain == ML_DOMAIN_BOOLEAN || domain == ML_DOMAIN_LAMBDA)
	{
		g = ghost_logic (out, results, domain, idx, text);
	}

	else if (domain != ML_DOMAIN_PROOF)
	{
		g = ghost_algebra (out, block, results, idx, text, domain);
	}

	return g;
}

MlGhost ml_ghost_proof (
	MlStr           *out,
	const MlSession *session,
	uint64_t        idx,
	MlSlice         text
)
{
	MlGhost       g = {
		.result = ML_OK,
		.shown  = 0
	};

	MlStr         source;
	MlProofReport rep;
	uint64_t      above = ml_choose (
		idx < session->lines.len,
		idx,
		session->lines.len
	);

	ml_str_init (&source);
	ml_proof_report_init (&rep);

	for (uint64_t i = 0; i < above && g.result == ML_OK; i++)
	{
		MlLineView view = ml_session_get (session, i);

		if (view.kind == ML_LINE_MATH && view.domain == ML_DOMAIN_PROOF)
		{
			g.result = ml_str_append (&source, view.text);
		}

		g.result = ml_choose (
			g.result == ML_OK,
			ml_str_append (&source, ml_slice_cstr ("\n")),
			g.result
		);
	}

	g.result = ml_choose (
		g.result == ML_OK,
		ml_str_append (&source, text),
		g.result
	);

	if (g.result == ML_OK)
	{
		g.result = ml_proof_check (&rep, ml_str_view (source));
	}

	if (g.result == ML_OK && rep.lines.len > 0)
	{
		const MlProofLine *last = &rep.lines.data [rep.lines.len - 1];
		const char        *sign = "";

		switch (last->mark)
		{
			case ML_PROOF_VALID:
			{
				sign = "\xE2\x9C\x93 ";
			} break;

			case ML_PROOF_UNCHECKED:
			case ML_PROOF_SORRY:
			{
				sign = "? ";
			} break;

			case ML_PROOF_INVALID:
			{
				sign = "\xE2\x9C\x97 ";
			} break;

			default:
			{
				sign = "";
			} break;
		}

		if (sign [0] != '\0')
		{
			g.result = ml_str_append (out, ml_slice_cstr (sign));
			g.result = ml_choose (
				g.result == ML_OK,
				ml_str_append (out, ml_str_view (last->note)),
				g.result
			);
			g.shown  = g.result == ML_OK;
		}
	}

	ml_proof_report_free (&rep);
	ml_str_free (&source);
	return g;
}

MlGhost ml_ghost (
	MlStr           *out,
	const MlResults *results,
	MlDomain        domain,
	uint64_t        idx,
	MlSlice         text
)
{
	return ml_ghost_block (out, NULL, results, domain, idx, text);
}
