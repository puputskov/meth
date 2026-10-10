#include <meth/core/names.h>

#include <meth/core/heap.h>
#include <meth/core/mem.h>
#include <meth/core/vec.h>

#include <string.h>

ML_VEC_DEFINE (NameTextVec, MlSlice)

/* The texts are in an arena of their own and never move. */
static MlArena      texts;
static NameTextVec  by_number;  /* by_number [n - 1] is the text of name n */
static MlName       *slots;     /* open addressing: a name number or 0 */
static uint64_t     slot_count; /* a power of two, or 0 before the first name */
static uint64_t     generation = 1;

static uint64_t hash_of (MlSlice s)
{
	uint64_t h = 14695981039346656037ULL;

	for (uint64_t i = 0; i < s.len; i++)
	{
		h ^= (uint8_t)s.data [i];
		h *= 1099511628211ULL;
	}

	return h;
}

/* The slot that holds the text, or the empty slot where it would go. */
static uint64_t slot_for (MlSlice text)
{
	uint64_t mask = slot_count - 1;
	uint64_t at   = hash_of (text) & mask;

	while (
		   slots [at] != ML_NAME_NONE
		&& !ml_slice_equal (by_number.data [slots [at] - 1], text)
	)
	{
		at = (at + 1) & mask;
	}

	return at;
}

/* Doubles the table and puts every name in again. */
static MlResult grow (void)
{
	uint64_t more = ml_choose (slot_count > 0, slot_count * 2, 256);
	MlName   *old = slots;
	uint64_t was  = slot_count;
	MlResult r    = ML_OK;

	slots = (MlName *)ml_heap_alloc_zero (more * sizeof (MlName));

	if (slots == NULL)
	{
		slots = old;
		r     = ML_ERR_NOMEM;
	}

	else
	{
		slot_count = more;

		for (uint64_t i = 0; i < was; i++)
		{
			if (old [i] != ML_NAME_NONE)
			{
				slots [slot_for (by_number.data [old [i] - 1])] = old [i];
			}
		}

		ml_heap_free (old);
	}

	return r;
}

MlName ml_name_find (MlSlice text)
{
	MlName found = ML_NAME_NONE;

	if (slot_count > 0)
	{
		found = slots [slot_for (text)];
	}

	return found;
}

MlName ml_name (MlSlice text)
{
	MlName n = ml_name_find (text);

	if (n == ML_NAME_NONE && by_number.len + 1 < UINT32_MAX)
	{
		MlResult r = ML_OK;

		/* keep the table at most half full */
		if (slot_count == 0 || (by_number.len + 1) * 2 > slot_count)
		{
			r = grow ();
		}

		if (r == ML_OK)
		{
			char *copy = ml_arena_alloc (&texts, text.len + 1, 1);

			if (copy != NULL)
			{
				memcpy (copy, text.data, text.len);
				copy [text.len] = '\0';
				r = NameTextVec_push (&by_number, ml_slice (copy, text.len));

				if (r == ML_OK)
				{
					n = (MlName)by_number.len;
					slots [slot_for (text)] = n;
				}
			}
		}
	}

	return n;
}

MlSlice ml_name_text (MlName n)
{
	return ml_choose (
		n != ML_NAME_NONE && n <= by_number.len,
		by_number.data [n - 1],
		ml_slice ("", 0)
	);
}

uint32_t ml_name_count (void)
{
	return (uint32_t)by_number.len;
}

uint64_t ml_names_generation (void)
{
	return generation;
}

void ml_names_release (void)
{
	generation++;
	NameTextVec_free (&by_number);
	ml_heap_free (slots);
	slots      = NULL;
	slot_count = 0;
	ml_arena_free (&texts);
}
