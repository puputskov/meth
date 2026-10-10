# Known issues

## `alg>` label keeps the old cell colour after `:theme` (Windows Terminal)

- **Seen:** after switching from a theme that does not paint a background
  (`dark`) to one that does (`light`, `contrast`) with `:theme light`, the cells
  behind the `alg>` gutter label keep their old colour. The rest of the screen
  is right. A restart of the program fixes it.
- **Where:** Windows Terminal, 24 bit colour.
- **What was checked:**
  - The bytes sent for the frame after the switch are the same as for a fresh
    start: the base colours, a full clear (`ESC[2J`) with the new background,
    then every row with the base colours after each reset. No row leaves a text
    attribute switched on.
  - Faint text (`SGR 2`) was suspected and is no longer used by the painting
    themes. It did not change anything.
  - Printing the same bytes (`ESC[38;2;31;31;31;48;2;255;255;255m ESC[2m 1 alg>
    ESC[0m`) from PowerShell shows white cells, so the terminal can draw them.
- **Not tried:** the same flow in another terminal (the older console window,
  a POSIX terminal), a trace of the bytes the running program writes (for
  example through a pseudo console), and whether the label row is drawn
  while the cursor is on it.
- **Code:** `ml_screen_render` and `paint_row` in `source/matlog/ui/screen.c`.

## `simplify` of a printed result can differ from the result

- **Seen:** found by the fuzzer (`source/tests/test_fuzz.c`). The result of
  `simplify` is printed, parsed and simplified again, and the second text is
  not always the first:
  - a sign in the denominator: `simplify ((x^2 - 1) / (x/- 1))` gives
    `-(x² - 1)/x`, and simplifying that text gives `(-x² + 1)/x`.
  - `simplify (51 / x + .1 / (x + 1))` gives `1/(10(x + 1)) + 51/x`, and
    simplifying that text gives `1/(10x + 10) + 51/x`.
- **Not the cause:** a second `ml_sym_simplify` pass over the tree changes
  nothing, so the difference is between the tree that is built from the
  input and the one built from the printed text (a negative sign above a
  quotient, a product with a sum in a denominator).
- **Code:** `ml_sym_product` and `ml_sym_sum` in `source/matlog/sym/simplify.c`,
  and the printer in `source/matlog/sym/format.c`.
