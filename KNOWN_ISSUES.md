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
- **Code:** `ml_screen_render` and `paint_row` in `source/meth/ui/screen.c`.
