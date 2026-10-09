#include <matlog/ui/rewrite.h>

#include <matlog/core/utf8.h>

#include <string.h>

#define DOMAINS_ALL 0xFFu
#define BIT(d)      (1u << (d))

#define MAX_NAME   16 /* longest \name or word that can match */
#define MAX_DIGITS 16 /* longest superscript run */

typedef struct Rule
{
	const char *from;
	uint32_t   cp;      /* what it turns into */
	uint32_t   domains; /* bit mask of the domains the rule is active in */
	int32_t    eat;     /* the character that completed the token is swallowed */
} Rule;

/* Rules that fire as soon as the last character of "from" is typed. */
static const Rule literals [] =
{
	{ .from = "<->", .cp = 0x2194, .domains = DOMAINS_ALL,           .eat = 0 }, /* left right arrow */
	{ .from = "<=",  .cp = 0x2264, .domains = DOMAINS_ALL,           .eat = 0 }, /* less or equal */
	{ .from = ">=",  .cp = 0x2265, .domains = DOMAINS_ALL,           .eat = 0 }, /* greater or equal */
	{ .from = "!=",  .cp = 0x2260, .domains = DOMAINS_ALL,           .eat = 0 }, /* not equal */
	{ .from = "->",  .cp = 0x2192, .domains = DOMAINS_ALL,           .eat = 0 }, /* arrow */
	{ .from = "=>",  .cp = 0x21D2, .domains = DOMAINS_ALL,           .eat = 0 }, /* implies */
	{ .from = "&&",  .cp = 0x2227, .domains = BIT (ML_DOMAIN_BOOLEAN), .eat = 0 }, /* and */
	{ .from = "||",  .cp = 0x2228, .domains = BIT (ML_DOMAIN_BOOLEAN), .eat = 0 }, /* or */
	{ .from = "~",   .cp = 0x00AC, .domains = BIT (ML_DOMAIN_BOOLEAN), .eat = 0 }  /* not */
};

/* Whole words, rewritten when a non word character follows. */
static const Rule words [] =
{
	{ .from = "pi",     .cp = 0x03C0, .domains = DOMAINS_ALL,             .eat = 0 },
	{ .from = "lambda", .cp = 0x03BB, .domains = BIT (ML_DOMAIN_LAMBDA), .eat = 1 }
};

/* Names after a backslash, rewritten when a non word character follows. */
static const Rule escapes [] =
{
	{ .from = "alpha",   .cp = 0x03B1, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "beta",    .cp = 0x03B2, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "gamma",   .cp = 0x03B3, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "delta",   .cp = 0x03B4, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "epsilon", .cp = 0x03B5, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "zeta",    .cp = 0x03B6, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "eta",     .cp = 0x03B7, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "theta",   .cp = 0x03B8, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "iota",    .cp = 0x03B9, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "kappa",   .cp = 0x03BA, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "lambda",  .cp = 0x03BB, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "mu",      .cp = 0x03BC, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "nu",      .cp = 0x03BD, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "xi",      .cp = 0x03BE, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "omicron", .cp = 0x03BF, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "pi",      .cp = 0x03C0, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "rho",     .cp = 0x03C1, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "sigma",   .cp = 0x03C3, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "tau",     .cp = 0x03C4, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "upsilon", .cp = 0x03C5, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "phi",     .cp = 0x03C6, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "chi",     .cp = 0x03C7, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "psi",     .cp = 0x03C8, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "omega",   .cp = 0x03C9, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "Gamma",   .cp = 0x0393, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "Delta",   .cp = 0x0394, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "Theta",   .cp = 0x0398, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "Lambda",  .cp = 0x039B, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "Xi",      .cp = 0x039E, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "Pi",      .cp = 0x03A0, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "Sigma",   .cp = 0x03A3, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "Phi",     .cp = 0x03A6, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "Psi",     .cp = 0x03A8, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "Omega",   .cp = 0x03A9, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "infty",   .cp = 0x221E, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "pm",      .cp = 0x00B1, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "times",   .cp = 0x00D7, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "cdot",    .cp = 0x00B7, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "leq",     .cp = 0x2264, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "geq",     .cp = 0x2265, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "neq",     .cp = 0x2260, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "to",      .cp = 0x2192, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "in",      .cp = 0x2208, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "forall",  .cp = 0x2200, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "exists",  .cp = 0x2203, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "partial", .cp = 0x2202, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "nabla",   .cp = 0x2207, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "int",     .cp = 0x222B, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "sum",     .cp = 0x2211, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "sqrt",    .cp = 0x221A, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "wedge",   .cp = 0x2227, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "vee",     .cp = 0x2228, .domains = DOMAINS_ALL, .eat = 0 },
	{ .from = "neg",     .cp = 0x00AC, .domains = DOMAINS_ALL, .eat = 0 }
};

