#ifndef MATLOG_CORE_MEM_H
#define MATLOG_CORE_MEM_H

#include <stddef.h>

#include <matlog/core/slice.h>

/*
 * Arena (bump) allocator on reserved address space. The arena reserves a large
 * range when it is first used and commits pages as allocations need them, so
 * memory never moves (a pointer stays valid until the arena is rewound, reset
 * or freed) and there is no list of blocks. Individual frees do not exist.
 * Committed pages are given back rarely: when a rewind leaves much more than
 * the arena uses (ML_ARENA_TRIM_SLACK).
 *
 * Two kinds of arena are used:
 *  - the application arena (ml_arena_app): scratch space for one operation.
 *    Take a mark, allocate, rewind to the mark. Marks nest like calls do.
 *  - an arena that belongs to a long lived object (the environment): made
 *    with ml_arena_init, released with ml_arena_free.
 */

typedef struct MlArena
{
	uint8_t  *base;      /* start of the reservation, NULL until first use */
	uint64_t want;       /* bytes to reserve, 0 selects the default */
	uint64_t reserved;   /* bytes of address space reserved */
	uint64_t committed;  /* bytes from base that can be used */
	uint64_t used;       /* bytes handed out (padding included) */
} MlArena;

/* A position in an arena; ml_arena_rewind frees everything after it. */
typedef struct MlArenaMark
{
	uint64_t used;
} MlArenaMark;

#define ML_ARENA_DEFAULT_RESERVE (1ULL << 30)  /* 1 GB of address space */
#define ML_ARENA_APP_RESERVE     (1ULL << 34)  /* 16 GB for the app arena */
#define ML_ARENA_COMMIT_CHUNK    (1ULL << 20)  /* pages are committed by 1 MB */
#define ML_ARENA_TRIM_SLACK      (1ULL << 26)  /* decommit above 64 MB slack */

/*
 * reserve is the address space to reserve (0 selects the default); if that
 * much is not available a smaller range is taken. No memory is touched yet.
 */
void ml_arena_init (MlArena *a, uint64_t reserve);

/* The application arena: one for the whole program, made on first use. */
MlArena *ml_arena_app (void);

/* align must be a power of two. Returns NULL on failure or size 0. */
void *ml_arena_alloc (MlArena *a, uint64_t size, uint64_t align);

/* Same as ml_arena_alloc but zero-filled. */
void *ml_arena_alloc_zero (MlArena *a, uint64_t size, uint64_t align);

/* Copies the slice and appends a NUL. */
char *ml_arena_strdup (MlArena *a, MlSlice s);

/* The position now, to rewind to later. */
MlArenaMark ml_arena_mark (const MlArena *a);

/* Invalidates every allocation made after the mark. */
void ml_arena_rewind (MlArena *a, MlArenaMark m);

/* Invalidates all allocations; the reservation is kept. */
void ml_arena_reset (MlArena *a);

/* Releases everything. The arena can be ml_arena_init'ed again. */
void ml_arena_free (MlArena *a);

#define ML_ARENA_NEW(a, T) \
	((T *)ml_arena_alloc_zero ((a), sizeof (T), _Alignof (T)))

#define ML_ARENA_NEW_ARRAY(a, T, n) \
	((T *)ml_arena_alloc_zero ((a), sizeof (T) * (n), _Alignof (T)))

#endif /* MATLOG_CORE_MEM_H */
