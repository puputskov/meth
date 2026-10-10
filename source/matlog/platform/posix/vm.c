#define _DEFAULT_SOURCE 1

#include <matlog/platform/vm.h>

#include <sys/mman.h>

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

uint8_t *ml_vm_reserve (uint64_t size)
{
	void    *p = mmap (
		NULL,
		size,
		PROT_NONE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
		-1,
		0
	);
	uint8_t *r = NULL;

	if (p != MAP_FAILED)
	{
		r = (uint8_t *)p;
	}

	return r;
}

int32_t ml_vm_commit (uint8_t *p, uint64_t size)
{
	return mprotect (p, size, PROT_READ | PROT_WRITE) == 0;
}

void ml_vm_decommit (uint8_t *p, uint64_t size)
{
	/* the pages are dropped (they read as zero again), then closed */
	madvise (p, size, MADV_DONTNEED);
	mprotect (p, size, PROT_NONE);
}

void ml_vm_release (uint8_t *p, uint64_t size)
{
	munmap (p, size);
}
