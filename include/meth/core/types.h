#ifndef METH_CORE_TYPES_H
#define METH_CORE_TYPES_H

#include <stddef.h>
#include <stdint.h>

/*
 * Code uses the fixed-width types (int32_t, uint32_t, uint64_t, uint8_t, ...)
 * instead of int, unsigned int, size_t and so on. Sizes and counts are
 * uint64_t.
 * meth targets 64 bit platforms only, which this assert enforces; it is also
 * what makes passing a uint64_t to the C library's size_t parameters lossless.
 * The one exception is the WebAssembly build (ML_WASM32, a 32 bit size_t, see
 * platform/wasm/): sizes stay uint64_t in the code and the memory is far
 * smaller than 4 GB, so a size always fits when it reaches the C library.
 */
#ifdef ML_WASM32
_Static_assert (
	sizeof (size_t) == sizeof (uint32_t),
	"ML_WASM32 is for a 32 bit target"
);
#else
_Static_assert (
	sizeof (size_t) == sizeof (uint64_t),
	"meth requires a 64 bit platform"
);
#endif

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

#endif /* METH_CORE_TYPES_H */
