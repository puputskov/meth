#include <matlog/math/unit.h>

#include <string.h>

/*
 * The dimensions the unit table refers to. Exponents are in the order of
 * MlBaseDim: length, mass, time, current, temperature, amount, luminosity.
 */
typedef enum DimKind
{
	DK_LENGTH,
	DK_MASS,
	DK_TIME,
	DK_CURRENT,
	DK_TEMPERATURE,
	DK_AMOUNT,
	DK_LUMINOSITY,
	DK_AREA,
	DK_VOLUME,
	DK_FREQUENCY,
	DK_FORCE,
	DK_PRESSURE,
	DK_ENERGY,
	DK_POWER,
	DK_CHARGE,
	DK_VOLTAGE,
	DK_CAPACITANCE,
	DK_RESISTANCE,
	DK_CONDUCTANCE,
	DK_FLUX,
	DK_FIELD,
	DK_INDUCTANCE,
	DK_COUNT
} DimKind;

static const MlDim k_dims [DK_COUNT] =
{
	[DK_LENGTH]      = { .e = { 1, 0,  0,  0, 0, 0, 0 } },
	[DK_MASS]        = { .e = { 0, 1,  0,  0, 0, 0, 0 } },
	[DK_TIME]        = { .e = { 0, 0,  1,  0, 0, 0, 0 } },
	[DK_CURRENT]     = { .e = { 0, 0,  0,  1, 0, 0, 0 } },
	[DK_TEMPERATURE] = { .e = { 0, 0,  0,  0, 1, 0, 0 } },
	[DK_AMOUNT]      = { .e = { 0, 0,  0,  0, 0, 1, 0 } },
	[DK_LUMINOSITY]  = { .e = { 0, 0,  0,  0, 0, 0, 1 } },
	[DK_AREA]        = { .e = { 2, 0,  0,  0, 0, 0, 0 } },
	[DK_VOLUME]      = { .e = { 3, 0,  0,  0, 0, 0, 0 } },
	[DK_FREQUENCY]   = { .e = { 0, 0, -1,  0, 0, 0, 0 } },
	[DK_FORCE]       = { .e = { 1, 1, -2,  0, 0, 0, 0 } },
	[DK_PRESSURE]    = { .e = { -1, 1, -2, 0, 0, 0, 0 } },
	[DK_ENERGY]      = { .e = { 2, 1, -2,  0, 0, 0, 0 } },
	[DK_POWER]       = { .e = { 2, 1, -3,  0, 0, 0, 0 } },
	[DK_CHARGE]      = { .e = { 0, 0,  1,  1, 0, 0, 0 } },
	[DK_VOLTAGE]     = { .e = { 2, 1, -3, -1, 0, 0, 0 } },
	[DK_CAPACITANCE] = { .e = { -2, -1, 4, 2, 0, 0, 0 } },
	[DK_RESISTANCE]  = { .e = { 2, 1, -3, -2, 0, 0, 0 } },
	[DK_CONDUCTANCE] = { .e = { -2, -1, 3, 2, 0, 0, 0 } },
	[DK_FLUX]        = { .e = { 2, 1, -2, -1, 0, 0, 0 } },
	[DK_FIELD]       = { .e = { 0, 1, -2, -1, 0, 0, 0 } },
	[DK_INDUCTANCE]  = { .e = { 2, 1, -2, -2, 0, 0, 0 } }
};

/*
 * A unit: the factor to the coherent SI unit is num / den * 10^e10, where an
 * omitted num or den is 1. fixed is 1 when no SI prefix may be put in front.
 * The kilogram is the gram with the prefix kilo, so the table has no kilogram.
 */
typedef struct UnitDef
{
	const char *sym;
	const char *name;
	DimKind    dk;
	int64_t    num;
	int64_t    den;
	int32_t    e10;
	int32_t    fixed;
} UnitDef;

