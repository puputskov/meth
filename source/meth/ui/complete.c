#include <meth/ui/complete.h>

#include <meth/docs/examples.h>
#include <meth/docs/snippets.h>
#include <meth/math/eval.h>
#include <meth/math/unit.h>
#include <meth/parse/words.h>
#include <meth/session/session.h>
#include <meth/texts/texts.h>
#include <meth/ui/theme.h>

typedef enum ArgKind
{
	ARG_NONE = 0,
	ARG_DOMAIN,
	ARG_PGA_DIM,
	ARG_ON_OFF,
	ARG_THEME,
	ARG_COLORS,
	ARG_EXPORT,
	ARG_HELP,
	ARG_EXAMPLE,
	ARG_SNIPPET,
	ARG_TUTORIAL,
	ARG_PATH /* files are not completed yet */
} ArgKind;

typedef struct Command
{
	const char *name;
	ArgKind    arg;
	int32_t    needs_arg; /* the command is useless without an argument */
} Command;

/* The commands of the editor, in the order they are offered. */
static const Command commands [] = {
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
	{ .name = "pga",   .arg = ARG_PGA_DIM, .needs_arg = 1 },
	{ .name = "steps", .arg = ARG_NONE,    .needs_arg = 0 },
	{ .name = "fold", .arg = ARG_NONE,     .needs_arg = 0 },
	{ .name = "unfold", .arg = ARG_NONE,   .needs_arg = 0 },
	{ .name = "foldall", .arg = ARG_NONE,  .needs_arg = 0 },
	{ .name = "unfoldall", .arg = ARG_NONE, .needs_arg = 0 },
	{ .name = "symbols", .arg = ARG_ON_OFF, .needs_arg = 0 },
	{ .name = "fix", .arg = ARG_NONE, .needs_arg = 1 },
	{ .name = "help", .arg = ARG_HELP, .needs_arg = 0 },
	{ .name = "export", .arg = ARG_EXPORT, .needs_arg = 1 },
	{ .name = "export!", .arg = ARG_EXPORT, .needs_arg = 1 },
	{ .name = "theme", .arg = ARG_THEME,   .needs_arg = 0 },
	{ .name = "colors", .arg = ARG_COLORS, .needs_arg = 0 },
	{ .name = "example", .arg = ARG_EXAMPLE, .needs_arg = 0 },
	{ .name = "examples", .arg = ARG_NONE, .needs_arg = 0 },
	{ .name = "snippet", .arg = ARG_SNIPPET, .needs_arg = 0 },
	{ .name = "snippets", .arg = ARG_NONE, .needs_arg = 0 },
	{ .name = "watch", .arg = ARG_ON_OFF,  .needs_arg = 0 },
	{ .name = "plot",  .arg = ARG_ON_OFF,  .needs_arg = 0 },
	{ .name = "tutorial", .arg = ARG_TUTORIAL, .needs_arg = 0 },
	{ .name = "set",   .arg = ARG_NONE,    .needs_arg = 0 },
	{ .name = "config", .arg = ARG_NONE,   .needs_arg = 0 },
	{ .name = "keys",  .arg = ARG_NONE,    .needs_arg = 0 },
	{ .name = "map",   .arg = ARG_NONE,    .needs_arg = 0 },
	{ .name = "unmap", .arg = ARG_NONE,    .needs_arg = 1 },
	{ .name = "undo",  .arg = ARG_NONE,    .needs_arg = 0 },
	{ .name = "redo",  .arg = ARG_NONE,    .needs_arg = 0 },
	{ .name = "delete", .arg = ARG_NONE,   .needs_arg = 0 },
	{ .name = "yank",  .arg = ARG_NONE,    .needs_arg = 0 },
	{ .name = "move",  .arg = ARG_NONE,    .needs_arg = 1 },
	{ .name = "copy",  .arg = ARG_NONE,    .needs_arg = 1 }
};

