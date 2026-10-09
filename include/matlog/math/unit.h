#ifndef MATLOG_MATH_UNIT_H
#define MATLOG_MATH_UNIT_H

#include <matlog/core/str.h>
#include <matlog/math/rational.h>

/*
 * Units of measurement. A dimension is a vector of exponents of the seven SI
 * base dimensions (a force is mass 1, length 1, time -2). A unit is a name
 * with a dimension and an exact factor to the coherent SI unit of that
 * dimension (a kilometre is 1000 metres, an hour 3600 seconds, a pound
 * 45359237/100000000 kilograms), so converting is one exact division.
 *
 * Names are looked up as a whole first ("min", "cd", "Pa"), then as an SI
 * prefix followed by a unit ("km", "mL", "kilogram", "µs"), so a name that is
 * a unit in its own right is never read as a prefix and something else.
 * Units that have an offset (degrees Celsius) are not here: an offset does
 * not survive multiplication, so they need a quantity type of their own.
 */

typedef enum MlBaseDim
{
	ML_DIM_LENGTH,
	ML_DIM_MASS,
	ML_DIM_TIME,
	ML_DIM_CURRENT,
	ML_DIM_TEMPERATURE,
	ML_DIM_AMOUNT,
	ML_DIM_LUMINOSITY,
	ML_DIM_COUNT
} MlBaseDim;

typedef struct MlDim
{
	int32_t e [ML_DIM_COUNT];
} MlDim;

/* What a name stands for: a dimension and a factor to the SI unit. */
typedef struct MlUnitMatch
{
	int32_t found;
	MlDim   dim;
	int64_t num;   /* the factor is num / den * 10^exp10 */
	int64_t den;
	int32_t exp10;
} MlUnitMatch;

/* Dimensions. The product and the quotient add and subtract exponents. */
MlDim ml_dim_none (void);
MlDim ml_dim_base (MlBaseDim base);
MlDim ml_dim_mul (MlDim a, MlDim b);
MlDim ml_dim_div (MlDim a, MlDim b);
MlDim ml_dim_pow (MlDim a, int32_t n);
int32_t ml_dim_equal (MlDim a, MlDim b);
int32_t ml_dim_is_none (MlDim a);

/*
 * The dimension as SI base units: "kg·m/s²", "m²", "1/s", "kg·m²/(s³·A)";
 * "1" when there is none.
 */
MlResult ml_dim_format (MlStr *out, MlDim d);

/*
 * The symbol of the named coherent SI unit of a dimension ("N" for a force,
 * "J" for an energy), or an empty slice when it has no name of its own. Where
 * two dimensions share one unit (Hz and Bq) the first wins.
 */
MlSlice ml_dim_unit_name (MlDim d);

/* Looks a unit name up, with or without an SI prefix. found is 0 if unknown. */
MlUnitMatch ml_unit_find (MlSlice name);

/* The factor of a match as an exact rational (ML_ERR_INVALID if not found). */
MlRat ml_unit_factor (MlArena *arena, MlUnitMatch m);

#endif /* MATLOG_MATH_UNIT_H */