static const UnitDef k_units [] =
{
	{ .sym = "m",   .name = "meter",      .dk = DK_LENGTH },
	{ .sym = "",    .name = "metre",      .dk = DK_LENGTH },
	{ .sym = "g",   .name = "gram",       .dk = DK_MASS, .den = 1000 },
	{ .sym = "s",   .name = "second",     .dk = DK_TIME },
	{ .sym = "A",   .name = "ampere",     .dk = DK_CURRENT },
	{ .sym = "K",   .name = "kelvin",     .dk = DK_TEMPERATURE },
	{ .sym = "mol", .name = "mole",       .dk = DK_AMOUNT },
	{ .sym = "cd",  .name = "candela",    .dk = DK_LUMINOSITY },
	{ .sym = "Hz",  .name = "hertz",      .dk = DK_FREQUENCY },
	{ .sym = "N",   .name = "newton",     .dk = DK_FORCE },
	{ .sym = "Pa",  .name = "pascal",     .dk = DK_PRESSURE },
	{ .sym = "J",   .name = "joule",      .dk = DK_ENERGY },
	{ .sym = "W",   .name = "watt",       .dk = DK_POWER },
	{ .sym = "C",   .name = "coulomb",    .dk = DK_CHARGE },
	{ .sym = "V",   .name = "volt",       .dk = DK_VOLTAGE },
	{ .sym = "F",   .name = "farad",      .dk = DK_CAPACITANCE },
	{ .sym = "Ω",   .name = "ohm",        .dk = DK_RESISTANCE },
	{ .sym = "S",   .name = "siemens",    .dk = DK_CONDUCTANCE },
	{ .sym = "Wb",  .name = "weber",      .dk = DK_FLUX },
	{ .sym = "T",   .name = "tesla",      .dk = DK_FIELD },
	{ .sym = "H",   .name = "henry",      .dk = DK_INDUCTANCE },
	{ .sym = "min", .name = "minute", .dk = DK_TIME, .num = 60, .fixed = 1 },
	{ .sym = "h",   .name = "hour",   .dk = DK_TIME, .num = 3600, .fixed = 1 },
	{ .sym = "d",   .name = "day",    .dk = DK_TIME, .num = 86400, .fixed = 1 },
	{ .sym = "L",   .name = "liter",  .dk = DK_VOLUME, .den = 1000 },
	{ .sym = "l",   .name = "litre",  .dk = DK_VOLUME, .den = 1000 },
	{ .sym = "ha", .name = "hectare", .dk = DK_AREA, .num = 10000, .fixed = 1 },
	{ .sym = "t",   .name = "tonne",  .dk = DK_MASS, .num = 1000 },
	{ .sym = "bar", .name = "bar",    .dk = DK_PRESSURE, .e10 = 5 },
	{
		.sym = "atm", .name = "atmosphere", .dk = DK_PRESSURE,
		.num = 101325, .fixed = 1
	},
	{ .sym = "Wh",  .name = "watthour", .dk = DK_ENERGY, .num = 3600 },
	{
		.sym = "cal", .name = "calorie", .dk = DK_ENERGY,
		.num = 4184, .den = 1000
	},
	{
		.sym = "eV", .name = "electronvolt", .dk = DK_ENERGY,
		.num = 1602176634, .e10 = -28
	},
	{
		.sym = "in", .name = "inch", .dk = DK_LENGTH,
		.num = 254, .den = 10000, .fixed = 1
	},
	{
		.sym = "ft", .name = "foot", .dk = DK_LENGTH,
		.num = 3048, .den = 10000, .fixed = 1
	},
	{
		.sym = "yd", .name = "yard", .dk = DK_LENGTH,
		.num = 9144, .den = 10000, .fixed = 1
	},
	{
		.sym = "mi", .name = "mile", .dk = DK_LENGTH,
		.num = 1609344, .den = 1000, .fixed = 1
	},
	{
		.sym = "lb", .name = "pound", .dk = DK_MASS,
		.num = 45359237, .den = 100000000, .fixed = 1
	},
	{
		.sym = "oz", .name = "ounce", .dk = DK_MASS,
		.num = 45359237, .den = 1600000000, .fixed = 1
	}
};

