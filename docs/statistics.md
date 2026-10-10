id: statistics
title: Statistics
summary: Means, spread, correlation, line and polynomial fits, distributions.
keywords: statistics, mean, median, mode, variance, std, standard deviation, quantile, correlation, regression, linfit, polyfit, histogram, binomial, normal, poisson, zscore
see: functions, matrices, ex-stats-describe-data

Data is a list of numbers, written as a vector `[2, 4, 4, 5]` (a column) or
a row `[[2, 4, 4, 5]]`, or made by `range(1, 10)`. A list has at most 64
numbers. The results are exact fractions while the data is exact.

# One list
```code
sum count mean median mode       mode is the smallest of the most frequent
var std                          sample: divided by n - 1
pvar pstd                        population: divided by n
min max                          of a list too: min ([3, 1, 2])
quantile (v, p)                  p from 0 to 1, linear between the values
iqr (v)                          quantile 3/4 less quantile 1/4
skew kurt                        skewness (approximate), excess kurtosis
cumsum sort                      running sums, the data in order
histogram (v, k)                 how many values are in each of k bins
range (a, b [, step])            a, a + step, ... up to b
```

```example
mean([2, 4, 4, 4, 5, 5, 7, 9])
=> 5
var([2, 4, 4, 4, 5, 5, 7, 9])
=> 32/7
pstd([2, 4, 4, 4, 5, 5, 7, 9])
=> 2
median([3, 1, 4, 1, 5])
=> 3
```

A standard deviation is exact when the variance is a perfect square, else
an approximation. Too little data is the error `domain` (the variance of one
number), a spread of zero is `div_zero`.

# Two lists
```code
cov (x, y)        sample covariance
corr (x, y)       the correlation coefficient
rsq (x, y)        r squared, exact
linfit (x, y)     [a, b] so that y is about a + b x
polyfit (x, y, n) [c0, ..., cn] of the best polynomial of degree n
lstsq (A, b)      the x with A x closest to b
```

```example
linfit([1, 2, 3, 4], [2, 4, 5, 9])
=> [[-1/2], [11/5]]
corr([1, 2, 3], [3, 2, 1])
=> -1
```

# Counting and distributions
```code
binomial (n, k)  perm (n, k)      choose and arrange, exact
binomial_pdf (n, k, p)            exact when p is exact
poisson_pdf (k, rate)
normal_pdf normal_cdf (x [, mu, sigma])
normal_inv (p [, mu, sigma])      the x with normal_cdf (x) = p
zscore (x, mu, sigma)             (x - mu)/sigma
```

```example
binomial(10, 3)
=> 120
binomial_pdf(10, 3, 1/2)
=> 15/128
```

The normal functions are approximations (the sign says so). See the examples
[describing data](ex-stats-describe-data), [fitting a line](ex-stats-fit-a-line)
and [coins and the bell curve](ex-stats-coin-and-bell-curve).