static MlCompletion completion_new (uint64_t token_start)
{
	MlCompletion c = {
		.token_start = token_start,
		.count       = 0,
		.ghost       = ML_MAX_CANDIDATES,
		.ghost_text   = {
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

/* Adds the names of the theme files (name.theme per line) as candidates. */
static void add_theme_files (MlCompletion *c, MlSlice typed, MlSlice files)
{
	MlSlice suffix = ml_slice_cstr (".theme");
	uint64_t pos   = 0;

	while (pos < files.len)
	{
		uint64_t end = pos;

		while (end < files.len && files.data [end] != '\n')
		{
			end++;
		}

		if (
			   end - pos > suffix.len
			&& ml_slice_equal (
				   ml_slice_sub (files, end - suffix.len, suffix.len),
				   suffix
			   )
		)
		{
			MlSlice name = ml_slice_sub (files, pos, end - pos - suffix.len);
			int32_t known = 0;

			for (uint64_t i = 0; i < c->count; i++)
			{
				known = known || ml_slice_equal (c->items [i], name);
			}

			if (
				   !known
				&& c->count < ML_MAX_CANDIDATES
				&& name.len >= typed.len
				&& ml_slice_equal (ml_slice_sub (name, 0, typed.len), typed)
			)
			{
				c->items [c->count] = name;
				c->count++;
			}
		}

		pos = end + 1;
	}
}

MlCompletion ml_complete_command (MlSlice line)
{
	return ml_complete_command_with (line, ml_slice ("", 0));
}

uint64_t ml_command_count (void)
{
	return ml_count (commands);
}

MlSlice ml_command_name (uint64_t index)
{
	return ml_choose (
		index < ml_count (commands),
		ml_slice_cstr (commands [index].name),
		ml_slice ("", 0)
	);
}

MlSlice ml_command_summary (MlSlice line)
{
	uint64_t cut   = 0;
	MlSlice  found = ml_slice ("", 0);

	while (cut < line.len && line.data [cut] != ' ')
	{
		cut++;
	}

	for (uint64_t i = 0; i < ml_count (commands) && found.len == 0; i++)
	{
		if (ml_slice_equal (ml_slice_sub (line, 0, cut), ml_command_name (i)))
		{
			MlStr key;

			ml_str_init (&key);

			if (
				ml_str_appendf (&key, "commands.%s", commands [i].name) == ML_OK
			)
			{
				const char *t = ml_text_find (ml_str_cstr (key));

				found = ml_slice_cstr (t);
			}

			ml_str_free (&key);
		}
	}

	return found;
}

MlCompletion ml_complete_command_with (MlSlice line, MlSlice theme_files)
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

		else if (kind == ARG_ON_OFF && !more)
		{
			add_candidate (&c, typed, "on");
			add_candidate (&c, typed, "off");
		}

		else if (kind == ARG_THEME && !more)
		{
			for (uint64_t i = 0; i < ml_theme_builtin_count (); i++)
			{
				add_candidate (&c, typed, ml_theme_builtin_name (i));
			}

			add_candidate (&c, typed, "reload");
			add_theme_files (&c, typed, theme_files);
		}

		else if (kind == ARG_EXAMPLE && !more)
		{
			for (uint64_t i = 0; i < ml_example_count (); i++)
			{
				add_candidate (&c, typed, ml_example_sources [i].name);
			}
		}

		else if (kind == ARG_TUTORIAL && !more)
		{
			add_candidate (&c, typed, "syntax");
			add_candidate (&c, typed, "show");
			add_candidate (&c, typed, "next");
			add_candidate (&c, typed, "back");
			add_candidate (&c, typed, "off");
			add_candidate (&c, typed, "reset");
		}

		else if (kind == ARG_SNIPPET && !more)
		{
			for (uint64_t i = 0; i < ml_snippet_count (); i++)
			{
				add_candidate (&c, typed, ml_snippet_sources [i].name);
			}
		}

		else if (kind == ARG_EXPORT && !more)
		{
			add_candidate (&c, typed, "latex");
			add_candidate (&c, typed, "mathml");
			add_candidate (&c, typed, "markdown");
		}

		else if (kind == ARG_COLORS && !more)
		{
			for (uint32_t i = 0; i < ML_COLORS_COUNT; i++)
			{
				add_candidate (
					&c,
					typed,
					ml_color_level_name ((MlColorLevel)i)
				);
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

/* ---- math lines --------------------------------------------------------- */

/* Adds item (a slice that stays valid) unless it is there or does not fit. */
static void add_item (MlCompletion *c, MlSlice typed, MlSlice item)
{
	int32_t known = 0;

	for (uint64_t i = 0; i < c->count; i++)
	{
		known = known || ml_slice_equal (c->items [i], item);
	}

	if (
		   !known
		&& c->count < ML_MAX_CANDIDATES
		&& item.len > typed.len
		&& ml_slice_equal (ml_slice_sub (item, 0, typed.len), typed)
	)
	{
		c->items [c->count] = item;
		c->count++;
	}
}

/* Does a byte sequence ending in a continuation byte belong to a name? */
static int32_t is_name_lead (MlSlice text, uint64_t lead)
{
	uint8_t b0 = (uint8_t)text.data [lead];

	return (
		   b0 == 0xCE
		|| b0 == 0xCF
		|| (
			   b0 == 0xC2
			&& lead + 1 < text.len
			&& (uint8_t)text.data [lead + 1] == 0xB5
		)
	);
}

/* Where the name at the end of text starts (text.len when there is none). */
static uint64_t name_start (MlSlice text)
{
	uint64_t start = text.len;
	int32_t  more  = 1;

	while (start > 0 && more)
	{
		uint8_t b = (uint8_t)text.data [start - 1];

		if (
			   (b >= 'a' && b <= 'z')
			|| (b >= 'A' && b <= 'Z')
			|| (b >= '0' && b <= '9')
			|| b == '_'
		)
		{
			start--;
		}

		else if (b >= 0x80)
		{
			uint64_t lead = start - 1;

			while (lead > 0 && ((uint8_t)text.data [lead] & 0xC0) == 0x80)
			{
				lead--;
			}

			more = is_name_lead (text, lead);

			if (more)
			{
				start = lead;
			}
		}

		else
		{
			more = 0;
		}
	}

	while (
		   start < text.len
		&& text.data [start] >= '0'
		&& text.data [start] <= '9'
	)
	{
		start++;
	}

	return start;
}

MlCompletion ml_complete_math (
	const MlEnv *env,
	uint64_t    snapshot,
	MlSlice     text,
	int32_t     units
)
{
	uint64_t     start = name_start (text);
	MlSlice      typed = ml_slice_sub (text, start, text.len - start);
	MlCompletion c     = completion_new (start);
	uint64_t     bound = ml_choose (
		snapshot < env->bindings.len,
		snapshot,
		env->bindings.len
	);

	if (typed.len > 0)
	{
		for (uint64_t i = bound; i > 0; i--)
		{
			MlSlice name = env->bindings.data [i - 1].name;

			/* the helpers of the library start with an underscore */
			if (name.len > 0 && name.data [0] != '_')
			{
				add_item (&c, typed, name);
			}
		}

		for (uint64_t i = 0; i < ml_function_name_count (); i++)
		{
			add_item (&c, typed, ml_function_name_at (i));
		}

		for (uint64_t i = 0; i < ml_word_count (); i++)
		{
			if (ml_word_listed (i))
			{
				add_item (&c, typed, ml_word_spelling (i));
			}
		}

		for (uint64_t i = 0; i < ml_unit_name_count () && units; i++)
		{
			add_item (&c, typed, ml_unit_name_at (i));
		}

		choose_ghost (&c, typed);
	}

	return c;
}