typedef struct Prefix
{
	const char *sym;
	const char *name;
	int32_t    e10;
} Prefix;

static const Prefix k_prefixes [] =
{
	{ .sym = "Y",  .name = "yotta", .e10 = 24 },
	{ .sym = "Z",  .name = "zetta", .e10 = 21 },
	{ .sym = "E",  .name = "exa",   .e10 = 18 },
	{ .sym = "P",  .name = "peta",  .e10 = 15 },
	{ .sym = "T",  .name = "tera",  .e10 = 12 },
	{ .sym = "G",  .name = "giga",  .e10 = 9 },
	{ .sym = "M",  .name = "mega",  .e10 = 6 },
	{ .sym = "k",  .name = "kilo",  .e10 = 3 },
	{ .sym = "h",  .name = "hecto", .e10 = 2 },
	{ .sym = "da", .name = "deca",  .e10 = 1 },
	{ .sym = "d",  .name = "deci",  .e10 = -1 },
	{ .sym = "c",  .name = "centi", .e10 = -2 },
	{ .sym = "m",  .name = "milli", .e10 = -3 },
	{ .sym = "µ",  .name = "micro", .e10 = -6 },
	{ .sym = "μ",  .name = "",      .e10 = -6 },
	{ .sym = "u",  .name = "",      .e10 = -6 },
	{ .sym = "n",  .name = "nano",  .e10 = -9 },
	{ .sym = "p",  .name = "pico",  .e10 = -12 },
	{ .sym = "f",  .name = "femto", .e10 = -15 },
	{ .sym = "a",  .name = "atto",  .e10 = -18 },
	{ .sym = "z",  .name = "zepto", .e10 = -21 },
	{ .sym = "y",  .name = "yocto", .e10 = -24 }
};

static const char *const k_base_symbol [ML_DIM_COUNT] =
{
	[ML_DIM_LENGTH]      = "m",
	[ML_DIM_MASS]        = "kg",
	[ML_DIM_TIME]        = "s",
	[ML_DIM_CURRENT]     = "A",
	[ML_DIM_TEMPERATURE] = "K",
	[ML_DIM_AMOUNT]      = "mol",
	[ML_DIM_LUMINOSITY]  = "cd"
};

/* The order the base units are written in: kg·m/s², not m·kg/s². */
static const MlBaseDim k_order [ML_DIM_COUNT] =
{
	ML_DIM_MASS,
	ML_DIM_LENGTH,
	ML_DIM_TIME,
	ML_DIM_CURRENT,
	ML_DIM_TEMPERATURE,
	ML_DIM_AMOUNT,
	ML_DIM_LUMINOSITY
};

static const char *const k_super [10] =
{
	"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"
};

MlDim ml_dim_none (void)
{
	MlDim d = { .e = { 0, 0, 0, 0, 0, 0, 0 } };

	return d;
}

MlDim ml_dim_base (MlBaseDim base)
{
	MlDim d = ml_dim_none ();

	if (base < ML_DIM_COUNT)
	{
		d.e [base] = 1;
	}

	return d;
}

MlDim ml_dim_mul (MlDim a, MlDim b)
{
	MlDim d = ml_dim_none ();

	for (uint64_t i = 0; i < ML_DIM_COUNT; i++)
	{
		d.e [i] = a.e [i] + b.e [i];
	}

	return d;
}

MlDim ml_dim_div (MlDim a, MlDim b)
{
	MlDim d = ml_dim_none ();

	for (uint64_t i = 0; i < ML_DIM_COUNT; i++)
	{
		d.e [i] = a.e [i] - b.e [i];
	}

	return d;
}

