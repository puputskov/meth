id: pga
title: Projective geometric algebra
summary: Points, lines, planes, motors and rotors with the geometric product, in 2D and 3D.
keywords: pga, geometric, algebra, multivector, blade, wedge, join, meet, dual, motor, rotor, translator, point, line, plane, e0, e1, e12
see: complex, matrices, ex-pga-motors

Projective geometric algebra (PGA) describes points, lines and planes, and
the moves that carry them, with one product. Switch a block to the domain
`pga2` (the plane, `R(2,0,1)`) or `pga3` (space, `R(3,0,1)`). A multivector
is a sum of blades; the coefficients are exact fractions while everything
that made them is exact, and doubles after an approximation (`≈`).

# Blades
The basis vectors are `e1`, `e2` (`e3` in 3D) and `e0`, which squares to
zero. A blade is a product of them in order: `e12`, `e20`, `e123`. A
swapped blade is the negative one: `e21` is `-e12`. On a pga line these
names are blades; a variable of the same name hides the blade.

```example pga2
e1*e1
=> 1
e0*e0
=> 0
e1*e2
=> e12
e2*e1
=> -e12
e12*e12
=> -1
```

# Products
```code
a * b                  the geometric product
a ^ b                  the wedge (outer) product
a / b                  a times the inverse of b
inner (a, b)           the inner product
q ^ n                  a multivector to a whole power: repeated product
```
A number to a power keeps its meaning, only a multivector in `^` is a wedge.

# Points, lines and planes
Vectors are planes in 3D and lines in 2D. A point is a grade 2 (2D) or
grade 3 (3D) blade, made by `point`.

```code
point (x, y)           a 2D point
point (x, y, z)        a 3D point
line (a, b, c)         the 2D line a x + b y + c = 0
plane (a, b, c, d)     the plane a x + b y + c z + d = 0
```

```example pga2
point(1, 2)
=> 2e01 - e02 + e12
line(1, 0, -2)
=> -2e0 + e1
```

# Join, meet and the dual
```code
join (a, b)            the line through two points, the plane through ...
meet (a, b)            the point where two lines cross, the line ...
dual (a)  undual (a)   swap a blade with the blade of the other part
reverse (a)            the order of every blade reversed
unit (a)  norm (a)     scaled to length 1, and the length
inv (a)                the inverse
scalar (a)             the number part
grade (a, k)           the part made of blades with k vectors
```

```example pga2
join(point(0, 0), point(1, 1))
=> -e1 + e2
meet(line(1, 0, -2), line(0, 1, -3))
=> 3e01 - 2e02 + e12
dual(e12)
=> e0
grade(e1 + e12, 2)
=> e12
```

# Moving things
A translator `translator (dx, dy)` and a rotor `rotor (angle)` are
multivectors that move other things by the sandwich product
`m * x * reverse (m)`. The angle is in radians, turning counterclockwise;
in 3D `rotor (angle, nx, ny, nz)` turns about the axis `(nx, ny, nz)`.
Motors multiply: the product is the move of both.

```example pga2
T = translator(3, 4)
T * point(1, 2) * reverse(T)
=> 6e01 - 4e02 + e12
```
