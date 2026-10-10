#include <matlog/parse/words.h>

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
	{ .spelling = "inf", .kind = SY, .id = ML_WORD_ID_INF }
};

#undef KW
#undef OP
#undef CN
#undef SY

MlWord ml_word_find (MlSlice name)
{
	MlWord found = { .kind = ML_WORD_NONE, .id = ML_WORD_ID_NONE };

	for (uint64_t i = 0; i < ml_count (rows) && found.kind == ML_WORD_NONE; i++)
	{
		if (ml_slice_equal (name, ml_slice_cstr (rows [i].spelling)))
		{
			found.kind = rows [i].kind;
			found.id   = rows [i].id;
		}
	}

	return found;
}

int32_t ml_word_is (MlSlice name, MlWordId id)
{
	return ml_word_find (name).id == id;
}

MlSlice ml_word_name (MlWordId id)
{
	MlSlice name = ml_slice_cstr ("");

	for (uint64_t i = 0; i < ml_count (rows) && name.len == 0; i++)
	{
		if (rows [i].id == id)
		{
			name = ml_slice_cstr (rows [i].spelling);
		}
	}

	return name;
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
