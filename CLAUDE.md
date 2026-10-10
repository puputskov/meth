# meth

Interactive math shell written in C. `TODO.md` is the list of open work (start there), `DONE.md` the design of what is built: do not read it
whole, find the section with `grep -n '^### ' DONE.md` and read only that one. `KNOWN_ISSUES.md` lists the known bugs.

## Project

- Language: C11, no dependencies beyond the C standard library and the OS API.
- Build: premake5 for VS2022 (`premake5 vs2022`), generated files and all outputs go to `build/`.
- Sources in `source/`, headers in `include/`. Everything that is data and not code is in `data/`: `docs/` (pages), `examples/`, `snippets/`, `stdlib/`,
  `texts/`, `lessons/`, `config/` (themes). `tools/gen_docs.py` compiles these into `source/meth/docs/*_data.c` and `texts/catalog_data.c`: run it after
  changing a file in `data/` (the tests fail when it was not run). Library code lives in `source/meth/` and `include/meth/`, included as `<meth/x.h>`.
- Windows first; all OS specific code goes in the `platform` module so other systems can be added later.
  OS specific files go in a per-OS subfolder, e.g. `source/meth/platform/win32/key.c` and `include/meth/platform/win32/key.h`
  (a Linux port would add `platform/linux/`), never as `key_win32.c`. Portable platform code stays directly in `platform/`.

## Build and test on Linux

`make test` builds with gcc into `build/make/` and runs the tests, `make test T=eval` only the group `eval` (`make groups` lists them). Every group
prints its name and its check count, so the last name on the screen is the group that hangs.

## Other ways to build

All of them build the same three programs (`meth`, `tests`, `keydemo`) from the same files, warnings as errors, and none of them needs the others:
`premake5 vs2022` (the Visual Studio solution), `make test` (gcc or clang), `cmake -S . -B build/cmake && cmake --build build/cmake && ctest --test-dir build/cmake`
(`-DML_SANITIZE=ON` for the sanitizers), `bazel build //:meth` / `bazel test //:tests`, and the scripts that call the compiler directly,
`./build.sh [meth|tests|keydemo|all] [debug|release] [run]` and `build.bat` (same arguments, finds Visual Studio with vswhere) into `build/direct/`.
`build_web.bat` builds the WebAssembly module of the web editor (`web/meth.wasm`) with clang and `wasm-ld`; see `TODO.md` section 9.
`python tools/test_web.py` runs `web/tests/cases.txt` in the module (headless Edge or Chrome) and natively and compares the results, then the checks of the
page API (`web/js/meth.js`) and the C test groups that need no files inside the module (`web/tests.wasm`).
A new source file is found by all of them without a change (they glob the folders); a new OS specific folder needs a change in each.

## Warnings

Warnings are errors: `warnings "Extra"` with `fatalwarnings { "All" }` in `premake5.lua` (`/WX`) and `-Wall -Wextra -Werror` in the `Makefile`. Fix the cause of a warning,
never silence it, and check `make` output for warnings before a commit.

## Memory

No `malloc`, `realloc`, `calloc` or `free` outside `core/heap.c`. Scratch work uses a mark of `ml_arena_app ()` and rewinds it; things that grow use the
typed containers (`ML_VEC_DEFINE`, `MlStr`) or `ml_heap_alloc`. For leak checks build with `-DML_HEAP_SYSTEM` and the sanitizers (see `Makefile`).

## Git

- Always commit directly to `master`. Never create or use other branches, and never open pull requests.

## Code style rules (always)

- Indent with TABS only. Spaces are only for alignment.
- The opening brace `{` is always on its own new line (Allman), for functions and every if/else/while/for/switch/struct/enum block.
  `} else {` is written as `}`, `else`, `{` on separate lines. Brace initializer lists stay on one line when short.
- Always put an empty line after a closing `}` of a block, unless the next line is another `}` or a preprocessor line. This includes `else` and `else if`: `}`, empty line, `else`.
- A space goes between a function name and its opening parenthesis, in declarations, definitions and calls (also macros and `sizeof`):
  `ml_str_clear (MlStr *s)`, `ml_str_clear (&s)`. Exception: the parameter list in a `#define NAME(args)` must stay attached.
- A space also goes before the `[` of an array subscript or array declarator: `e.bytes [0]`, `s->data [idx]`, `uint8_t buf [4]`.
  (Designated index entries `[ML_KEY_NONE] = ...` start the line, so there is nothing before the bracket.)
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
  Sizes, counts and indices are `uint64_t`, not `size_t` (64 bit targets only, enforced in `meth/core/types.h`; the planned wasm32 web build is the one exception, see `TODO.md` section 9). `char` stays for text bytes,
  and `int main` stays as the C standard requires.
- Lines are at most 80 columns (a tab counts as 4). Break a long call so that each argument has its own line and the closing
  parenthesis is on a line of its own; wrap comments at 80 too. Long string literals are split into adjacent literals, one per line.
  Parameters of declarations are aligned: type column, then name column (the star belongs to the name).
