# matlog

Interactive math shell written in C. See `plan.md` for the design.

## Project

- Language: C11, no dependencies beyond the C standard library and the OS API.
- Build: premake5 for VS2022 (`premake5 vs2022`), generated files and all outputs go to `build/`.
- Sources in `source/`, headers in `include/`. Library code lives in `source/matlog/` and `include/matlog/`, included as `<matlog/x.h>`.
- Windows first; all OS specific code goes in the `platform` module so other systems can be added later.

## Code style rules (always)

- Indent with TABS only. Spaces are only for alignment.
- The opening brace `{` is always on its own new line (Allman), for functions and every if/else/while/for/switch/struct/enum block.
  `} else {` is written as `}`, `else`, `{` on separate lines. Brace initializer lists stay on one line when short.
- Always put an empty line after a closing `}` of a block, unless the next line is another `}` or a preprocessor line. This includes `else` and `else if`: `}`, empty line, `else`.
- A space goes between a function name and its opening parenthesis, in declarations, definitions and calls (also macros and `sizeof`):
  `ml_str_clear (MlStr *s)`, `ml_str_clear (&s)`. Exception: the parameter list in a `#define NAME(args)` must stay attached.
- Align the `=` signs of consecutive assignment and declaration lines (same indent, no blank line between) with spaces:
  `uint64_t need    = size + align;` / `MlArenaBlock *nb = block_new (cap);`. Compound assignments (`+=`) and inline `{ .a = 1, .b = 2 }` are not aligned.
- Declarations in a block (consecutive lines, same indent) are aligned in columns: type, then variable name, then `=`, then trailing comments:
  ```c
  MlResult r = ML_OK;
  va_list  ap;
  int32_t  n;
  ```
  A pointer star belongs to the name (`char     *data;`). Aligning always uses spaces, never tabs. This also applies to struct members and `#define` values.
- Use fixed-width types: `int32_t`, `uint32_t`, `uint8_t`, `int64_t`... instead of `int`, `unsigned int`, `unsigned char`, `long long`.
  Sizes, counts and indices are `uint64_t`, not `size_t` (64 bit targets only, enforced in `matlog/core/types.h`). `char` stays for text bytes,
  and `int main` stays as the C standard requires.
- Struct initializers always use named (designated) entries: `{ .width = 2, .height = 5, .name = "foo" }`, never `{ 2, 5, "foo" }`.
  Plain arrays of scalars or strings (e.g. a table of names) are the only exception.
- No early exits. Every function has a single `return` at its end. Avoid `break`, `continue` and `goto`.
  Chain conditions with a result variable (`if (r == ML_OK) { ... }`) instead of returning early.
- If cleanup is needed, do it in one cleanup stage at the end of the function.
- Avoid pointers when possible: pass and return small structs by value (e.g. `MlSlice`, result structs), use indices instead of
  pointers, and use typed containers instead of `void *`. Pointers are fine for arena allocations and for the "self" of a mutable
  container.
