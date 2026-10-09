#include "test.h"

#include <matlog/math/unit.h>

static MlSlice lit (const char *s)
{
	return ml_slice_cstr (s);
}

static MlDim energy_dim (void);

static MlDim force_dim (void)
{
	MlDim kg = ml_dim_base (ML_DIM_MASS);
	MlDim m  = ml_dim_base (ML_DIM_LENGTH);
	MlDim s  = ml_dim_base (ML_DIM_TIME);

	return ml_dim_div (ml_dim_mul (kg, m), ml_dim_pow (s, 2));
}

static MlDim energy_dim (void)
{
	return ml_dim_mul (force_dim (), ml_dim_base (ML_DIM_LENGTH));
}

static int32_t dim_is (MlDim d, const char *expected)
{
	MlStr   s;
	int32_t same;

	ml_str_init (&s);
	same = (
		   ml_dim_format (&s, d) == ML_OK
		&& ml_slice_equal (ml_str_view (s), lit (expected))
	);

	if (!same)
	{
		printf (
			"  dimension is \"%s\", expected \"%s\"\n",
			ml_str_cstr (s),
			expected
		);
	}

	ml_str_free (&s);
	return same;
}

/* The factor of a unit name as text: "1000", "1/1000". */
static int32_t factor_is (const char *name, const char *expected)
{
	MlArena a;
	MlStr   s;
	int32_t same;
	MlRat   f;

	ml_arena_init (&a, 0);
	ml_str_init (&s);
	f    = ml_unit_factor (&a, ml_unit_find (lit (name)));
	same = (
		   ml_rat_format (&s, f) == ML_OK
		&& ml_slice_equal (ml_str_view (s), lit (expected))
	);

	if (!same)
	{
		printf (
			"  factor of %s is \"%s\", expected \"%s\"\n",
			name,
			ml_str_cstr (s),
			expected
		);
	}

	ml_str_free (&s);
	ml_arena_free (&a);
	return same;
}

static int32_t dim_of_is (const char *name, MlDim expected)
{
	MlUnitMatch m = ml_unit_find (lit (name));

	return m.found && ml_dim_equal (m.dim, expected);
}

static void test_dimensions (void)
{
	MlDim length = ml_dim_base (ML_DIM_LENGTH);
	MlDim time   = ml_dim_base (ML_DIM_TIME);
	MlDim speed  = ml_dim_div (length, time);

	CHECK (ml_dim_is_none (ml_dim_none ()));
	CHECK (!ml_dim_is_none (length));
	CHECK (ml_dim_equal (length, ml_dim_base (ML_DIM_LENGTH)));
	CHECK (!ml_dim_equal (length, time));
	CHECK (ml_dim_is_none (ml_dim_div (length, length)));
	CHECK (ml_dim_equal (ml_dim_mul (speed, time), length));
	CHECK (ml_dim_equal (ml_dim_pow (length, 0), ml_dim_none ()));
	CHECK (ml_dim_equal (
		ml_dim_pow (time, -2),
		ml_dim_div (ml_dim_none (), ml_dim_pow (time, 2))
	));
}

static void test_format (void)
{
	MlDim length = ml_dim_base (ML_DIM_LENGTH);
	MlDim mass   = ml_dim_base (ML_DIM_MASS);
	MlDim time   = ml_dim_base (ML_DIM_TIME);
	MlDim amp    = ml_dim_base (ML_DIM_CURRENT);

	CHECK (dim_is (ml_dim_none (), "1"));
	CHECK (dim_is (length, "m"));
	CHECK (dim_is (ml_dim_pow (length, 2), "m²"));
	CHECK (dim_is (ml_dim_pow (length, 12), "m¹²"));
	CHECK (dim_is (ml_dim_div (ml_dim_none (), time), "1/s"));
	CHECK (dim_is (ml_dim_div (length, time), "m/s"));
	CHECK (dim_is (force_dim (), "kg·m/s²"));
	CHECK (dim_is (ml_dim_pow (mass, 2), "kg²"));
	CHECK (dim_is (
		ml_dim_div (force_dim (), ml_dim_mul (time, amp)),
		"kg·m/(s³·A)"
	));
}

static void test_find (void)
{
	CHECK (ml_unit_find (lit ("m")).found);
	CHECK (ml_unit_find (lit ("meter")).found);
	CHECK (ml_unit_find (lit ("metre")).found);
	CHECK (ml_unit_find (lit ("kg")).found);
	CHECK (ml_unit_find (lit ("kilogram")).found);
	CHECK (ml_unit_find (lit ("mm")).found);
	CHECK (ml_unit_find (lit ("millimeter")).found);
	CHECK (ml_unit_find (lit ("µs")).found);
	CHECK (ml_unit_find (lit ("us")).found);
	CHECK (ml_unit_find (lit ("kΩ")).found);
	CHECK (ml_unit_find (lit ("kWh")).found);

	CHECK (!ml_unit_find (lit ("")).found);
	CHECK (!ml_unit_find (lit ("x")).found);
	CHECK (!ml_unit_find (lit ("k")).found);
	CHECK (!ml_unit_find (lit ("kilo")).found);
	CHECK (!ml_unit_find (lit ("kkg")).found);
	CHECK (!ml_unit_find (lit ("kmin")).found);
	CHECK (!ml_unit_find (lit ("kin")).found);
	CHECK (!ml_unit_find (lit ("kilom")).found);
	CHECK (!ml_unit_find (lit ("kmeter")).found);
	CHECK (!ml_unit_find (lit ("M")).found);
	CHECK (!ml_unit_find (lit ("mmm")).found);
}

