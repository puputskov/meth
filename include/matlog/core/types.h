#ifndef MATLOG_CORE_TYPES_H
#define MATLOG_CORE_TYPES_H

#include <stddef.h>
#include <stdint.h>

/*
 * Code uses the fixed-width types (int32_t, uint32_t, uint64_t, uint8_t, ...)
 * instead of int, unsigned int, size_t and so on. Sizes and counts are
 * uint64_t.
 * matlog targets 64 bit platforms only, which this assert enforces; it is also
 * what makes passing a uint64_t to the C library's size_t parameters lossless.
 */
_Static_assert (
	sizeof (size_t) == sizeof (uint64_t),
	"matlog requires a 64 bit platform"
);

/*
 * The conditional expression. Use it instead of the ?: operator; like ?: it
 * only evaluates
 * the argument that is chosen. Write the arguments one per line:
 *
 *	return ml_choose (
 *		n > 0,
 *		first (n),
 *		0
 *	);
 */
#define ml_choose(cond, a, b) ((cond) ? (a) : (b))

/* Number of elements of an array (not a pointer). */
#define ml_count(a) (sizeof (a) / sizeof ((a) [0]))

#endif /* MATLOG_CORE_TYPES_H */
