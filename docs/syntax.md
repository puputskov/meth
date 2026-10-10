id: syntax
title: The algebra syntax
summary: Numbers, operators, names and statements of a math line.
keywords: precedence, operators, implicit multiplication, fractions, exact, unicode, assignment
see: functions, units, symbolic

A math line in the algebra domain is an expression, an assignment
(`a = 3`) or the definition of a function (`f(x) = x^2`). Results are exact
whenever they can be: fractions stay fractions.

# Numbers and operators
Numbers are written `12`, `3.14` or `.5`. The operators are `+ - * / ^`
and the postfix `!` for the factorial. `^` binds tighter than `*` and `/`,
and is right associative; a minus in front binds looser than `^`.

```example
1/3 + 1/6
=> 1/2
-2^2
=> -4
2^3^2
=> 512
5!
=> 120
```

# Implicit products
A number, a closing parenthesis or a superscript directly followed by a
name, an opening parenthesis or a root is a product: `2x`, `2(x+1)`,
`(a)(b)`, `2pi`. Names are whole words, so `xy` is one name, not `x*y`.

# Unicode
The unicode spellings are the same as the ASCII ones: `x²` for `x^2`,
`√` for `sqrt`, `π` for `pi`, `×` and `·` for `*`, `÷` for `/`, `−` for `-`.

# Comparisons and conditions
`<  >  <=  >=  ==  !=` (also `≤ ≥ ≠`) compare two values and give `true` or
`false`. Numbers compare by value, exact ones exactly; quantities compare in
SI units and need the same dimension; matrices and booleans can only be equal
or not. `and`, `or` and `not (c)` combine them: arithmetic binds tighter than
a comparison, a comparison tighter than `and`, and `and` tighter than `or`.
`and` and `or` stop at the left operand when it decides.

`if (c, a, b)` is `a` when `c` is true, else `b`, and only the branch it takes
is evaluated, so a function can call itself and end:

```example
1/3 + 1/6 == 1/2
=> true
1 km > 999 m and not (2 > 3)
=> true
fact(n) = if (n <= 1, 1, n * fact(n - 1))
fact(10)
=> 3628800
```

A computation that takes more than two million steps ends with the error
`too_long` (try `fib(32)` with the usual double recursion), so a typing
mistake never keeps the editor busy. `3!=3` is "3 is not equal to 3": write
`3! == 6` for the factorial.

# Names and statements
Names are letters, digits and `_`, not starting with a digit. A name
that is assigned keeps its value in the lines below.

```example
a = 3
=> 3
2a + 1
=> 7
f(x) = x^2 + 1
f(a)
=> 10
```

# Exact and approximate results
A result that cannot be an exact fraction is approximate and shown with
`≈`: `sqrt(2)` is `≈ 1.41421356`. The constants `pi` and `e` are exact
names that evaluate to approximations.