- A boolean expression with four or more operands, or one that does not fit, is written vertically in parentheses, one operand per
  line, the operator leading, operands aligned after the operator:
  ```c
  ok = (
  	   ok
  	&& !(n == 2      && c <  0x00080)
  	&& !(c >= 0xD800 && c <= 0x0DFFF)
  	&& c <= 0x10FFFF
  );
  ```
  Inside such blocks the comparison operators and numbers are aligned where that is possible (hex numbers are zero padded to one width).
- In consecutive `CHECK (...)` lines whose first argument is a call, the call names are padded so that their opening parentheses line up
  (a leading `!` counts as part of the name): `CHECK (ml_slice_equal  (...))` / `CHECK (!ml_slice_equal (...))`.
- Use `ml_count (array)` instead of `sizeof array / sizeof array [0]`.
- Never use the `?:` operator. Use the `ml_choose (cond, a, b)` macro from `meth/core/types.h` (same lazy evaluation) and write its arguments one
  per line, with the closing parenthesis on its own line. When it is nested in another call, that call's arguments go one per line too:
  ```c
  return ml_choose (
  	enc.size > 0,
  	ml_lineedit_insert (e, ml_slice (enc.bytes, enc.size)),
  	ML_ERR_UTF8
  );
  ```
- Struct initializers always use named (designated) entries and are written one entry per line. The opening brace stays on the line of the
  `=` (`MlLine line = {`, nested `.block = {` too, the one exception to the Allman rule) and the closing brace is on a line of its own
  (`};`, `},`), with the `=` of the entries aligned:
  ```c
  MlLine line = {
  	.kind   = ML_LINE_NONE,
  	.domain = ML_DOMAIN_ALGEBRA
  };
  ```
  Rows of a data table (`{ .lo = 0x0300, .hi = 0x036F },`) stay on one line each.
- Tables indexed by an enum use designated index entries, one per line, with the `=` aligned: `[ML_DOMAIN_ALGEBRA]   = "algebra",`.
  Do not use numeric designators (`[1] = ...`): the index names nothing, so compute the value instead. Plain value lists with
  no natural index (e.g. test data) stay positional.
- Declare loop variables in the `for` statement: `for (uint64_t i = 0; i < n; i++)`, not at the top of the function.
- No early exits. Every function has a single `return` at its end. Avoid `break`, `continue` and `goto`. The one use of `break` is to end a case of
  a `switch`.
  Chain conditions with a result variable (`if (r == ML_OK) { ... }`) instead of returning early.
- A choice among several constant values of one enum or integer is a `switch`, not a chain of `else if`. Allman braces; the `case` labels are
  indented one level inside the `switch`, one label per line, and the statements of a case are in braces that stand at the indent of the label,
  the statements one level deeper, the closing brace and the `break` together: `} break;`. A blank line between the cases, always a `default`,
  the value is assigned to the result variable and not returned from the case:
  ```c
  switch (kind)
  {
  	case ML_NODE_ADD:
  	case ML_NODE_SUB:
  	{
  		r = sum_value (kind, a, b);
  	} break;

  	case ML_NODE_MUL:
  	{
  		r = ml_value_mul (arena, a, b);
  	} break;

  	default:
  	{
  		r = error_at (ML_EVAL_TYPE);
  	} break;
  }
  ```
- If cleanup is needed, do it in one cleanup stage at the end of the function.
- No recursion, anywhere. Walk trees and other nested data with a loop and an explicit stack (a typed vector on the heap),
  or with the tree's own links (parent, first child, next sibling) when it has them, never with a function that calls
  itself, directly or through other functions. A recursive descent parser becomes a state machine: each grammar function is
  a mode, and what a call would keep on the C stack is a frame on the explicit stack. The C stack use of the program must not
  depend on its input. Nesting limits (depth of parentheses, tree height, calls) stay as limits on the heap stacks.
- Avoid pointers when possible: pass and return small structs by value (e.g. `MlSlice`, result structs), use indices instead of
  pointers, and use typed containers instead of `void *`. Pointers are fine for arena allocations and for the "self" of a mutable
  container.

## Formatting tool

`.clang-format` holds the layout rules clang-format can express (tabs, Allman, `foo (`, `e [0]`, 80 columns, one argument per line, leading
operators, aligned `=` and declarations). Run it on the files you write instead of laying them out by hand:
`"W:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/bin/clang-format.exe" -i file.c`.
It cannot do these, so check them by hand afterwards: the empty line after every `}` (also before `else`), initializer lists with one entry per line (it
writes `= {` too, which is the rule now), vertical boolean blocks in `if (` with the
operands aligned after the operator, the star in the name column of aligned declarations, `ml_choose`, single return, no recursion.
Do not run it over a whole existing file without looking at the diff: about one line in seven of the current code would change.

`python tools/stylecheck.py` checks the rules a tool can check (tabs, brace placement, empty line after `}`, 80 columns, no `?:`, spaces before `(`
and `[`, fixed-width types, one `return` at the end, no `break`/`continue`/`goto` (except `} break;` ending a case), no recursion through the call graph, no numeric designators).
`--fix` adds the empty lines after `}` and turns leading spaces into tabs. Run it before a commit; the aim is 0 findings.

