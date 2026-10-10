id: lists
title: Lists
summary: Lists of any length: range, list, item, indexing, item by item arithmetic.
keywords: list, range, len, item, append, concat, reverse, take, drop, vec, tolist, index, sequence, array
see: statistics, matrices, syntax, ex-algebra-lists

A list holds any number of values, up to 100000: numbers, quantities,
booleans, complex numbers, matrices. A list does not hold other lists. It is
different from a vector: `[1, 2, 3]` is a column vector (at most 64 numbers)
for matrix work, `range(1, 1000)` and `list(1, 2, 3)` are lists. The statistics
functions read both (see [statistics](statistics)).

# Making lists
```code
range (a, b [, step])   a, a + step, ... up to b
list (a, b, ...)        the list of the arguments; list () is empty
tolist (v)              the list of the numbers of a vector
vec (xs)                the column vector of a list of exact numbers
```

```example
range(1, 5)
=> [1, 2, 3, 4, 5]
range(10, 1, -3)
=> [10, 7, 4, 1]
list(1, 2/3, 2 > 1)
=> [1, 2/3, true]
len(range(1, 1000))
=> 1000
```

A long list is shown with its first eight and last two items and its length.

# Reading and changing
```code
len (xs)           the number of items
item (xs, i)       item i, counted from 1; a negative i counts from the end
xs[i]              the same, the bracket right after the name
append (xs, x)     the list with x added at the end
concat (xs, ys)    the items of xs, then those of ys
reverse (xs)       the other way round
take (xs, n)       the first n items
drop (xs, n)       all but the first n items
```

```example
xs = range(10, 50, 10)
=> [10, 20, 30, 40, 50]
xs[2]
=> 20
item(xs, -1)
=> 50
take(reverse(xs), 2)
=> [50, 40]
len(append(xs, 60))
=> 6
```

# Arithmetic
`+ - * / ^` work item by item: two lists of one length, or a list and a
single value that is used for every item. Comparing two lists with `==` and
`!=` compares them item by item.

```example
range(1, 4) * 2
=> [2, 4, 6, 8]
range(1, 3) + range(10, 30, 10)
=> [11, 22, 33]
-range(1, 3)
=> [-1, -2, -3]
sum(range(1, 100)) / len(range(1, 100))
=> 101/2
```

A list of exact numbers works with every function of
[statistics](statistics): `mean`, `median`, `sort`, `hist` and the others.
`prod(range(1, 10))` is the product of the items. Errors: `size` for two lists
of different lengths, `domain` for a position outside the list, `type` for
something that is not a list.