MlDim ml_dim_pow (MlDim a, int32_t n)
{
	MlDim d = ml_dim_none ();

	for (uint64_t i = 0; i < ML_DIM_COUNT; i++)
	{
		d.e [i] = a.e [i] * n;
	}

	return d;
}

int32_t ml_dim_equal (MlDim a, MlDim b)
{
	int32_t same = 1;

	for (uint64_t i = 0; i < ML_DIM_COUNT; i++)
	{
		if (a.e [i] != b.e [i])
		{
			same = 0;
		}
	}

	return same;
}

int32_t ml_dim_is_none (MlDim a)
{
	return ml_dim_equal (a, ml_dim_none ());
}

/* How many base dimensions have a positive (sign 1) or negative exponent. */
static int32_t count_side (MlDim d, int32_t sign)
{
	int32_t n = 0;

	for (uint64_t i = 0; i < ML_DIM_COUNT; i++)
	{
		if (d.e [i] * sign > 0)
		{
			n++;
		}
	}

	return n;
}

/* "m" or "m²": the symbol of a base unit and its exponent as superscript. */
static MlResult append_term (MlStr *out, MlBaseDim base, int32_t power)
{
	MlResult r     = ml_str_append (out, ml_slice_cstr (k_base_symbol [base]));
	char     digits [12];
	int32_t  count = 0;
	int32_t  rest  = power;

	while (power > 1 && rest > 0)
	{
		digits [count] = (char)('0' + rest % 10);
		count++;
		rest /= 10;
	}

	for (int32_t i = count - 1; i >= 0 && r == ML_OK; i--)
	{
		r = ml_str_append (out, ml_slice_cstr (k_super [digits [i] - '0']));
	}

	return r;
}

/* The terms on one side of the fraction bar, joined with a middle dot. */
static MlResult append_side (MlStr *out, MlDim d, int32_t sign)
{
	MlResult r     = ML_OK;
	int32_t  first = 1;

	for (uint64_t i = 0; i < ML_DIM_COUNT && r == ML_OK; i++)
	{
		MlBaseDim base = k_order [i];

		if (d.e [base] * sign > 0)
		{
			if (!first)
			{
				r = ml_str_append (out, ml_slice_cstr ("·"));
			}

			if (r == ML_OK)
			{
				r = append_term (out, base, d.e [base] * sign);
			}

			first = 0;
		}
	}

	return r;
}

MlResult ml_dim_format (MlStr *out, MlDim d)
{
	MlResult r    = ML_OK;
	int32_t  nums = count_side (d, 1);
	int32_t  dens = count_side (d, -1);

	if (nums == 0)
	{
		r = ml_str_append (out, ml_slice_cstr ("1"));
	}

	else
	{
		r = append_side (out, d, 1);
	}

	if (r == ML_OK && dens > 0)
	{
		r = ml_str_append (out, ml_slice_cstr ("/"));

		if (r == ML_OK && dens > 1)
		{
			r = ml_str_append (out, ml_slice_cstr ("("));
		}

		if (r == ML_OK)
		{
			r = append_side (out, d, -1);
		}

		if (r == ML_OK && dens > 1)
		{
			r = ml_str_append (out, ml_slice_cstr (")"));
		}
	}

	return r;
}

/* 1 when the row's factor to the SI unit is exactly one. */
static int32_t row_is_coherent (uint64_t row)
{
	UnitDef u = k_units [row];

	return (
		   (u.num == 0 || u.num == 1)
		&& (u.den == 0 || u.den == 1)
		&& u.e10 == 0
	);
}

MlSlice ml_dim_unit_name (MlDim d)
{
	MlSlice name = ml_slice ("", 0);

	/* the kilogram is not in the table, see UnitDef */
	if (ml_dim_equal (d, k_dims [DK_MASS]))
	{
		name = ml_slice_cstr ("kg");
	}

	for (uint64_t i = 0; i < ml_count (k_units) && name.len == 0; i++)
	{
		if (
			   row_is_coherent (i)
			&& k_units [i].sym [0] != '\0'
			&& ml_dim_equal (d, k_dims [k_units [i].dk])
		)
		{
			name = ml_slice_cstr (k_units [i].sym);
		}
	}

	return name;
}

