#include <meth/core/mem.h>

#include <meth/platform/vm.h>

#include <string.h>

#define MIN_RESERVE (1ULL << 20)

static uint64_t round_up (uint64_t n, uint64_t unit)
{
	return (n + unit - 1) / unit * unit;
}

/* Reserves the address space on first use, a smaller range if it must. */
static void reserve_space (MlArena *a)
{
	uint64_t size = round_up (
		ml_choose (a->want > 0, a->want, ML_ARENA_DEFAULT_RESERVE),
		ML_VM_GRANULE
	);

	while (a->base == NULL && size >= MIN_RESERVE)
	{
		a->base = ml_vm_reserve (size);

		if (a->base != NULL)
		{
			a->reserved = size;
		}

		else
		{
			size = round_up (size / 2, ML_VM_GRANULE);
		}
	}
}

/* Commits pages so that `end` bytes from base can be used. */
static int32_t commit_to (MlArena *a, uint64_t end)
{
	int32_t ok = end <= a->committed;

	if (!ok && end <= a->reserved)
	{
		uint64_t target = round_up (end, ML_ARENA_COMMIT_CHUNK);

		target = ml_choose (target < a->reserved, target, a->reserved);
		ok     = ml_vm_commit (a->base + a->committed, target - a->committed);

		if (ok)
		{
			a->committed = target;
		}
	}

	return ok;
}

void ml_arena_init (MlArena *a, uint64_t reserve)
{
	a->base      = NULL;
	a->want      = reserve;
	a->reserved  = 0;
	a->committed = 0;
	a->used      = 0;
}

MlArena *ml_arena_app (void)
{
	static MlArena app;
	static int32_t made = 0;

	if (!made)
	{
		ml_arena_init (&app, ML_ARENA_APP_RESERVE);
		made = 1;
	}

	return &app;
}

/* The general case: reserves, commits more pages, or fails. */
static void *alloc_slow (MlArena *a, uint64_t size, uint64_t align)
{
	void *p = NULL;

	if (a->base == NULL)
	{
		reserve_space (a);
	}

	if (a->base != NULL)
	{
		/* base is page aligned, so aligning the offset aligns the address
		   for every align up to the page size */
		uint64_t at  = (a->used + align - 1) & ~(align - 1);
		int32_t  fit = at >= a->used && at <= a->reserved
			&& size <= a->reserved - at;

		if (fit && commit_to (a, at + size))
		{
			p       = a->base + at;
			a->used = at + size;
		}
	}

	return p;
}

void *ml_arena_alloc (MlArena *a, uint64_t size, uint64_t align)
{
	void    *p    = NULL;
	int32_t valid = size != 0 && align != 0 && (align & (align - 1)) == 0;

	if (valid)
	{
		/* the usual case: the pages are there, so it is a bump */
		uint64_t at  = (a->used + align - 1) & ~(align - 1);
		uint64_t end = at + size;

		if (a->base != NULL && end >= at && end <= a->committed)
		{
			p       = a->base + at;
			a->used = end;
		}

		else
		{
			p = alloc_slow (a, size, align);
		}
	}

	return p;
}

void *ml_arena_alloc_zero (MlArena *a, uint64_t size, uint64_t align)
{
	void *p = ml_arena_alloc (a, size, align);

	if (p)
	{
		memset (p, 0, size);
	}

	return p;
}

char *ml_arena_strdup (MlArena *a, MlSlice s)
{
	char *p = ml_arena_alloc (a, s.len + 1, 1);

	if (p)
	{
		memcpy (p, s.data, s.len);
		p [s.len] = '\0';
	}

	return p;
}

MlArenaMark ml_arena_mark (const MlArena *a)
{
	MlArenaMark m = { .used = a->used };

	return m;
}

void ml_arena_rewind (MlArena *a, MlArenaMark m)
{
	if (m.used <= a->used)
	{
		a->used = m.used;
	}

	/* pages are given back rarely: only when far more is committed than used */
	if (a->committed > a->used + ML_ARENA_TRIM_SLACK)
	{
		uint64_t keep = round_up (
			a->used + ML_ARENA_TRIM_SLACK / 4,
			ML_ARENA_COMMIT_CHUNK
		);

		if (keep < a->committed)
		{
			ml_vm_decommit (a->base + keep, a->committed - keep);
			a->committed = keep;
		}
	}
}

void ml_arena_reset (MlArena *a)
{
	MlArenaMark start = { .used = 0 };

	ml_arena_rewind (a, start);
}

void ml_arena_free (MlArena *a)
{
	if (a->base != NULL)
	{
		ml_vm_release (a->base, a->reserved);
	}

	ml_arena_init (a, a->want);
}
