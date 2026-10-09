#ifndef MATLOG_CORE_TYPES_H
#define MATLOG_CORE_TYPES_H

#include <stddef.h>
#include <stdint.h>

/*
 * Code uses the fixed-width types (int32_t, uint32_t, uint64_t, uint8_t, ...)
 * instead of int, unsigned int, size_t and so on. Sizes and counts are uint64_t.
 * matlog targets 64 bit platforms only, which this assert enforces; it is also
 * what makes passing a uint64_t to the C library's size_t parameters lossless.
 */
_Static_assert (sizeof (size_t) == sizeof (uint64_t), "matlog requires a 64 bit platform");

#endif /* MATLOG_CORE_TYPES_H */
