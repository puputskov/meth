#include <matlog/parse/words.h>

#include <matlog/core/heap.h>

#include <string.h>

/*
 * One row per spelling. listed is 1 for the spellings the completion offers
 * (the ASCII constants pi and e).
 */
typedef struct WordRow
{
	const char *spelling;
	MlWordKind kind;
	MlWordId   id;
	int32_t    listed;
} WordRow;

#define KW ML_WORD_KEYWORD
#define OP ML_WORD_OPERATOR
#define CN ML_WORD_CONSTANT
#define SY ML_WORD_SYMBOL
#define FN ML_WORD_FUNCTION
#define SB ML_WORD_SYMBOLIC
#define CM ML_WORD_COMMAND

static const WordRow rows [] = {
	{ .spelling = "include", .kind = KW, .id = ML_WORD_ID_INCLUDE },
	{ .spelling = "lambda", .kind = KW, .id = ML_WORD_ID_LAMBDA },
	{ .spelling = "let", .kind = KW, .id = ML_WORD_ID_LET },
	{ .spelling = "and", .kind = OP, .id = ML_WORD_ID_AND },
	{ .spelling = "or", .kind = OP, .id = ML_WORD_ID_OR },
	{ .spelling = "to", .kind = OP, .id = ML_WORD_ID_TO },
	{ .spelling = "pi", .kind = CN, .id = ML_WORD_ID_PI, .listed = 1 },
	{ .spelling = "\xCF\x80", .kind = CN, .id = ML_WORD_ID_PI },
	{ .spelling = "e", .kind = CN, .id = ML_WORD_ID_E, .listed = 1 },
	{ .spelling = "true", .kind = CN, .id = ML_WORD_ID_TRUE },
	{ .spelling = "false", .kind = CN, .id = ML_WORD_ID_FALSE },
	{ .spelling = "inf", .kind = SY, .id = ML_WORD_ID_INF },
	{ .spelling = "set", .kind = SY, .id = ML_WORD_ID_SET },
	{ .spelling = "sin", .kind = FN, .id = ML_WORD_ID_SIN },
	{ .spelling = "cos", .kind = FN, .id = ML_WORD_ID_COS },
	{ .spelling = "tan", .kind = FN, .id = ML_WORD_ID_TAN },
	{ .spelling = "asin", .kind = FN, .id = ML_WORD_ID_ASIN },
	{ .spelling = "acos", .kind = FN, .id = ML_WORD_ID_ACOS },
	{ .spelling = "atan", .kind = FN, .id = ML_WORD_ID_ATAN },
	{ .spelling = "ln", .kind = FN, .id = ML_WORD_ID_LN },
	{ .spelling = "log", .kind = FN, .id = ML_WORD_ID_LOG },
	{ .spelling = "exp", .kind = FN, .id = ML_WORD_ID_EXP },
	{ .spelling = "abs", .kind = FN, .id = ML_WORD_ID_ABS },
	{ .spelling = "sqrt", .kind = FN, .id = ML_WORD_ID_SQRT },
	{ .spelling = "det", .kind = FN, .id = ML_WORD_ID_DET },
	{ .spelling = "inv", .kind = FN, .id = ML_WORD_ID_INV },
	{ .spelling = "rref", .kind = FN, .id = ML_WORD_ID_RREF },
	{ .spelling = "rank", .kind = FN, .id = ML_WORD_ID_RANK },
	{ .spelling = "solve", .kind = FN, .id = ML_WORD_ID_SOLVE },
	{ .spelling = "steps", .kind = FN, .id = ML_WORD_ID_STEPS },
	{ .spelling = "simplify", .kind = SB, .id = ML_WORD_ID_SIMPLIFY },
	{ .spelling = "expand", .kind = SB, .id = ML_WORD_ID_EXPAND },
	{ .spelling = "subs", .kind = SB, .id = ML_WORD_ID_SUBS },
	{ .spelling = "diff", .kind = SB, .id = ML_WORD_ID_DIFF },
	{ .spelling = "integrate", .kind = SB, .id = ML_WORD_ID_INTEGRATE },
	{ .spelling = "limit", .kind = SB, .id = ML_WORD_ID_LIMIT },
	{ .spelling = "table", .kind = CM, .id = ML_WORD_ID_TABLE },
	{ .spelling = "trace", .kind = CM, .id = ML_WORD_ID_TRACE }
};