/* 1 when s is the non-empty text c. */
static int32_t is_text (MlSlice s, const char *c)
{
	return s.len > 0 && ml_slice_equal (s, ml_slice_cstr (c));
}

typedef struct RowHit
{
	int32_t row;     /* index in k_units, -1 when there is none */
	int32_t is_name; /* 1 when the long name matched, 0 for the symbol */
} RowHit;

static RowHit find_row (MlSlice s)
{
	RowHit hit =
	{
		.row     = -1,
		.is_name = 0
	};

	for (uint64_t i = 0; i < ml_count (k_units) && hit.row < 0; i++)
	{
		if (is_text (s, k_units [i].sym))
		{
			hit.row     = (int32_t)i;
			hit.is_name = 0;
		}

		else if (is_text (s, k_units [i].name))
		{
			hit.row     = (int32_t)i;
			hit.is_name = 1;
		}
	}

	return hit;
}

static MlUnitMatch no_match (void)
{
	MlUnitMatch m =
	{
		.found = 0,
		.dim   = { .e = { 0, 0, 0, 0, 0, 0, 0 } },
		.num   = 1,
		.den   = 1,
		.exp10 = 0
	};

	return m;
}

/* The match of a table row, with the exponent of a prefix added. */
static MlUnitMatch match_row (int32_t row, int32_t prefix_e10)
{
	UnitDef     u = k_units [row];
	MlUnitMatch m = no_match ();

	m.found = 1;
	m.dim   = k_dims [u.dk];
	m.exp10 = u.e10 + prefix_e10;

	if (u.num != 0)
	{
		m.num = u.num;
	}

	if (u.den != 0)
	{
		m.den = u.den;
	}

	return m;
}

static const char *prefix_text (uint64_t i, int32_t by_name)
{
	return ml_choose (
		by_name,
		k_prefixes [i].name,
		k_prefixes [i].sym
	);
}

/* A symbol prefix goes with a symbol, a prefix name with a unit name. */
static MlUnitMatch find_prefixed (MlSlice name)
{
	MlUnitMatch m = no_match ();

	for (uint64_t i = 0; i < ml_count (k_prefixes) && !m.found; i++)
	{
		for (int32_t by_name = 0; by_name < 2 && !m.found; by_name++)
		{
			const char *p   = prefix_text (i, by_name);
			uint64_t    len = strlen (p);

			if (
				   len > 0
				&& name.len > len
				&& memcmp (name.data, p, len) == 0
			)
			{
				RowHit hit = find_row (
					ml_slice_sub (name, len, name.len - len)
				);

				if (
					   hit.row >= 0
					&& hit.is_name == by_name
					&& !k_units [hit.row].fixed
				)
				{
					m = match_row (hit.row, k_prefixes [i].e10);
				}
			}
		}
	}

	return m;
}

MlUnitMatch ml_unit_find (MlSlice name)
{
	RowHit      hit = find_row (name);
	MlUnitMatch m   = no_match ();

	if (hit.row >= 0)
	{
		m = match_row (hit.row, 0);
	}

	else
	{
		m = find_prefixed (name);
	}

	return m;
}

MlRat ml_unit_factor (MlArena *arena, MlUnitMatch m)
{
	MlRat r = ml_rat_error (ML_ERR_INVALID);

	if (m.found)
	{
		MlRat base = ml_rat_make (
			arena,
			ml_big_from_i64 (arena, m.num),
			ml_big_from_i64 (arena, m.den)
		);

		r = ml_rat_mul (
			arena,
			base,
			ml_rat_pow (arena, ml_rat_from_i64 (arena, 10), m.exp10)
		);
	}

	return r;
}
