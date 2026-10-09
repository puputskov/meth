#ifndef MATLOG_CORE_UTF8_H
#define MATLOG_CORE_UTF8_H

#include <stddef.h>
#include <stdint.h>

#include <matlog/core/slice.h>

/* U+FFFD, produced when decoding malformed input */
#define ML_CP_REPLACEMENT 0xFFFDu

typedef struct MlDecoded
{
	uint32_t cp;
	uint64_t size; /* bytes consumed, 0 only for empty input */
} MlDecoded;

typedef struct MlEncoded
{
	char     bytes [4];
	uint64_t size; /* bytes used, 0 when the code point is not a valid scalar value */
} MlEncoded;

/*
 * Decodes the first code point of s. Malformed, overlong, surrogate and out of
 * range sequences yield ML_CP_REPLACEMENT with size 1.
 */
MlDecoded ml_utf8_decode (MlSlice s);

MlEncoded ml_utf8_encode (uint32_t cp);

/* 1 when the whole slice is well-formed UTF-8. */
int32_t ml_utf8_valid (MlSlice s);

/* Number of code points. Malformed bytes count as one each. */
uint64_t ml_utf8_count (MlSlice s);

/* Byte offset of the code point boundary after pos (clamped to the length). */
uint64_t ml_utf8_next (MlSlice s, uint64_t pos);

/* Byte offset of the code point boundary before pos (0 when pos is 0). */
uint64_t ml_utf8_prev (MlSlice s, uint64_t pos);

/* Terminal columns used by a code point: 0 (combining/zero width), 1 or 2 (wide). */
int32_t ml_cp_width (uint32_t cp);

/* Terminal columns used by a UTF-8 slice. */
uint64_t ml_utf8_width (MlSlice s);

/* 1 for characters that form words: ASCII letters, digits, '_' and letters of the Latin, Greek, Cyrillic and CJK scripts. */
int32_t ml_cp_is_word (uint32_t cp);

#endif /* MATLOG_CORE_UTF8_H */
