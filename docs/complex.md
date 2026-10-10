id: complex
title: Complex numbers and quaternions
summary: The unit i, complex arithmetic, quaternions with i, j and k, and rotations.
keywords: complex, imaginary, quaternion, i, j, k, conj, abs, arg, polar, rotate, hamilton, rotation
see: functions, matrices, ex-complex-quadratic

`i` is the imaginary unit with `i*i = -1`, and the square root of a negative
number is a complex number: `sqrt(-4)` is `2i`. A number with an imaginary
part is written `3 + 4i`. The parts are exact fractions while everything
that made them is exact, and doubles after an approximation (`≈`).

```example
(1+2i)*(3-i)
=> 5 + 5i
(1+2i)/(3-i)
=> 1/10 + (7/10)i
abs(3+4i)
=> 5
sqrt(-4)
=> 2i
conj(3+4i)
=> 3 - 4i
```

# Functions
```code
re (z)  im (z)         the real part and the imaginary part
conj (z)               3 - 4i for 3 + 4i
abs (z)                the modulus, exact when it is a rational
arg (z)                the angle in radians (approximate)
polar (r, theta)       r (cos theta + i sin theta)
exp ln sin cos sqrt    of a complex number too (approximate)
```
A complex number to a whole power is exact (`(1+i)^8` is 16); to any other
power it is the principal value (`(-8)^(1/3)`, `i^i`). `exp(i*pi)` shows
`≈ -1`: a part that is tiny against the others is shown as zero.

# Quaternions
A quaternion is `a + b i + c j + d k` with `i*j = k`, `j*k = i` and
`k*i = j`, and `j*i = -k`: the product does not commute. The unit `i` is
the same as that of the complex numbers, which are the quaternions with
`c = d = 0`.

```code
quat (w, x, y, z)      the quaternion w + x i + y j + z k
abs (q)  conj (q)      the norm and the conjugate
1/q  q/r               the inverse; q/r is q times 1/r
re (q)  im (q)         the real part, the vector part as [[x], [y], [z]]
rotate (q, v)          the 3 vector v turned by q: q v q^-1
```

```example
i*j
=> k
j*i
=> -k
rotate(1+k, [1,0,0])
=> [[0], [1], [0]]
```

`1 + k` is a quarter turn about the z axis (a rotation by the angle a has the
quaternion `cos(a/2) + sin(a/2)(x i + y j + z k)` for the unit axis x, y, z;
the length does not matter, `rotate` divides it out). Two rotations in a row
are the product, the second on the left. See
[the quaternion example](ex-quat-rotation).

# The names i, j and k
They are values only where a value is needed, and a variable or a parameter
of that name hides them (`k = 5` makes `2k` 10). The symbolic functions
still read them as symbols, so `diff(k*x, k)` is `x`. Matrices hold exact
real numbers only, for now.
