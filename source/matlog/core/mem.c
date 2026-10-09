#include <matlog/core/mem.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct MlArenaBlock
{
	MlArenaBlock *next;
	uint64_t     cap;
	uint64_t     used;
	/* forces data to be maximally aligned */
	union
	{
		int64_t ll;
		double  ld;
		void    *p;
	} align_;
};

static uint8_t *block_data (MlArenaBlock *b)
{
	return (uint8_t *)(b + 1);
}

static MlArenaBlock *block_new (uint64_t cap)
{
	MlArenaBlock *b = malloc (sizeof (MlArenaBlock) + cap);

	if (b)
	{
		b->next = NULL;
		b->cap  = cap;
		b->used = 0;
	}

	return b;
}

/* Bytes to skip so that (base + used + skip) is a multiple of align. */
static uint64_t pad_for (MlArenaBlock *b, uint64_t align)
{
	uintptr_t addr = (uintptr_t)(block_data (b) + b->used);

	return (uint64_t)((align - (addr & (align - 1))) & (align - 1));
}

static int32_t block_fits (MlArenaBlock *b, uint64_t size, uint64_t align)
{
	uint64_t pad  = pad_for (b, align);
	uint64_t room = b->cap - b->used;

	return size <= room && pad <= room - size;
}

static void *block_take (MlArenaBlock *b, uint64_t size, uint64_t align)
{
	uint64_t pad = pad_for (b, align);
	void     *p  = block_data (b) + b->used + pad;

	b->used += pad + size;
	return p;
}

/* Oversized requests get a block of their own, slotted behind the active one. */
static void *alloc_in_new_block (MlArena *a, uint64_t size, uint64_t align)
{
	void *p = NULL;

	if (size <= SIZE_MAX - align)
	{
		uint64_t     need = size + align;
		uint64_t     cap  = need > a->block_size ? need : a->block_size;
		MlArenaBlock *nb  = block_new (cap);

		if (nb)
		{
			if (need > a->block_size && a->head)
			{
				nb->next      = a->head->next;
				a->head->next = nb;
			}

			else
			{
				nb->next = a->head;
				a->head  = nb;
			}

			p = block_take (nb, size, align);
		}
	}

	return p;
}

void ml_arena_init (MlArena *a, uint64_t block_size)
{
	a->head       = NULL;
	a->block_size = block_size ? block_size : ML_ARENA_DEFAULT_BLOCK;
}

void *ml_arena_alloc (MlArena *a, uint64_t size, uint64_t align)
{
	void    *p    = NULL;
	int32_t valid = size != 0 && align != 0 && (align & (align - 1)) == 0;

	if (valid)
	{
		if (a->head && block_fits (a->head, size, align))
		{
			p = block_take (a->head, size, align);
		}

		else
		{
			p = alloc_in_new_block (a, size, align);
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

void ml_arena_reset (MlArena *a)
{
	if (a->head)
	{
		MlArenaBlock *rest = a->head->next;

		a->head->next = NULL;
		a->head->used = 0;
		while (rest)
		{
			MlArenaBlock *next = rest->next;

			free (rest);
			rest = next;
		}
	}
}

void ml_arena_free (MlArena *a)
{
	MlArenaBlock *b = a->head;

	while (b)
	{
		MlArenaBlock *next = b->next;

		free (b);
		b = next;
	}

	a->head = NULL;
}
