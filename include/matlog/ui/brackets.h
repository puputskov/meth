#ifndef MATLOG_UI_BRACKETS_H
#define MATLOG_UI_BRACKETS_H

#include <matlog/core/slice.h>
#include <matlog/core/types.h>

/*
 * The bracket next to the cursor and the one it pairs with, for the
 * highlight of matching parentheses. The bracket under the cursor counts
 * first, then the one right before it. ( ) [ ] and { } are paired by their
 * own kind: only brackets of the same kind are counted.
 */
typedef struct MlBracketMatch
{
	int32_t  found;   /* 1 when there is a bracket at or before the cursor */
	int32_t  matched; /* 1 when it has a partner */
	uint64_t at;      /* byte offset of the bracket */
	uint64_t partner; /* byte offset of the partner, valid when matched */
} MlBracketMatch;

MlBracketMatch ml_bracket_match (MlSlice text, uint64_t cursor);

#endif /* MATLOG_UI_BRACKETS_H */
