id: plots
title: Plots
summary: Histograms, scatter plots and line plots drawn in the side panel.
keywords: plot, scatter, hist, histogram, chart, graph, braille, visualization
see: statistics, editor, commands, ex-stats-plot-your-data

A plot is a value like a number or a matrix. The line shows only a one line
summary, so the editor stays clean; the picture is drawn in the panel at the
right of the editor when you switch it on.

# Making a plot
```code
hist (v, k)       a histogram of the list v in k bins
scatter (x, y)    the points (x [i], y [i])
plot (x, y)       the same points, each joined to the next by a line
```

```example
hist ([1, 2, 2, 3, 3, 3], 3)
=> histogram of 6 values in 3 bins
scatter ([1, 2, 3], [2, 4, 6])
=> scatter plot of 3 points
plot ([1, 2], [5, 7])
=> line plot of 2 points
```

The lists are vectors of the same length, as for the statistics functions
(see [statistics](statistics)). A plot can be given a name,
`spread = hist (data, 5)`, and shown again later by writing the name.

# Showing it
`{ACTION_PLOT}` on the line of a plot draws it in the panel, and the same key
again hides it. The command is `:plot` (`:plot on`, `:plot off`). Any number of plots can be on;
they are drawn one above the other, with a star on the header of the plot of
the line of the cursor, and what does not fit ends in "+N more". When the
watch window is on too, the plots take the upper two thirds of the panel.
Which plots are on is kept with the session in the settings file, like the
folds, and not in the `.mlog` file. A terminal under 70 columns has no room
for the panel.

A histogram is drawn with block characters, eight levels to a row; scatter
and line plots with braille dots, two by four to a character cell. The
axes show the range of the data.
