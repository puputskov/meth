#include <meth/platform/vm.h>

/*
 * Memory for the arenas in a WebAssembly module. There is one linear memory
 * that only grows, so a reservation is a part of it taken with memory.grow
 * (the browser commits the pages when they are first written), commit and
 * decommit have nothing to do, and a released range cannot be given back: it
 * is kept in a table and handed out again for a reservation of the same size.
 */

#define PAGE     65536ULL
#define MAX_FREE 64

typedef struct Range
{
	uint8_t  *base;
	uint64_t size;
} Range;

static Range freed [MAX_FREE];

uint8_t *ml_vm_reserve (uint64_t size)
{
	uint8_t *r = NULL;

	for (uint64_t i = 0; r == NULL && i < MAX_FREE; i++)
	{
		if (freed [i].base != NULL && freed [i].size == size)
		{
			r              = freed [i].base;
			freed [i].base = NULL;
		}
	}

	if (r == NULL && size % PAGE == 0 && size / PAGE < 0x10000)
	{
		uint32_t first = (uint32_t)__builtin_wasm_memory_grow (
			0,
			(uint32_t)(size / PAGE)
		);

		if (first != (uint32_t)-1)
		{
			r = (uint8_t *)((uint64_t)first * PAGE);
		}
	}

	return r;
}

int32_t ml_vm_commit (uint8_t *p, uint64_t size)
{
	(void)p;
	(void)size;
	return 1;
}

void ml_vm_decommit (uint8_t *p, uint64_t size)
{
	(void)p;
	(void)size;
}

void ml_vm_release (uint8_t *p, uint64_t size)
{
	int32_t kept = 0;

	for (uint64_t i = 0; !kept && i < MAX_FREE; i++)
	{
		if (freed [i].base == NULL)
		{
			freed [i].base = p;
			freed [i].size = size;
			kept           = 1;
		}
	}
}
