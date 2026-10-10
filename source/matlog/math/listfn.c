#include <matlog/math/listfn.h>

static MlValue error_of (MlErrorKind kind)
{
	return ml_value_error (kind, 0, 0);
}

/* The items of a list, or of a vector as exact numbers. */
typedef struct Seq
{
	MlErrorKind   error;
	uint64_t      n;
	const MlValue *items;
} Seq;

static Seq seq_of (MlArena *arena, MlValue v)
{
	Seq s = {
		.error = ML_EVAL_NONE,
		.n     = 0,
		.items = NULL
	};

	if (v.kind == ML_VALUE_LIST)
	{
		s.n     = v.list->count;
		s.items = v.list->items;
	}

	else if (
		   v.kind == ML_VALUE_MATRIX
		&& (v.matrix.rows == 1 || v.matrix.cols == 1)
	)
	{
		MlValue *own;

		s.n = v.matrix.rows * v.matrix.cols;
		own = ML_ARENA_NEW_ARRAY (arena, MlValue, s.n + 1);

		if (own == NULL)
		{
			s.error = ML_EVAL_NO_MEMORY;
		}

		for (uint64_t i = 0; i < s.n && own != NULL; i++)
		{
			own [i] = ml_value_exact (v.matrix.cells [i]);
		}

		s.items = own;
	}

	else
	{
		s.error = ML_EVAL_TYPE;
	}

	return s;
}

/* A whole number of the list functions, from -2^62 to 2^62. */
typedef struct Whole
{
	MlErrorKind error;
	int64_t     value;
} Whole;

static Whole whole_of (MlValue v)
{
	MlI64Result k = ml_value_to_i64 (v);
	Whole       w = {
		.error = ML_EVAL_NONE,
		.value = k.value
	};

	if (v.kind != ML_VALUE_EXACT)
	{
		w.error = ML_EVAL_TYPE;
	}

	else if (!k.ok)
	{
		w.error = ML_EVAL_NOT_INTEGER;
	}

	return w;
}

MlValue ml_list_item (MlArena *arena, MlValue base, MlValue position)
{
	Seq   s = seq_of (arena, base);
	Whole w = whole_of (position);
	MlValue r = error_of (ML_EVAL_TYPE);

	if (s.error != ML_EVAL_NONE)
	{
		r = error_of (s.error);
	}

	else if (w.error != ML_EVAL_NONE)
	{
		r = error_of (w.error);
	}

	else
	{
		int64_t at = ml_choose (
			w.value < 0,
			(int64_t)s.n + w.value,
			w.value - 1
		);

		r = ml_choose (
			at >= 0 && at < (int64_t)s.n,
			s.items [at],
			error_of (ML_EVAL_DOMAIN)
		);
	}

	return r;
}

/* The list of the items from first on, count of them, in the order given. */
static MlValue slice_list (
	MlArena       *arena,
	const MlValue *items,
	uint64_t      first,
	uint64_t      count,
	int32_t       backwards
)
{
	MlValue *out = ML_ARENA_NEW_ARRAY (arena, MlValue, count + 1);
	MlValue r    = error_of (ML_EVAL_NO_MEMORY);

	if (out != NULL)
	{
		for (uint64_t i = 0; i < count; i++)
		{
			uint64_t from = ml_choose (
				backwards,
				first + count - 1 - i,
				first + i
			);

			out [i] = items [from];
		}

		r = ml_value_list (arena, out, count);
	}

	return r;
}

static MlValue to_vector (MlArena *arena, Seq s)
{
	MlMat   m = ml_mat_zero (arena, s.n, 1);
	MlValue r = error_of (ML_EVAL_SIZE);

	for (uint64_t i = 0; i < s.n && m.status == ML_OK; i++)
	{
		if (s.items [i].kind != ML_VALUE_EXACT)
		{
			m = ml_mat_error (ML_ERR_INVALID);
		}

		else
		{
			ml_mat_set (&m, i, 0, s.items [i].exact);
		}
	}

	if (s.n > ML_MAT_MAX_DIM || s.n == 0)
	{
		r = error_of (ML_EVAL_SIZE);
	}

	else if (m.status == ML_ERR_INVALID)
	{
		r = error_of (ML_EVAL_NOT_EXACT);
	}

	else
	{
		r = ml_value_matrix (m);
	}

	return r;
}

