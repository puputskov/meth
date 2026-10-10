id: boolean
title: Boolean algebra
summary: Expressions with and, or, not, truth tables and simplification.
keywords: boolean, logic, truth table, and, or, not, xor, implies, simplify, tautology
see: syntax, lambda

A line in a block of the boolean domain (`{ACTION_BLOCK_NEW}`, then choose
`boolean`) is a boolean expression. Names are variables, `true` and `false`
(also `1`, `0`, `⊤` and `⊥`) are the constants.

```code
not    ~ ! ¬            binds tightest
and    & && ∧ and
xor    ^ ⊕ xor
or     | || ∨ or
imply  -> => →          to the right
equal  <-> <=> == ↔     binds loosest
```

# The result
A line without free names gives `true` or `false`. With names it gives the
expression in its smallest sum of products form (the Quine-McCluskey
method), or the expression as it was written when that is shorter. A
tautology is `true` and a contradiction `false`, so two expressions are
equal when `a == b` is `true`:

```code
~(a & b) == (~a | ~b)
a -> b
```

The first line gives `true`, the second `¬a ∨ b`.

# Truth tables
`table` before an expression shows its truth table under the line, with
the names in alphabetical order (at most 6 names). `{ACTION_STEPS}` on a line shows
its truth table too, and `meth steps --domain boolean EXPR` prints it.

# While you type
The result appears dimmed after the expression as soon as it is complete.
The operators (`and`, `∧`, `&`, ...) have the colour of the class `logic`.

# Definitions
`p = a & b` defines a name. Later lines replace `p` with its definition.
A definition keeps its own names: `a = true` below it does not change `p`.
At most 10 names may occur in a line.

# Errors
`bool_unexpected_character`, `bool_expected_operand`,
`bool_expected_operator`, `bool_unbalanced_parenthesis`,
`bool_too_many_names`, `bool_table_too_big` and `bool_bad_definition`.
