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

## `primes(n)` is very slow and runs out of memory (Linux, gcc)

- **Seen:** `matlog eval "len(primes(50))"` takes about 7 seconds, `primes(200)`
  runs out of memory; the `stdlib` test group fails on its line
  `len(primes(1000))` ("out of memory") and uses gigabytes before it does.
  `isprime(97)` alone takes 30 ms and `len(range(2,50))` 29 ms, most of it
  system time (pages touched), so the start of an evaluation looks costly too.
- **Suspected:** `filter(isprime, range(2, n))`, a user function passed by name
  as a value and called through the parameter, allocates in the evaluation
  arena for every call and never gives it back, or the arena block grows by
  a large factor on every call.
- **Not tried:** the same line on Windows, a build with `-O2`, counting the
  arena bytes per call of `isprime`.
- **Code:** the function values of `source/matlog/math/eval.c`,
  `stdlib/numbers.mlog` (`primes`, `isprime`).