/* Superscript forms of the digits 0 to 9. */
static const uint32_t superscript_digits [10] =
{
	0x2070, 0x00B9, 0x00B2, 0x00B3, 0x2074, 0x2075, 0x2076, 0x2077, 0x2078, 0x2079
};

#define SUPERSCRIPT_MINUS  0x207B
#define SQRT_SIGN          0x221A

/* What a completed token turns into and where it sits in the line. */
typedef struct Match
{
	MlResult result;
	int32_t  found;
	uint64_t from; /* start of the text to replace */
	uint64_t to;   /* end of it, the cursor */
	int32_t  eat;  /* the trigger character is swallowed */
	MlStr    text; /* the replacement, owned by the caller */
} Match;

static Match match_new (void)
{
	Match m =
	{
		.result = ML_OK,
		.found  = 0,
		.from   = 0,
		.to     = 0,
		.eat    = 0,
		.text   =
		{
			.data = NULL,
			.len  = 0,
			.cap  = 0
		}
	};

	return m;
}

static int32_t domain_active (uint32_t domains, MlDomain domain)
{
	return (domains & BIT (domain)) != 0;
}

static int32_t ends_with (MlSlice s, const char *suffix)
{
	MlSlice suf = ml_slice_cstr (suffix);

	return s.len >= suf.len && ml_slice_equal (ml_slice_sub (s, s.len - suf.len, suf.len), suf);
}

static int32_t is_digit (char c)
{
	return c >= '0' && c <= '9';
}

