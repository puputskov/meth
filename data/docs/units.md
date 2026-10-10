id: units
title: Units
summary: Quantities with units, conversions, SI prefixes and plural names.
keywords: unit, quantity, conversion, to, meter, second, hour, day, week, month, year, ampere hour, prefix
see: syntax

Units belong to the **physics domain**, the algebra domain with unit names,
quantities and `to` switched on. In the algebra domain a name like `h`, `m`,
`s`, `g` or `bar` is a variable and `3 m` is the product of 3 and `m`, as
everyone expects; in the physics domain they are the hour, the metre, the
second, the gram and the bar. Switch with `:mode physics` (or {ACTION_DOMAIN_NEXT} until the
label says `phy>`) and start a block. The operators, variables, functions,
lists and matrices are the same, and variables and functions flow between the
two domains as they do between lines of one domain.

A number followed by a unit is a quantity. Quantities add and multiply
with their dimensions checked, and `a to b` converts. A function written in
a physics line is evaluated with units wherever it is called from.

A line of the algebra domain that fails only because it uses units says so
(`units belong to the physics domain`), and `:fix units` makes those lines
physics lines. In a file a physics line is `m:physics 3 ft to m`.

```example physics
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

```example physics
1 year to days
=> 365 days
5 mAh to C
=> 18 C
```
