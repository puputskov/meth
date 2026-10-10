id: functions
title: Functions and constants
summary: The built in functions of the algebra domain.
keywords: sqrt, abs, sin, cos, tan, ln, log, exp, floor, ceil, round, min, max, gcd, lcm, pi, if, not, true, false
see: syntax, symbolic, statistics, complex

The built in functions take their arguments in parentheses:
`sqrt(x)`, `max(1, 5, 3)`.

# Numbers
- `sqrt abs` the root and the absolute value.
- `floor ceil round` whole numbers near a value.
- `min max` the smallest and the largest of their arguments, or of a list.
- `gcd lcm` the greatest common divisor and the least common multiple.

# Analysis
- `sin cos tan asin acos atan` the trigonometric functions, in radians.
- `ln log exp` logarithms and the exponential.

# Conditions
- `if (c, a, b)` the value `a` when `c` is true, else `b`; only that branch is
  evaluated.
- `not (c)` turns true into false and back. `and`, `or` and the comparisons
  are operators, see [syntax](syntax).

# Constants
`true` and `false`, and `pi` and `e` (also written `π`). They evaluate to approximations. `i`, `j`
and `k` are the imaginary units, see [complex](complex).

```example
gcd(12, 18)
=> 6
max(1, 5, 3)
=> 5
floor(2.5)
=> 2
```
