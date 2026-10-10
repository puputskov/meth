# meth

An interactive math shell written in C. You write notes and math lines in a
session, and every math line shows its result as you type: exact arithmetic
with big integers and fractions, algebra, calculus, matrices, statistics,
units, boolean algebra, lambda calculus, projective geometric algebra and
step-checked proofs. It runs in the terminal and, built to WebAssembly, as a
notebook in the browser.

- C11, no dependencies beyond the C standard library and the OS API.
- Windows first; OS specific code is in `source/meth/platform/`.
- The web editor runs the same library compiled to WebAssembly (`web/`).

## Build

```
premake5 vs2022          # Visual Studio 2022 solution in build/
make test                # gcc or clang, builds into build/make/ and runs the tests
make test T=eval         # one test group (make groups lists them)
cmake -S . -B build/cmake && cmake --build build/cmake && ctest --test-dir build/cmake
bazel build //:meth && bazel test //:tests
./build.sh [meth|tests|keydemo|all] [debug|release] [run]     # build.bat on Windows
```

All of them build the same programs (`meth`, `tests`, `keydemo`) from the same
files, with warnings as errors.

The web editor: `build_web.bat` or `./build_web.sh` build `web/meth.wasm`
(needs clang and `wasm-ld`). Serve `web/` with any static server, for example
`python -m http.server` in that folder. `python tools/test_web.py` runs its
tests in a headless Edge or Chrome. Every push to `master` builds, tests and
publishes the editor as a GitHub Pages site (`.github/workflows/web.yml`).

## Use

```
meth                     # the editor; the first run starts the tutorial
meth --tutorial          # learn the tool by doing
meth eval "1/3 + 1/6"    # evaluate one expression (--format json for data)
meth docs                # the documentation
```

Inside the editor `:help` opens the documentation and `:tutorial` the lessons.

## Layout

| Folder | Contents |
| --- | --- |
| `source/`, `include/` | the program; the library is in `source/meth/` and `include/meth/` |
| `data/` | everything that is data: `docs/`, `examples/`, `snippets/`, `stdlib/`, `texts/`, `lessons/`, `config/` |
| `web/` | the browser editor: HTML, CSS, plain JavaScript, the `.wasm` modules |
| `tools/` | `gen_docs.py` (compiles `data/` into C), `stylecheck.py`, `test_web.py` |

After changing a file in `data/`, run `python tools/gen_docs.py`; the tests
fail when it was not run. `python tools/stylecheck.py` checks the code style
rules (see `CLAUDE.md`).

## Project notes

- `TODO.md`: open work.
- `DONE.md`: the design of what is built.
- `KNOWN_ISSUES.md`: known bugs.
- `CLAUDE.md`: conventions and the code style rules.