/* take (xs, n) and drop (xs, n) */
static MlValue take_drop (
	MlArena  *arena,
	MlListFn which,
	MlValue  xs,
	MlValue  count
)
{
	Seq   s = seq_of (arena, xs);
	Whole w = whole_of (count);
	MlValue r = error_of (ML_EVAL_TYPE);

	if (s.error != ML_EVAL_NONE)
	{
		r = error_of (s.error);
	}

	else if (w.error != ML_EVAL_NONE)
	{
		r = error_of (w.error);
	}

	else
	{
		uint64_t k = ml_choose (
			w.value < 0,
			0,
			ml_choose ((uint64_t)w.value > s.n, s.n, (uint64_t)w.value)
		);

		r = ml_choose (
			which == ML_LISTFN_TAKE,
			slice_list (arena, s.items, 0, k, 0),
			slice_list (arena, s.items, k, s.n - k, 0)
		);
	}

	return r;
}

/* append (xs, x) and concat (xs, ys) */
static MlValue join (MlArena *arena, MlListFn which, MlValue a, MlValue b)
{
	Seq     s = seq_of (arena, a);
	Seq     t = seq_of (arena, b);
	MlValue r = error_of (ML_EVAL_TYPE);

	if (s.error != ML_EVAL_NONE)
	{
		r = error_of (s.error);
	}

	else if (which == ML_LISTFN_CONCAT && t.error != ML_EVAL_NONE)
	{
		r = error_of (t.error);
	}

	else
	{
		uint64_t extra = ml_choose (which == ML_LISTFN_APPEND, 1, t.n);
		MlValue  *out  = ML_ARENA_NEW_ARRAY (arena, MlValue, s.n + extra + 1);

		if (out == NULL)
		{
			r = error_of (ML_EVAL_NO_MEMORY);
		}

		else
		{
			for (uint64_t i = 0; i < s.n; i++)
			{
				out [i] = s.items [i];
			}

			for (uint64_t i = 0; i < extra; i++)
			{
				out [s.n + i] = ml_choose (
					which == ML_LISTFN_APPEND,
					b,
					t.items [i]
				);
			}

			r = ml_value_list (arena, out, s.n + extra);
		}
	}

	return r;
}

MlValue ml_listfn_apply (
	MlArena       *arena,
	MlListFn      which,
	const MlValue *args,
	uint64_t      argc
)
{
	MlValue r = error_of (ML_EVAL_TYPE);
	Seq     s;

	if (which == ML_LISTFN_LIST)
	{
		r = ml_value_list (arena, args, argc);
	}

	else if (which == ML_LISTFN_ITEM)
	{
		r = ml_list_item (arena, args [0], args [1]);
	}

	else if (which == ML_LISTFN_APPEND || which == ML_LISTFN_CONCAT)
	{
		r = join (arena, which, args [0], args [1]);
	}

	else if (which == ML_LISTFN_TAKE || which == ML_LISTFN_DROP)
	{
		r = take_drop (arena, which, args [0], args [1]);
	}

	else
	{
		s = seq_of (arena, args [0]);

		if (s.error != ML_EVAL_NONE)
		{
			r = error_of (s.error);
		}

		else if (which == ML_LISTFN_LEN)
		{
			r = ml_value_from_i64 (arena, (int64_t)s.n);
		}

		else if (which == ML_LISTFN_TOLIST)
		{
			r = slice_list (arena, s.items, 0, s.n, 0);
		}

		else if (which == ML_LISTFN_REVERSE)
		{
			r = slice_list (arena, s.items, 0, s.n, 1);
		}

		else
		{
			r = to_vector (arena, s);
		}
	}

	return r;
}
