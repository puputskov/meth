id: themes
title: Themes and settings
summary: Colours, the built in themes, your own theme files and the settings file.
keywords: theme, colors, colours, dark, light, contrast, mono, settings, settings.mlcfg, background, highlight, no_color
see: commands

Math lines are drawn with syntax highlighting: numbers, names, functions,
constants, units, operators, brackets, keywords and errors each have a
colour, and so have the gutter, the results and the hints.

# Choosing a theme
`:theme dark`, `:theme light`, `:theme contrast` or `:theme mono`; Tab
completes the names, also those of your own theme files. `:colors 24bit`,
`256`, `16` or `off` sets how many colours the terminal is asked to show;
it is found from `COLORTERM`, `TERM` and `NO_COLOR` by default. The choice is
kept in the settings file.

The `light` and `contrast` themes paint their own background; `dark` and
`mono` keep the colours of the terminal.

# Your own theme
A theme is a text file `name.theme` in the settings folder. Each line is
`class = style`:

```code
background = #ffffff
paint      = on
number     = #098658
error      = #cd3131 underline
gutter     = #6e7781
```
A style is a colour `#rrggbb`, a background `on #rrggbb` and the words
`bold`, `dim`, `underline`, `reverse`; `none` is no style. Classes that are
left out keep the style of the dark theme. `matlog theme dark` prints a
built in theme in this form to start from; `:theme reload` reads the file
again, and it is read again by itself when you save it.

The classes are text, number, name, function, constant, unit, operator,
paren, keyword, error, match, unmatched, gutter, result, result-error,
ghost, hint-key, note and note-code.

# The settings file
`settings.mlcfg` in the settings folder (`%APPDATA%\matlog` on Windows,
`~/.config/matlog` on Linux; `MATLOG_CONFIG` names another folder). It is
a plain text file with `[sections]` and `key = value` lines; matlog keeps
what it does not know when it writes. `[display]` holds `theme` and
`colors`; `[folds]` holds the folded blocks of each session file.
