id: library
title: The standard library
summary: Functions written in meth itself: number theory, helpers, root finding, integration, differential equations.
keywords: library, stdlib, newton, bisect, simpson, trapezoid, euler, rk4, deriv, proj, gram_schmidt, approx, fib, isprime, primes, factor, divisors, totient, sigma, collatz, digits, isqrt, mod, sign, lerp, clamp, hypot, deg, rad, linspace
see: functions, lists, syntax, ex-algebra-library

The library is a set of functions written in the language of the tool and
defined before the first line of every session. They are ordinary functions:
the watch window does not list them, a definition of your own with the same
name hides the library one, and the files are in the folder `stdlib` of the
source if you want to read how they are made. The names that start with an
underscore are helpers of the library.

# Numbers
```code
mod (a, b)         the remainder of a divided by b, with the sign of b
sign (x)           1, -1 or 0
frac (x)           the part after the point
trunc (x)          the whole part, cut toward zero
lerp (a, b, t)     the point at the fraction t of the way from a to b
clamp (x, lo, hi)  x held between lo and hi
hypot (a, b)       the length of the hypotenuse
deg (r)  rad (d)   radians to degrees and back
```

```example
mod(-1, 5)
=> 4
lerp(10, 20, 1/4)
=> 25/2
clamp(15, 0, 10)
=> 10
hypot(3, 4)
=> 5
```

# Number theory
```code
fib (n)            the n-th Fibonacci number, exact also for big n
isqrt (n)          the whole part of the square root, exact
isprime (n)        true for a prime (trial division, fast up to about 10^10)
primes (n)         the primes up to n
factor (n)         the prime factors in order
divisors (n)       the numbers that divide n
totient (n)        how many of 1 ... n share no factor with n
sigma (n)          the sum of the divisors
collatz (n)        the steps of the 3n + 1 walk down to 1
digits (n)         the decimal digits as a list
```

```example
fib(100)
=> 354224848179261915075
isprime(1000003)
=> true
primes(30)
=> [2, 3, 5, 7, 11, 13, 17, 19, 23, 29]
factor(360)
=> [2, 2, 2, 3, 3, 5]
divisors(28)
=> [1, 2, 4, 7, 14, 28]
sigma(28)
=> 56
collatz(27)
=> 111
digits(9045)
=> [9, 0, 4, 5]
```

# Lists of numbers
```code
linspace (a, b, n)   n numbers from a to b, evenly spaced
cumprod (xs)         the running products
dotl (xs, ys)        the dot product of two lists
normalize (xs)       the list divided by its largest absolute value
```

```example
linspace(0, 1, 5)
=> [0, 1/4, 1/2, 3/4, 1]
cumprod(range(1, 5))
=> [1, 2, 6, 24, 120]
dotl(range(1, 3), range(4, 6))
=> 32
```

# Functions that take a function
These take a function as an argument: the name of a function (yours, or a
built in one such as `sin`) or a short function `x -> expression` written in
place. The function `f` is called as `f(x)` inside them.

```code
newton (f, df, x0, n)        n steps of Newton's method for f (x) = 0; approximate
bisect (f, a, b, n)          n halvings of [a, b] where f changes sign; exact
deriv (f, x, h)              the slope at x from x - h and x + h
trapezoid (f, a, b, n)       the area under f from a to b, n trapezoids
simpson (f, a, b, n)         the same by Simpson's rule, n even
euler (f, x0, y0, h, n)      y after n Euler steps of y' = f (x, y); approximate
rk4 (f, x0, y0, h, n)        the same by Runge-Kutta; approximate
rk4path (f, x0, y0, h, n)    the list of all the y from step 0 to n
proj (u, v)                  the projection of the vector v onto u
gram_schmidt (vs)            orthogonal vectors from a list of vectors
```

```example
newton(x -> x^2 - 2, x -> 2x, 1, 6)
=> ≈ 1.41421356
bisect(x -> x^2 - 2, 1, 2, 20)
=> 2965821/2097152
simpson(x -> x^3, 0, 2, 4)
=> 4
trapezoid(x -> x^2, 0, 1, 100)
=> 6667/20000
rk4((x, y) -> y, 0, 1, 1/10, 10)
=> ≈ 2.71827974
proj([1, 0], [3, 4])
=> [[3], [0]]
```

A function that is given to another one sees the variables of the session and
its own names, not the names of the line it was written in: in
`mix(c) = apply(x -> x + c, 1)` the `c` is not known inside the `x -> ...`.
Write the value into the call instead, or use a variable. `approx (x)` turns an
exact number into an approximation, which is how `newton` and `rk4` keep their
numbers short.

The statistics functions and the loops (`map`, `filter`, `fold`, `sum`) are
built in, see [statistics](statistics) and [lists](lists). Every line has a
limit of two million steps, so a big argument ends with the error `too_long`.

# Physics
In the physics domain the library has `kinetic (m, v)`, `momentum (m, v)`,
`weight (m)` and `speed (d, t)`, which work on quantities:
`kinetic (2 kg, 3 m/s) to J` is `9 J`.
