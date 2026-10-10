id: units
title: Units
summary: Quantities with units, conversions, SI prefixes and plural names.
keywords: unit, quantity, conversion, to, meter, second, hour, day, week, month, year, ampere hour, prefix
see: syntax

A number followed by a unit is a quantity. Quantities add and multiply
with their dimensions checked, and `a to b` converts.

```example
5 km + 300 m
=> 5300 m
3 ft to m
=> 1143/1250 m
2 hours to min
=> 120 min
```

# Names
Units have a symbol (`km`) and a long name (`kilometer`); the SI prefixes
work on both, and a long name may be plural (`3 days`). Other time units are
the `week`, the `month` (a twelfth of a year) and the `year` of 365 days.
Charge can be written in ampere hours: `5 mAh to C` is `18 C`.

A unit name that is also a variable means the variable: names are looked up
as variables first.

`dim(x)` gives the dimension of a quantity as one SI unit.

```example
1 year to days
=> 365 days
5 mAh to C
=> 18 C
```
