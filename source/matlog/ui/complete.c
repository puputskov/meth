#include <matlog/ui/complete.h>

#include <matlog/session/session.h>

typedef enum ArgKind
{
	ARG_NONE = 0,
	ARG_DOMAIN,
	ARG_PGA_DIM,
	ARG_PATH /* files are not completed yet */
} ArgKind;

typedef struct Command
{
	const char *name;
	ArgKind    arg;
	int32_t    needs_arg; /* the command is useless without an argument */
} Command;

/* The commands of the editor, in the order they are offered. */
static const Command commands [] =
{
	{ .name = "q",     .arg = ARG_NONE,    .needs_arg = 0 },
	{ .name = "quit",  .arg = ARG_NONE,    .needs_arg = 0 },
	{ .name = "q!",    .arg = ARG_NONE,    .needs_arg = 0 },
	{ .name = "w",     .arg = ARG_PATH,    .needs_arg = 0 },
	{ .name = "write", .arg = ARG_PATH,    .needs_arg = 0 },
	{ .name = "wq",    .arg = ARG_PATH,    .needs_arg = 0 },
	{ .name = "x",     .arg = ARG_PATH,    .needs_arg = 0 },
	{ .name = "open",  .arg = ARG_PATH,    .needs_arg = 1 },
	{ .name = "open!", .arg = ARG_PATH,    .needs_arg = 1 },
	{ .name = "e",     .arg = ARG_PATH,    .needs_arg = 1 },
	{ .name = "e!",    .arg = ARG_PATH,    .needs_arg = 1 },
	{ .name = "mode",  .arg = ARG_DOMAIN,  .needs_arg = 1 },
	{ .name = "pga",   .arg = ARG_PGA_DIM, .needs_arg = 1 }
};

static MlCompletion completion_new (uint64_t token_start)
{
	MlCompletion c =
	{
		.token_start = token_start,
		.count       = 0,
		.ghost       = ML_MAX_CANDIDATES,
		.ghost_text   =
		{
			.data = "",
			.len  = 0
		},
		.append_space = 0
	};

	return c;
}

MlCompletion ml_completion_none (void)
{
	return completion_new (0);
}

/* Adds name when it starts with what has been typed. */
static void add_candidate (MlCompletion *c, MlSlice typed, const char *name)
{
	MlSlice item = ml_slice_cstr (name);

	if (
		   c->count < ML_MAX_CANDIDATES
		&& item.len >= typed.len
		&& ml_slice_equal (ml_slice_sub (item, 0, typed.len), typed)
	)
	{
		c->items [c->count] = item;
		c->count++;
	}
}

static int32_t command_needs_arg (MlSlice name)
{
	int32_t needs = 0;

	for (uint64_t i = 0; i < ml_count (commands) && !needs; i++)
	{
		needs = (
			   commands [i].needs_arg
			&& ml_slice_equal (name, ml_slice_cstr (commands [i].name))
		);
	}

	return needs;
}

static ArgKind command_arg_kind (MlSlice name)
{
	ArgKind kind = ARG_NONE;

	for (uint64_t i = 0; i < ml_count (commands); i++)
	{
		if (ml_slice_equal (name, ml_slice_cstr (commands [i].name)))
		{
			kind = commands [i].arg;
		}
	}

	return kind;
}

/* The first candidate that is longer than what was typed becomes the ghost. */
static void choose_ghost (MlCompletion *c, MlSlice typed)
{
	for (uint64_t i = 0; i < c->count && c->ghost == ML_MAX_CANDIDATES; i++)
	{
		if (typed.len > 0 && c->items [i].len > typed.len)
		{
			c->ghost      = i;
			c->ghost_text = ml_slice_sub (
				c->items [i],
				typed.len,
				c->items [i].len - typed.len
			);
		}
	}
}

MlCompletion ml_complete_command (MlSlice line)
{
	MlCompletion c     = completion_new (0);
	uint64_t     space = 0;

	while (space < line.len && line.data [space] != ' ')
	{
		space++;
	}

	if (space == line.len)
	{
		for (uint64_t i = 0; i < ml_count (commands); i++)
		{
			add_candidate (&c, line, commands [i].name);
		}
	}

	else
	{
		MlSlice typed = ml_slice_sub (line, space + 1, line.len - space - 1);
		ArgKind kind  = command_arg_kind (ml_slice_sub (line, 0, space));
		int32_t more  = 0;

		c.token_start = space + 1;

		/* only the first argument is completed */
		for (uint64_t i = 0; i < typed.len; i++)
		{
			more = more || typed.data [i] == ' ';
		}

		if (kind == ARG_DOMAIN && !more)
		{
			for (uint32_t d = 0; d < ML_DOMAIN_COUNT; d++)
			{
				add_candidate (&c, typed, ml_domain_name ((MlDomain)d));
			}
		}

		else if (kind == ARG_PGA_DIM && !more)
		{
			add_candidate (&c, typed, "2");
			add_candidate (&c, typed, "3");
		}

		choose_ghost (&c, typed);
	}

	if (space == line.len)
	{
		choose_ghost (&c, line);
		c.append_space = (
			   c.ghost < ML_MAX_CANDIDATES
			&& command_needs_arg (c.items [c.ghost])
		);
	}

	return c;
}
