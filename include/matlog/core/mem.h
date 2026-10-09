#ifndef MATLOG_CORE_MEM_H
#define MATLOG_CORE_MEM_H

#include <stddef.h>

#include <matlog/core/slice.h>

/*
 * Arena (bump) allocator. Allocations live until ml_arena_reset or
 * ml_arena_free; individual frees do not exist. All returned memory is
 * aligned as requested.
 */

typedef struct MlArenaBlock MlArenaBlock;

typedef struct MlArena
{
	MlArenaBlock *head;      /* newest block, allocations come from here */
	uint64_t     block_size; /* default payload size of a new block */
} MlArena;

#define ML_ARENA_DEFAULT_BLOCK 65536

/* block_size 0 selects ML_ARENA_DEFAULT_BLOCK. No memory is allocated yet. */
void ml_arena_init (MlArena *a, uint64_t block_size);

/* align must be a power of two. Returns NULL on failure or size 0. */
void *ml_arena_alloc (MlArena *a, uint64_t size, uint64_t align);

/* Same as ml_arena_alloc but zero-filled. */
void *ml_arena_alloc_zero (MlArena *a, uint64_t size, uint64_t align);

/* Copies the slice and appends a NUL. */
char *ml_arena_strdup (MlArena *a, MlSlice s);

/* Invalidates all allocations but keeps the newest block for reuse. */
void ml_arena_reset (MlArena *a);

/* Releases everything. The arena can be ml_arena_init'ed again. */
void ml_arena_free (MlArena *a);

#define ML_ARENA_NEW(a, T) \
	((T *)ml_arena_alloc_zero ((a), sizeof (T), _Alignof (T)))

#define ML_ARENA_NEW_ARRAY(a, T, n) \
	((T *)ml_arena_alloc_zero ((a), sizeof (T) * (n), _Alignof (T)))

#endif /* MATLOG_CORE_MEM_H */
