id: symbolic
title: Symbolic math
summary: Simplify, expand, differentiate, integrate, limits and equations.
keywords: simplify, expand, subs, diff, derivative, integrate, integral, limit, solve, equation, symbols
see: syntax, commands

Inside these functions names that have no value are symbols, so
`diff(x^3, x)` works without defining `x`. Outside them, a name with no
value is an error unless symbols are turned on with `:symbols on` (see
[commands](commands)); the examples on this page are written with symbols
on.

# The functions
- `simplify(f)` and `expand(f)` rewrite an expression.
- `subs(f, x, v)` puts a value for a name.
- `diff(f, x [, n])` the derivative, `n` times.
- `integrate(f, x [, a, b])` an antiderivative, or the definite integral
  from `a` to `b`.
- `limit(f, x, a [, side])` a limit; `inf` is infinity.
- `solve(f, x)` the real roots of `f` as a set.

```example
simplify(x + x)
=> 2x
expand((x+1)^2)
=> x² + 2x + 1
diff(x^3, x)
=> 3x²
integrate(x^2, x)
=> x³/3
integrate(x^2, x, 0, 1)
=> 1/3
limit(sin(x)/x, x, 0)
=> 1
solve(x^2 - 4, x)
=> {-2, 2}
```
