id: functions
title: Functions and constants
summary: The built in functions of the algebra domain.
keywords: steps, sqrt, abs, sin, cos, tan, ln, log, exp, floor, ceil, round, min, max, gcd, lcm, pi, if, not, true, false
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

# Lists and loops
- `range list len item append concat reverse take drop` and `xs[i]`, see [lists](lists).
- `map filter fold` and `sum (i, a, b, e)`, `prod (i, a, b, e)` run a function over a list or
  a range, see [lists](lists).

# Functions as arguments
A function you define can take a function: `apply(f, x) = f(x)` and then `apply(sqrt, 16)` or
`apply(x -> x * 2, 5)`. The argument is the name of a function or a short function
`x -> expression`; inside, the parameter is called like any function. It is not a value: it cannot
be added, named or returned. `approx (x)` makes an exact number an approximation. See the
[library](library) for `newton`, `simpson`, `rk4` and the others.

# Steps
`steps (e)` has the value of `e`, and marks its line: the steps of the first operation in `e` that
has some (`det`, `inv`, `rref`, `solve`, a product of matrices, `simplify`, `expand`, `diff`,
`integrate`, `limit`) are shown under the line without {ACTION_STEPS}, printed by `meth eval` and
`meth run`, and written after the line by `:export`. The call is left out of the typeset math:
`steps (det (A))` is exported as det A. Use it to make the lines of a tutorial or a document that
should show their work.

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
