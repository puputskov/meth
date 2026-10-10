#include <matlog/core/heap.h>

#include <matlog/core/mem.h>

#include <string.h>

#ifdef ML_HEAP_SYSTEM

#include <stdlib.h>

void *ml_heap_alloc (uint64_t size)
{
	return ml_choose (size > 0, malloc (size), NULL);
}

void *ml_heap_alloc_zero (uint64_t size)
{
	return ml_choose (size > 0, calloc (size, 1), NULL);
}

void *ml_heap_realloc (void *p, uint64_t size)
{
	return realloc (p, size);
}

void ml_heap_free (void *p)
{
	free (p);
}

uint64_t ml_heap_live_blocks (void)
{
	return 0;
}

uint64_t ml_heap_bad_frees (void)
{
	return 0;
}

#else

#define HEAP_RESERVE (1ULL << 38)
#define HEADER       16
#define MIN_CLASS    5
#define MAX_CLASS    40
#define MAGIC_LIVE   0x4D4C48454150ULL
#define MAGIC_FREE   0x4D4C46524545ULL

/* in front of every block */
typedef struct Header
{
	uint64_t magic;
	uint64_t cls;
} Header;

/* in the payload of a free block */
typedef struct FreeBlock
{
	struct FreeBlock *next;
} FreeBlock;

static MlArena   heap;
static int32_t   heap_ready;
static FreeBlock *free_list [MAX_CLASS + 1];
static uint64_t  live_blocks;
static uint64_t  bad_frees;

static void heap_start (void)
{
	if (!heap_ready)
	{
		ml_arena_init (&heap, HEAP_RESERVE);
		heap_ready = 1;
	}
}

/* The size class (block size is 1 << class) that holds size and the header. */
static uint64_t class_of (uint64_t size)
{
	uint64_t cls = MIN_CLASS;

	while (cls <= MAX_CLASS && (1ULL << cls) - HEADER < size)
	{
		cls++;
	}

	return cls;
}

static Header *header_of (void *p)
{
	return (Header *)((uint8_t *)p - HEADER);
}

void *ml_heap_alloc (uint64_t size)
{
	void     *p   = NULL;
	uint64_t cls  = class_of (size);
	Header   *h   = NULL;

	heap_start ();

	if (size > 0 && cls <= MAX_CLASS)
	{
		if (free_list [cls] != NULL)
		{
			h               = header_of (free_list [cls]);
			free_list [cls] = free_list [cls]->next;
		}

		else
		{
			h = (Header *)ml_arena_alloc (&heap, 1ULL << cls, HEADER);
		}
	}

	if (h != NULL)
	{
		h->magic = MAGIC_LIVE;
		h->cls   = cls;
		p        = (uint8_t *)h + HEADER;
		live_blocks++;
	}

	return p;
}

void *ml_heap_alloc_zero (uint64_t size)
{
	void *p = ml_heap_alloc (size);

	if (p != NULL)
	{
		memset (p, 0, size);
	}

	return p;
}

void ml_heap_free (void *p)
{
	if (p != NULL)
	{
		Header *h = header_of (p);

		if (h->magic == MAGIC_LIVE && h->cls <= MAX_CLASS)
		{
			FreeBlock *f = (FreeBlock *)p;

			h->magic           = MAGIC_FREE;
			f->next            = free_list [h->cls];
			free_list [h->cls] = f;
			live_blocks--;
		}

		else
		{
			bad_frees++;
		}
	}
}

void *ml_heap_realloc (void *p, uint64_t size)
{
	void *r = NULL;

	if (p == NULL)
	{
		r = ml_heap_alloc (size);
	}

	else if (size == 0)
	{
		ml_heap_free (p);
	}

	else
	{
		Header   *h   = header_of (p);
		uint64_t room = (1ULL << h->cls) - HEADER;

		if (size <= room)
		{
			r = p;
		}

		else
		{
			r = ml_heap_alloc (size);

			if (r != NULL)
			{
				memcpy (r, p, room);
				ml_heap_free (p);
			}
		}
	}

	return r;
}

uint64_t ml_heap_live_blocks (void)
{
	return live_blocks;
}

uint64_t ml_heap_bad_frees (void)
{
	return bad_frees;
}

#endif