static int32_t is_letter (char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* The text before the cursor. */
static MlSlice before_cursor (const MlLineEdit *e)
{
	return ml_slice_sub (ml_str_view (e->text), 0, e->cursor);
}

/* x^2 and x^-10 at the end of before. Needs a non word, non '.' trigger. */
static Match find_superscript (MlDomain domain, MlSlice before, uint32_t trigger)
{
	Match    m       = match_new ();
	uint64_t i       = before.len;
	uint64_t digits  = 0;
	int32_t  allowed = (
		   domain != ML_DOMAIN_BOOLEAN
		&& domain != ML_DOMAIN_PGA2
		&& domain != ML_DOMAIN_PGA3
		&& trigger != '.'
	);

	while (allowed && i > 0 && is_digit (before.data [i - 1]))
	{
		i--;
		digits++;
	}

	if (allowed && digits > 0 && digits <= MAX_DIGITS)
	{
		int32_t  minus = i > 0 && before.data [i - 1] == '-';
		uint64_t caret = ml_choose (
			minus,
			i - 1,
			i
		);

		if (caret > 0 && before.data [caret - 1] == '^')
		{
			m.found = 1;
			m.from  = caret - 1;
			m.to    = before.len;

			if (minus)
			{
				m.result = ml_str_append_cp (&m.text, SUPERSCRIPT_MINUS);
			}

			for (uint64_t k = i; k < before.len && m.result == ML_OK; k++)
			{
				m.result = ml_str_append_cp (&m.text, superscript_digits [before.data [k] - '0']);
			}
		}
	}

	return m;
}

/* \name at the end of before. */
static Match find_escape (MlDomain domain, MlSlice before)
{
	Match    m = match_new ();
	uint64_t i = before.len;

	while (i > 0 && before.len - i <= MAX_NAME && is_letter (before.data [i - 1]))
	{
		i--;
	}

	if (i > 0 && i < before.len && before.data [i - 1] == '\\')
	{
		MlSlice name = ml_slice_sub (before, i, before.len - i);

		for (uint64_t k = 0; k < sizeof escapes / sizeof escapes [0] && !m.found; k++)
		{
			if (domain_active (escapes [k].domains, domain) && ml_slice_equal (name, ml_slice_cstr (escapes [k].from)))
			{
				m.found  = 1;
				m.from   = i - 1;
				m.to     = before.len;
				m.result = ml_str_append_cp (&m.text, escapes [k].cp);
			}
		}
	}

	return m;
}

/* A whole word at the end of before; it must not follow a backslash. */
static Match find_word (MlDomain domain, MlSlice before, uint32_t trigger)
{
	Match    m     = match_new ();
	uint64_t start = before.len;

	while (start > 0 && ml_cp_is_word (ml_utf8_decode (ml_slice_sub (before, ml_utf8_prev (before, start), start - ml_utf8_prev (before, start))).cp))
	{
		start = ml_utf8_prev (before, start);
	}

	if (start < before.len && !(start > 0 && before.data [start - 1] == '\\'))
	{
		MlSlice word = ml_slice_sub (before, start, before.len - start);

		for (uint64_t k = 0; k < sizeof words / sizeof words [0] && !m.found; k++)
		{
			int32_t eat = words [k].eat && trigger == ' ';

			if (
				   domain_active (words [k].domains, domain)
				&& ml_slice_equal (word, ml_slice_cstr (words [k].from))
				&& (!words [k].eat || eat)
			)
			{
				m.found  = 1;
				m.from   = start;
				m.to     = before.len;
				m.eat    = eat;
				m.result = ml_str_append_cp (&m.text, words [k].cp);
			}
		}
	}

	return m;
}

/* The rewrite a non word character completes, looking at the text before the cursor. */
static Match find_completion (MlDomain domain, MlSlice before, uint32_t trigger)
{
	Match m = find_superscript (domain, before, trigger);

	if (!m.found && m.result == ML_OK)
	{
		ml_str_free (&m.text);
		m = find_escape (domain, before);
	}

	if (!m.found && m.result == ML_OK)
	{
		ml_str_free (&m.text);
		m = find_word (domain, before, trigger);
	}

	return m;
}

/* sqrt(...) ending with the ')' that was just inserted. */
static Match find_sqrt (MlSlice before)
{
	Match    m     = match_new ();
	uint64_t depth = 0;
	uint64_t i     = before.len;
	int32_t  open  = 0;

	while (i > 0 && !open)
	{
		char c = before.data [i - 1];

		depth += ml_choose (
			c == ')',
			1,
			0
		);
		depth -= ml_choose (
			c == '(',
			1,
			0
		);
		open = c == '(' && depth == 0;
		i--;
	}

	if (open && i >= 4 && ml_slice_equal (ml_slice_sub (before, i - 4, 4), ml_slice_cstr ("sqrt")))
	{
		uint64_t start   = i - 4;
		MlSlice  operand = ml_slice_sub (before, i + 1, before.len - i - 2);
		int32_t  simple  = operand.len > 0;
		int32_t  bounded = start == 0 || !ml_cp_is_word (ml_utf8_decode (ml_slice_sub (before, ml_utf8_prev (before, start), start - ml_utf8_prev (before, start))).cp);
		uint64_t pos     = 0;

		while (pos < operand.len && simple)
		{
			MlDecoded d = ml_utf8_decode (ml_slice_sub (operand, pos, operand.len - pos));

			simple = ml_cp_is_word (d.cp);
			pos   += d.size;
		}

		if (bounded && operand.len > 0)
		{
			m.found  = 1;
			m.from   = start;
			m.to     = before.len;
			m.result = ml_str_append_cp (&m.text, SQRT_SIGN);

			if (m.result == ML_OK && !simple)
			{
				m.result = ml_str_append (&m.text, ml_slice_cstr ("("));
			}

			if (m.result == ML_OK)
			{
				m.result = ml_str_append (&m.text, operand);
			}

			if (m.result == ML_OK && !simple)
			{
				m.result = ml_str_append (&m.text, ml_slice_cstr (")"));
			}
		}
	}

	return m;
}

/* The longest literal rule that the text before the cursor ends with. */
static Match find_literal (MlDomain domain, MlSlice before)
{
	Match    m    = match_new ();
	uint64_t best = 0;
	uint32_t cp   = 0;

	for (uint64_t k = 0; k < sizeof literals / sizeof literals [0]; k++)
	{
		uint64_t len = strlen (literals [k].from);

		if (domain_active (literals [k].domains, domain) && len > best && ends_with (before, literals [k].from))
		{
			best = len;
			cp   = literals [k].cp;
		}
	}

	if (best > 0)
	{
		m.found  = 1;
		m.from   = before.len - best;
		m.to     = before.len;
		m.result = ml_str_append_cp (&m.text, cp);
	}

	return m;
}

/*
 * Replaces [m.from, m.to) of the line with m.text and arms the undo.
 * The new text goes in first, so a failure leaves the line untouched.
 */
static MlResult apply (MlRewriter *rw, MlLineEdit *e, Match m, MlSlice trigger)
{
	MlResult r = ML_OK;

	ml_str_clear (&rw->original);
	r = ml_str_append (&rw->original, ml_slice_sub (ml_str_view (e->text), m.from, m.to - m.from));

	if (r == ML_OK && m.eat)
	{
		r = ml_str_append (&rw->original, trigger);
	}

	if (r == ML_OK)
	{
		r = ml_str_insert (&e->text, m.to, ml_str_view (m.text));
	}

	if (r == ML_OK)
	{
		ml_str_erase (&e->text, m.from, m.to - m.from);
		e->cursor        = m.from + m.text.len;
		rw->start        = m.from;
		rw->replaced_len = m.text.len;
		rw->trigger_len  = 0;
		rw->armed        = 1;
	}

	return r;
}

void ml_rewriter_init (MlRewriter *rw, MlDomain domain)
{
	rw->domain = domain;
	ml_str_init (&rw->original);
	rw->start        = 0;
	rw->replaced_len = 0;
	rw->trigger_len  = 0;
	rw->armed        = 0;
}

void ml_rewriter_free (MlRewriter *rw)
{
	ml_str_free (&rw->original);
	rw->armed = 0;
}

void ml_rewriter_set_domain (MlRewriter *rw, MlDomain domain)
{
	rw->domain = domain;
	rw->armed  = 0;
}

MlResult ml_rewriter_insert_cp (MlRewriter *rw, MlLineEdit *e, uint32_t cp)
{
	MlResult  r       = ML_OK;
	MlEncoded enc     = ml_utf8_encode (cp);
	Match     done    = match_new ();
	Match     after   = match_new ();
	MlSlice   trigger = ml_slice (enc.bytes, enc.size);

	rw->armed = 0;

	if (enc.size == 0)
	{
		r = ML_ERR_UTF8;
	}

	if (r == ML_OK && !ml_cp_is_word (cp))
	{
		done = find_completion (rw->domain, before_cursor (e), cp);
		r    = done.result;
	}

	if (r == ML_OK && done.found)
	{
		r = apply (rw, e, done, trigger);
	}

	if (r == ML_OK && !(done.found && done.eat))
	{
		r = ml_lineedit_insert (e, trigger);

		if (r == ML_OK && done.found)
		{
			rw->trigger_len = enc.size;
		}
	}

	/* rewrites that the inserted character itself completes */
	if (r == ML_OK && !(done.found && done.eat))
	{
		after = ml_choose (
			cp == ')',
			find_sqrt (before_cursor (e)),
			find_literal (rw->domain, before_cursor (e))
		);
		r = after.result;
	}

	if (r == ML_OK && after.found)
	{
		r = apply (rw, e, after, trigger);
	}

	ml_str_free (&done.text);
	ml_str_free (&after.text);
	return r;
}

MlResult ml_rewriter_backspace (MlRewriter *rw, MlLineEdit *e)
{
	MlResult r = ML_OK;

	if (rw->armed && e->cursor == rw->start + rw->replaced_len + rw->trigger_len)
	{
		r = ml_str_insert (&e->text, rw->start, ml_str_view (rw->original));

		if (r == ML_OK)
		{
			ml_str_erase (&e->text, rw->start + rw->original.len, rw->replaced_len);
			e->cursor = rw->start + rw->original.len + rw->trigger_len;
		}
	}

	else
	{
		ml_lineedit_backspace (e);
	}

	rw->armed = 0;
	return r;
}

MlResult ml_rewriter_flush (MlRewriter *rw, MlLineEdit *e)
{
	MlResult r    = ML_OK;
	Match    done = find_completion (rw->domain, before_cursor (e), ' ');

	r = done.result;

	/* a swallowing rule needs its space, the flush does not type one */
	if (r == ML_OK && done.found && !done.eat)
	{
		r = apply (rw, e, done, ml_slice ("", 0));
	}

	ml_str_free (&done.text);
	return r;
}

MlEditResult ml_rewriter_handle_key (MlRewriter *rw, MlLineEdit *e, MlKey key)
{
	MlEditResult res = { .result = ML_OK, .handled = 1 };

	if (key.code == ML_KEY_CHAR && !(key.mods & (ML_MOD_CTRL | ML_MOD_ALT)))
	{
		res.result = ml_rewriter_insert_cp (rw, e, key.cp);
	}

	else if (key.code == ML_KEY_BACKSPACE && key.mods == 0)
	{
		res.result = ml_rewriter_backspace (rw, e);
	}

	else
	{
		rw->armed = 0;
		res       = ml_lineedit_handle_key (e, key);
	}

	return res;
}
