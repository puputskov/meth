#include <matlog/ui/brackets.h>

/* The closer of an opening bracket, the opener of a closing one, else 0. */
static uint8_t pair_of (uint8_t b, int32_t *opening)
{
	uint8_t other = 0;

	*opening = 1;
	other    = ml_choose (b == '(', ')', other);
	other    = ml_choose (b == '[', ']', other);
	other    = ml_choose (b == '{', '}', other);

	if (other == 0)
	{
		*opening = 0;
		other    = ml_choose (b == ')', '(', other);
		other    = ml_choose (b == ']', '[', other);
		other    = ml_choose (b == '}', '{', other);
	}

	return other;
}

MlBracketMatch ml_bracket_match (MlSlice text, uint64_t cursor)
{
	MlBracketMatch m =
	{
		.found   = 0,
		.matched = 0,
		.at      = 0,
		.partner = 0
	};

	int32_t  opening = 0;
	uint64_t at      = ml_choose (cursor < text.len, cursor, text.len);
	uint8_t  other   = ml_choose (
		at < text.len,
		pair_of ((uint8_t)text.data [at], &opening),
		0
	);

	/* not under the cursor: the bracket right before it */
	if (other == 0 && at > 0 && at <= text.len)
	{
		at    = at - 1;
		other = pair_of ((uint8_t)text.data [at], &opening);
	}

	if (other != 0)
	{
		uint8_t  self  = (uint8_t)text.data [at];
		uint64_t depth = 1;
		uint64_t i     = at;

		m.found = 1;
		m.at    = at;

		while (depth > 0 && ml_choose (opening, i + 1 < text.len, i > 0))
		{
			i = ml_choose (opening, i + 1, i - 1);

			if ((uint8_t)text.data [i] == self)
			{
				depth++;
			}

			else if ((uint8_t)text.data [i] == other)
			{
				depth--;
			}
		}

		if (depth == 0)
		{
			m.matched = 1;
			m.partner = i;
		}
	}

	return m;
}
