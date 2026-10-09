id: lambda
title: Lambda calculus
summary: Terms, abstraction, application and step by step reduction.
keywords: lambda, calculus, beta, reduction, church, numeral, normal form, trace
see: boolean, syntax

A line in a block of the lambda domain is a term. The result is its normal
form, found by normal order reduction.

```code
x            a name
λx. t        an abstraction (also \x. t); λx y. t is λx. λy. t
t u          an application: f x y is (f x) y
3            the Church numeral λf x. f (f (f x))
```

The body of a λ goes as far to the right as it can, and an application
binds tighter than a λ. The result is written with as few parentheses as
possible, and a binder gets a prime (`y'`) when its name would capture a
free one:

```code
(λx y. x) y
```

gives `λy'. y`.

# Definitions
`name = term` defines a name for the lines below. A λ that binds the same
name hides the definition. A definition is kept as the term, not as its
normal form, so a term without one can be named:

```code
succ = λn f x. f (n f x)
plus = λm n f x. m f (n f x)
plus 2 3
```

The last line gives `λf x. f (f (f (f (f x))))`, the Church numeral 5.

# Steps
`trace` before a term shows every beta step under the line, up to 60.

# When there is no normal form
After 5000 steps the line is the error `lambda_no_normal_form`, and when
the term grows past two million nodes `lambda_too_big`. A definition is
still made.

# Errors
`lambda_unexpected_character`, `lambda_expected_term`,
`lambda_expected_parameter`, `lambda_unbalanced_parenthesis`,
`lambda_number_too_big`, `lambda_no_normal_form`, `lambda_too_big` and
`lambda_bad_definition`.
