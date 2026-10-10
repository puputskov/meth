id: matrices
title: Matrices and vectors
summary: Matrix literals, products, determinants and inverses.
keywords: matrix, vector, determinant, inverse, transpose, rank, rref, dot, cross
see: syntax

A matrix is a list of rows: `[[1,2],[3,4]]`. A list of numbers
`[1,2,3]` is a column vector. The usual operators work on them: `A*B`,
`A^T` for the transpose, `A^-1` for the inverse.

# Functions
`det inv transpose trace rank rref solve identity dot cross norm`.

```example
[[1,2],[3,4]] * [[0,1],[1,0]]
=> [[2, 1], [4, 3]]
det([[1,2],[3,4]])
=> -2
inv([[1,2],[3,4]])
=> [[-2, 1], [3/2, -1/2]]
```

Matrices are shown typeset under their line in the editor, and as real
matrices in the [export](export).
