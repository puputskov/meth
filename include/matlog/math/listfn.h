#ifndef MATLOG_MATH_LISTFN_H
#define MATLOG_MATH_LISTFN_H

#include <matlog/math/value.h>

/*
 * The functions of the lists (plan section 4.18). A list is any number of
 * values, see value.h; a vector (a column or a row of exact numbers, at most
 * 64 of them) can be used where a list is read.
 *
 *	list (a, b, ...)      the list of its arguments; list () is empty
 *	tolist (v)            the list of the numbers of a vector
 *	vec (xs)              the column vector of a list of exact numbers
 *	len (xs)              the number of items (of a vector: of its numbers)
 *	item (xs, i)          item i, from 1; a negative i counts from the end
 *	append (xs, x)        the list with x after its last item
 *	concat (xs, ys)       the items of xs, then those of ys
 *	reverse (xs)          the items in the other order
 *	take (xs, n)          the first n items (all of them when there are fewer)
 *	drop (xs, n)          all but the first n items
 *
 * xs[i] is item (xs, i). Errors: ML_EVAL_TYPE for what is not a list or a
 * vector, ML_EVAL_NOT_INTEGER for a position that is not a whole number,
 * ML_EVAL_DOMAIN for a position outside the list, ML_EVAL_SIZE for a list
 * that is too long to be a vector, ML_EVAL_NOT_EXACT for a vec of
 * approximations.
 */

/* name, fewest and most arguments */
#define ML_LISTFN_LIST(X) \
	X (LIST,    "list",    0, 1000000) \
	X (TOLIST,  "tolist",  1, 1) \
	X (VEC,     "vec",     1, 1) \
	X (LEN,     "len",     1, 1) \
	X (ITEM,    "item",    2, 2) \
	X (APPEND,  "append",  2, 2) \
	X (CONCAT,  "concat",  2, 2) \
	X (REVERSE, "reverse", 1, 1) \
	X (TAKE,    "take",    2, 2) \
	X (DROP,    "drop",    2, 2)

typedef enum MlListFn
{
#define ML_LISTFN_ENUM(id, name, lo, hi) ML_LISTFN_##id,
	ML_LISTFN_LIST (ML_LISTFN_ENUM)
#undef ML_LISTFN_ENUM
	ML_LISTFN_TOTAL
} MlListFn;

MlValue ml_listfn_apply (
	MlArena       *arena,
	MlListFn      which,
	const MlValue *args,
	uint64_t      argc
);

/* Item i (from 1, or from the end when negative) of a list or a vector. */
MlValue ml_list_item (MlArena *arena, MlValue base, MlValue position);

#endif /* MATLOG_MATH_LISTFN_H */
