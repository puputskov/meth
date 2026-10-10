#ifndef MATLOG_PLATFORM_VM_H
#define MATLOG_PLATFORM_VM_H

#include <matlog/core/types.h>

/*
 * Virtual memory: address space is reserved first and pages are committed
 * (made usable) as they are needed, so a region never moves and a pointer into
 * it stays valid. The arena (core/mem.h) is the only user.
 *
 * Sizes and addresses passed to commit and decommit must be multiples of
 * ML_VM_GRANULE, which is a multiple of every page size we run on.
 */

#define ML_VM_GRANULE 65536

/* Reserves size bytes of address space, NULL when there is no room. */
uint8_t *ml_vm_reserve (uint64_t size);

/* Makes the range readable and writable (zero filled the first time). */
int32_t ml_vm_commit (uint8_t *p, uint64_t size);

/* Gives the pages of the range back; the address space stays reserved. */
void ml_vm_decommit (uint8_t *p, uint64_t size);

/* Releases the whole reservation made by ml_vm_reserve (same size). */
void ml_vm_release (uint8_t *p, uint64_t size);

#endif /* MATLOG_PLATFORM_VM_H */