static void test_dimension_of_units (void)
{
	CHECK (dim_of_is ("m", ml_dim_base (ML_DIM_LENGTH)));
	CHECK (dim_of_is ("kg", ml_dim_base (ML_DIM_MASS)));
	CHECK (dim_of_is ("g", ml_dim_base (ML_DIM_MASS)));
	CHECK (dim_of_is ("h", ml_dim_base (ML_DIM_TIME)));
	CHECK (dim_of_is ("N", force_dim ()));
	CHECK (dim_of_is ("kN", force_dim ()));
	CHECK (dim_of_is ("L", ml_dim_pow (ml_dim_base (ML_DIM_LENGTH), 3)));
	CHECK (dim_of_is ("ha", ml_dim_pow (ml_dim_base (ML_DIM_LENGTH), 2)));
	CHECK (dim_of_is ("J", energy_dim ()));
	CHECK (!dim_of_is ("J", force_dim ()));
}

static void test_factors (void)
{
	CHECK (factor_is ("m", "1"));
	CHECK (factor_is ("km", "1000"));
	CHECK (factor_is ("mm", "1/1000"));
	CHECK (factor_is ("cm", "1/100"));
	CHECK (factor_is ("um", "1/1000000"));
	CHECK (factor_is ("g", "1/1000"));
	CHECK (factor_is ("kg", "1"));
	CHECK (factor_is ("mg", "1/1000000"));
	CHECK (factor_is ("kilogram", "1"));
	CHECK (factor_is ("t", "1000"));
	CHECK (factor_is ("min", "60"));
	CHECK (factor_is ("h", "3600"));
	CHECK (factor_is ("d", "86400"));
	CHECK (factor_is ("ms", "1/1000"));
	CHECK (factor_is ("L", "1/1000"));
	CHECK (factor_is ("mL", "1/1000000"));
	CHECK (factor_is ("ha", "10000"));
	CHECK (factor_is ("bar", "100000"));
	CHECK (factor_is ("mbar", "100"));
	CHECK (factor_is ("atm", "101325"));
	CHECK (factor_is ("Wh", "3600"));
	CHECK (factor_is ("kWh", "3600000"));
	CHECK (factor_is ("cal", "523/125"));
	CHECK (factor_is ("in", "127/5000"));
	CHECK (factor_is ("ft", "381/1250"));
	CHECK (factor_is ("mi", "201168/125"));
	CHECK (factor_is ("lb", "45359237/100000000"));
	CHECK (factor_is ("oz", "45359237/1600000000"));
	CHECK (factor_is ("eV", "801088317/5000000000000000000000000000"));
	CHECK (factor_is ("dam", "10"));
	CHECK (factor_is ("daN", "10"));
	CHECK (factor_is ("dm", "1/10"));
	CHECK (factor_is ("Tm", "1000000000000"));
}

static void test_unknown (void)
{
	MlArena a;

	ml_arena_init (&a, 0);
	CHECK (
		ml_rat_status (ml_unit_factor (&a, ml_unit_find (lit ("x"))))
		== ML_ERR_INVALID
	);
	ml_arena_free (&a);
}

static void test_unit_names (void)
{
	CHECK (ml_slice_equal (ml_dim_unit_name (force_dim ()), lit ("N")));
	CHECK (ml_slice_equal (
		ml_dim_unit_name (ml_dim_base (ML_DIM_LENGTH)),
		lit ("m")
	));
	CHECK (ml_slice_equal (
		ml_dim_unit_name (ml_dim_base (ML_DIM_MASS)),
		lit ("kg")
	));
	CHECK (ml_slice_equal (
		ml_dim_unit_name (energy_dim ()),
		lit ("J")
	));
	CHECK (ml_slice_equal (
		ml_dim_unit_name (ml_dim_pow (ml_dim_base (ML_DIM_TIME), -1)),
		lit ("Hz")
	));
	CHECK (ml_dim_unit_name (ml_dim_none ()).len == 0);
	CHECK (
		ml_dim_unit_name (ml_dim_pow (ml_dim_base (ML_DIM_LENGTH), 2)).len == 0
	);
}

void run_unit_tests (void)
{
	test_dimensions ();
	test_format ();
	test_find ();
	test_dimension_of_units ();
	test_factors ();
	test_unknown ();
	test_unit_names ();
}
