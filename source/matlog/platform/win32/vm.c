#include <matlog/platform/vm.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

uint8_t *ml_vm_reserve (uint64_t size)
{
	return (uint8_t *)VirtualAlloc (NULL, size, MEM_RESERVE, PAGE_NOACCESS);
}

int32_t ml_vm_commit (uint8_t *p, uint64_t size)
{
	return VirtualAlloc (p, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

void ml_vm_decommit (uint8_t *p, uint64_t size)
{
	VirtualFree (p, size, MEM_DECOMMIT);
}

void ml_vm_release (uint8_t *p, uint64_t size)
{
	(void)size;
	VirtualFree (p, 0, MEM_RELEASE);
}
