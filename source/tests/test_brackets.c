#include "test.h"

#include <matlog/ui/brackets.h>

static MlBracketMatch match (const char *text, uint64_t cursor)
{
	return ml_bracket_match (ml_slice_cstr (text), cursor);
}

static void test_pairs (void)
{
	MlBracketMatch m = match ("(1+2)*3", 0);

	/* an opening bracket under the cursor finds its closer */
	CHECK (m.found && m.matched && m.at == 0 && m.partner == 4);

	/* a closing one under the cursor finds its opener */
	m = match ("(1+2)*3", 4);
	CHECK (m.found && m.matched && m.at == 4 && m.partner == 0);

	/* right after a closing bracket counts too, the one under wins */
	m = match ("(1+2)*3", 5);
	CHECK (m.found && m.matched && m.at == 4 && m.partner == 0);
	m = match ("(a)(b)", 3);
	CHECK (m.found && m.at == 3 && m.partner == 5);

	/* nesting */
	m = match ("((a)+(b))", 0);
	CHECK (m.matched && m.partner == 8);
	m = match ("((a)+(b))", 1);
	CHECK (m.matched && m.partner == 3);
	m = match ("((a)+(b))", 8);
	CHECK (m.matched && m.partner == 0);

	/* the kinds are paired apart */
	m = match ("[(a]", 0);
	CHECK (m.matched && m.partner == 3);
	m = match ("f{x}", 1);
	CHECK (m.matched && m.partner == 3);
}

static void test_unmatched (void)
{
	MlBracketMatch m = match ("(1+2", 0);

	CHECK (m.found && !m.matched && m.at == 0);
	m = match ("1+2)", 4);
	CHECK (m.found && !m.matched && m.at == 3);
	m = match ("1+2", 1);
	CHECK (!m.found);
	m = match ("", 0);
	CHECK (!m.found);
	m = match ("(", 5);
	CHECK (m.found && !m.matched);
}

void run_brackets_tests (void);

void run_brackets_tests (void)
{
	test_pairs ();
	test_unmatched ();
}