#undef KW
#undef OP
#undef CN
#undef SY
#undef FN
#undef SB
#undef CM

/*
 * The words are interned the first time they are asked for: row_of_name is
 * indexed by name number and holds the row plus one (0 for a name that is no
 * word), name_of_id the name of the ASCII spelling of each word. They are made
 * again when the names were released.
 */
static uint32_t *row_of_name;
static uint64_t row_of_name_len;
static MlName   name_of_id [ML_WORD_ID_COUNT];
static uint64_t built_for;

void ml_words_release (void)
{
	ml_heap_free (row_of_name);
	row_of_name     = NULL;
	row_of_name_len = 0;
	built_for       = 0;
}

static void build (void)
{
	MlName top = 0;

	ml_words_release ();
	memset (name_of_id, 0, sizeof name_of_id);

	for (uint64_t i = 0; i < ml_count (rows); i++)
	{
		MlName n = ml_name (ml_slice_cstr (rows [i].spelling));

		top = ml_choose (n > top, n, top);

		if (name_of_id [rows [i].id] == ML_NAME_NONE)
		{
			name_of_id [rows [i].id] = n;
		}
	}

	row_of_name = (uint32_t *)ml_heap_alloc_zero (
		((uint64_t)top + 1) * sizeof (uint32_t)
	);

	if (row_of_name != NULL)
	{
		row_of_name_len = (uint64_t)top + 1;

		for (uint64_t i = 0; i < ml_count (rows); i++)
		{
			row_of_name [ml_name (ml_slice_cstr (rows [i].spelling))]
				= (uint32_t)(i + 1);
		}
	}

	built_for = ml_names_generation ();
}

static void ready (void)
{
	if (built_for != ml_names_generation ())
	{
		build ();
	}
}

MlWord ml_word_of (MlName name)
{
	MlWord found = { .kind = ML_WORD_NONE, .id = ML_WORD_ID_NONE };

	ready ();

	if (name < row_of_name_len && row_of_name [name] != 0)
	{
		found.kind = rows [row_of_name [name] - 1].kind;
		found.id   = rows [row_of_name [name] - 1].id;
	}

	return found;
}

MlWord ml_word_find (MlSlice name)
{
	/* the words are interned first, or a first look up finds no name */
	ready ();
	return ml_word_of (ml_name_find (name));
}

int32_t ml_word_is (MlSlice name, MlWordId id)
{
	return ml_word_find (name).id == id;
}

MlName ml_word_name_id (MlWordId id)
{
	ready ();
	return ml_choose (id < ML_WORD_ID_COUNT, name_of_id [id], ML_NAME_NONE);
}

MlSlice ml_word_name (MlWordId id)
{
	return ml_name_text (ml_word_name_id (id));
}

uint64_t ml_word_kind_count (MlWordKind kind)
{
	uint64_t n = 0;

	for (uint64_t i = 0; i < ml_count (rows); i++)
	{
		n += (rows [i].kind == kind);
	}

	return n;
}

MlSlice ml_word_kind_at (MlWordKind kind, uint64_t n)
{
	MlSlice found = ml_slice_cstr ("");

	for (uint64_t i = 0; i < ml_count (rows) && found.len == 0; i++)
	{
		if (rows [i].kind == kind)
		{
			if (n == 0)
			{
				found = ml_slice_cstr (rows [i].spelling);
			}

			else
			{
				n--;
			}
		}
	}

	return found;
}

uint64_t ml_word_count (void)
{
	return ml_count (rows);
}

MlSlice ml_word_spelling (uint64_t index)
{
	return ml_choose (
		index < ml_count (rows),
		ml_slice_cstr (rows [index].spelling),
		ml_slice_cstr ("")
	);
}

MlWord ml_word_at (uint64_t index)
{
	MlWord w = { .kind = ML_WORD_NONE, .id = ML_WORD_ID_NONE };

	if (index < ml_count (rows))
	{
		w.kind = rows [index].kind;
		w.id   = rows [index].id;
	}

	return w;
}

int32_t ml_word_listed (uint64_t index)
{
	return ml_choose (index < ml_count (rows), rows [index].listed, 0);
}
