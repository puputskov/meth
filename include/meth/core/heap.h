#ifndef METH_CORE_HEAP_H
#define METH_CORE_HEAP_H

#include <meth/core/types.h>

/*
 * The heap of the program: memory for the things that grow and shrink in no
 * particular order (vectors, strings, the lines of a session). It lives in
 * reserved address space (core/mem.h, platform/vm.h) and nothing in the
 * program calls malloc, realloc or free.
 *
 * Blocks come in sizes that are powers of two (16 bytes of header included). A
 * freed block goes on a list of its size and is handed out again, so a vector
 * that outgrew its block gives the old one back and memory stays bounded in a
 * long session. Blocks are 16 byte aligned. Pages are not given back to the
 * system. One thread only.
 *
 * Compile with ML_HEAP_SYSTEM to use the C library instead, so that the
 * sanitizers and valgrind see every block (the tests do this in that build).
 */

/* NULL when size is 0 or memory is exhausted. */
void *ml_heap_alloc (uint64_t size);

/* Zero filled. */
void *ml_heap_alloc_zero (uint64_t size);

/*
 * Grows or shrinks a block, keeping its content up to the smaller size. p may
 * be NULL. On failure NULL is returned and p stays valid.
 */
void *ml_heap_realloc (void *p, uint64_t size);

/* p may be NULL. */
void ml_heap_free (void *p);

/* Blocks handed out and not yet freed, to find leaks in a test. */
uint64_t ml_heap_live_blocks (void);

/* Frees of a pointer that was not live (double free, foreign pointer). */
uint64_t ml_heap_bad_frees (void);

#endif /* METH_CORE_HEAP_H */
