# matlog: what is open

Work from this list. The design of what already exists is in `DONE.md` (read only the section you touch). Rules for the code: `CLAUDE.md`. Bugs
that are known: `KNOWN_ISSUES.md`. How to build and test on Linux: `make test T=group` (see `CLAUDE.md`).

When a task is finished: delete it here, and write what was built into its section of `DONE.md`.

## 1. Next candidates, in a sensible order

1. **Texts in files, the rest** (4.24): the texts inside the export writers and the docs build (names of the page kinds, `matlog docs` output), and the
   placeholders of the website build (waits for the website build).
2. **Faster evaluation, still**: `MlValue` (280 bytes) is passed and returned by value at every step (`eval_with`, `finish`, `step_call`) and `MlBinding` (about
   450 bytes) is copied by `call_target` and `MlScope.func`; the zero fill of the limbs of small big numbers (`limbs_new`, 260 thousand calls for `primes (300)`)
   and `ml_value_none` (a 280 byte clear, 90 thousand calls) are the next costs in the profile. Writing results in place, a smaller value (the kinds that
   exclude each other in a union, after checking that nothing reads a field of another kind) or passing values by index into the value stack would help.
3. **Export, png and pdf** (needs the layout engine), the export panel; **proofs** (section 4); **PGA** (section 5).

## 2. Not built, per section of DONE.md

Short list of what each built section says it still lacks. The details are in the section.

- **3 Editing**: double click for words; the key table for sequences of keys.
- **4.4 Notes / 4.2 ghost / 4.3 completion**: unit names in the math completion; the highlight classes of the other domains.
- **4.6 Plots**: `plot (f, a, b)`, the view of a matrix transformation (basis vectors and a shape), a fitted line over a scatter plot, axis titles, theme
  classes for the plot, a panel under the editor for narrow terminals.
- **Physics domain** (built): load of an old file with units in `m:algebra` lines says nothing once (only the hint on each line); comparisons and lists of quantities
  are not restricted to physics lines; the units in the completion of the editor (the math completion is not wired into the editor yet).
- **4.9 Tutorial**: lessons for PGA, stage 2 lessons for export, lessons for files (`:w`, `:open`).
- **4.10 Themes**: the settings file entry for the theme, the OSC 11 background query, highlight classes of the other domains.
- **4.11 Export**: png and pdf (layout engine, `export/layout`), the export panel and its preview, options other than `dark steps includes` and page size,
  options remembered per session, the visual selection as the range, typeset PGA lines and includes.
- **4.14 Documentation**: the website build (`matlog docs build site/`), the WebAssembly build, `matlog docs search|show|list` if missing.
- **4.15 Settings**: sequences of keys; bindings for the help viewer and the command line; settings without a feature yet (default domain, width of
  the result column, live Markdown preview, undo history size, reopen the last file, confirm before `:q!`); the website build expanding key
  placeholders; the status line and messages naming a key; `F6` of the tutorial as an action.
- **4.16 Examples**: Tab through snippet placeholders, snippets in the math completion, `matlog snippets`, an example opened as a read only session,
  examples that need later features (images, PGA motors, full inverse kinematics with approximate matrices).
- **4.17 Complex numbers**: complex entries in matrices, complex roots from `solve`, `slerp`, `normalize`, quaternion from and to axis angle and
  rotation matrix, LaTeX and MathML export of the new values, steps for the product, completion of the units.
- **4.21 Watch window**: moving into the panel, Enter to expand, `d` to jump to the definition, sorting and filtering, the theme classes
  `watch-name`, `watch-type`, `watch-value`, a panel under the editor for narrow terminals.
- **4.22 Statistics**: `load_csv`, a seeded random generator, `t` and `chi2` quantiles and tests, lists longer than 64 numbers, data with units.
- **4.24 Texts**, **4.25 Words**: see section 1.
- **9b Matrices**: nothing open except what 4.17 lists.
- **9c Symbolic**: limits of infinity minus infinity and series; integrals with a squared quadratic factor in the denominator, cubics and quartics
  without rational roots, trigonometric substitution; `solve` of systems of equations in the algebra domain, complex roots, cubic and quartic formulas,
  trigonometric equations; formulas of the symbolic functions do not see the locals of `let`.
- **9e Boolean and lambda**: export (done for both), syntax completion of the names defined above, equivalence proofs for the proof checker.

## 3. Open questions

Defaults chosen in DONE.md section 9.1 (approximate numbers, implicit multiplication, 0^0 as an error) can still be changed.

## 4. Proofs and their validation (4.12, not built)

A proof is written in a session, line by line, and the tool checks every step. A proof is made of ordinary lines in a new domain, `proof`, plus a few
statement lines, so it lives in the same `.mlog` file as the calculations, with notes between the steps, and it is exported like everything else.
- Statements: `theorem name: <claim>` (what is to be shown, with its hypotheses: `given x > 0, y > 0`), then the steps, then `qed`. A step is a claim
  followed by a justification: `x + x = 2x  by simplify`, `... by lemma name`, `... by rule distributive`, `... by hypothesis`, `... by step 3`.
  A line that starts with `=` or `...` continues the previous right hand side, so a chain of equalities reads like on paper
  (`= x² + 2x + 1  by expand`). Lemmas are earlier theorems of the session or of included files; a proof can use only what is above it.
- Checking: each step is checked against its justification by the engine, never taken on trust. The checker gives, per line, one of: valid, not valid
  (with the reason and what was expected), not checked (a justification the engine cannot decide, shown as a warning and counted), or an error in the
  syntax. A proof is complete when `qed` closes every goal with no unchecked step; the theorem is then available as a lemma below. `sorry` is allowed
  as a placeholder step and is reported, so unfinished proofs are visible and never counted as complete.
- Justifications the engine can decide, in order of work: arithmetic and algebraic equality (the two sides are simplified to a normal form and compared,
  exact rationals, so no rounding), a named rewrite rule from a table (the rule engine of section 4.1, with its steps shown on request: the user can see
  which rule connects two lines), a hypothesis, an earlier step or lemma, the definition of a name, and later: induction (base case and step as two
  goals, the induction hypothesis added to the context), cases (`cases on x >= 0`, one goal per case, covering checked), contradiction, and domain
  rules (matrix identities such as (AB)^T = B^T A^T by rewriting, boolean laws, lambda terms equal up to reduction). Inequalities are decided for
  linear and simple polynomial cases first. What the engine cannot decide stays `not checked`; the engine must never say valid when it is not sure.
- Soundness: the checker is a small core (the rules it trusts are a short, listed table, each rule tested against numeric spot checks and exact
  identities) and a larger layer of search on top that only proposes justifications; whatever the search finds is rechecked by the core. `:why` on a
  line shows the justification found, `:hint` suggests one for a step that has none.
- Counterexamples: a claim that is not valid is tried on small exact values and, when it fails, the failing values are shown (`x = 1: 2 ≠ 3`), which
  is also a way to test an idea before proving it. `:test <claim>` does only this.
- Interactive use: in the editor a proof line shows its status mark at the end of the row (a check mark, a cross with the reason, or a question mark),
  the same way results are shown, recomputed on every edit from the changed line down. Ghost mode shows what the step's right hand side simplifies to.
  The tutorial gets a lesson for it and the export writes proofs as `proof` environments (latex), lists (markdown) or structured MathML.
- Tests: a corpus of valid and of broken proofs (a wrong step at each position, a missing hypothesis, a circular lemma, a step that uses a theorem
  proved below it); every valid one must be accepted and every broken one rejected at the right line with the right reason.

## 5. PGA (milestone 7, not built)

- PGA is implemented generically over a signature `(p, q, r)` (basis blades as bitmasks, metric from the signature).
  `R(2,0,1)` and `R(3,0,1)` are presets, selectable in PGA mode (e.g. `:pga 2` / `:pga 3`). Same code serves both.

The domain tags `pga2` and `pga3` exist in sessions and files; their lines are only echoed. Everything else (multivectors, the geometric product,
motors, highlighting, steps, export, docs page, tutorial lesson, examples) is open.

## 6. Export of functions to C, C++ and Python (4.19, not built)


- A function or a block of definitions can be exported as source code: `:export-code FILE.c|.cpp|.py` or `matlog codegen FILE:LINE --lang c|cpp|python`.
  The expression tree of the function is the input; the output is a plain function (doubles, or fixed-size arrays for vectors and matrices) with the
  names of the parameters, a comment holding the original matlog line, and constants and helper functions it needs from the standard library.
- Targets: C11 (`double`, `<math.h>`), C++17 (`std::array`, `<cmath>`, optionally a small header only vector/matrix type), Python 3 (plain functions,
  optionally with NumPy). Exact rationals become `double` constants (with a note), integers stay integers where the type is known.
- Symbolic results are exported after simplification, so `diff` and `integrate` results become code (derivatives and Jacobians for the inverse kinematics
  example). A test compiles the output of the examples with the system compiler, or runs it with Python when present, and compares values.
- Functions that cannot be exported (symbols that stand for nothing, unsupported domains) give a clear error with a byte range.

## 7. Images and kernels (4.20, not built)


- **Load an image** (`img = load ("photo.png")`; PNG and PPM/PGM to start, read by the program itself without a dependency) as a value kind `image`
  (width, height, channels, float or 8 bit samples). Shown in the terminal as a small preview (half blocks and 24 bit colour, or braille in grayscale),
  with `size (img)`, `crop`, `resize`, `gray`, `channel`, and `save (img, "out.png")`.
- **Kernels** are small matrices applied by convolution: `conv (img, K)` with the matrices of the matrix domain (blur, sharpen, Sobel edges, Gaussian
  from a function), with the border rules named (`clamp`, `wrap`, `zero`). Also pointwise kernels, written as an expression of `p` (the pixel) and
  `x`, `y`: `map (img, p -> 255 - p)`. Steps show the sum for one chosen pixel, as the matrix product steps do.
- Kernels written as matlog functions can be exported to C, C++ and Python (section 4.19) as a loop over the pixels.
- Limits: images are held on the heap with a size cap (so ghost mode stays fast: ghost never runs a kernel on a large image, it shows the size
  only); the CLI gets `matlog eval` with image results written to a file. Needs PNG decoding (inflate) and encoding (the export module needs it for
  section 4.11 anyway, so they are shared).

## 8. Fuzzer (4.23, not built)


- A fuzzer that feeds the program random and mutated input to find crashes, hangs, memory errors and broken invariants. Writing the first examples
  already found a memory bug (a symbolic value read after the arena of its line was reused), the kind of fault a fuzzer finds without help.
- Targets, each a function from bytes to a result with nothing else to set up: the lexer and the parser of the algebra domain, the evaluator and the
  symbolic engine (`simplify`, `diff`, `integrate`, `limit`, `solve`), the boolean and lambda engines, the units, the `.mlog` and `.theme` and
  `settings.mlcfg` readers, the key and command parsers, the docs page parser, the export writers, and the editor itself fed with random key
  sequences (the editor must never crash and must keep its invariants: cursor in range, the session valid UTF-8, undo returns to the start).
- Inputs: a corpus of the test inputs, the examples (section 4.16) and the docs; mutation (flip, insert, delete, splice, repeat) and generation from
  the grammar for the expression parsers. A seeded random generator in the tool itself so a run is reproducible: `tests/fuzz` takes `--seed N
  --runs N --target NAME`, runs in the normal test build in a short fixed form (a few thousand cases) and for as long as wanted by hand. A failing
  input is written to a file, shrunk by removing pieces while it still fails, and kept in `tests/corpus/` as a regression test.
- Checks beyond "does not crash": an evaluator result printed and parsed again gives the same value, `simplify` is idempotent, a symbolic result agrees
  with the numeric value at sample points, the exact and approximate paths agree where both exist, memory use and time stay within limits (nothing
  recurses, so the C stack must not depend on the input), and every allocation failure path is tried by an allocator that fails on the n-th call.
- Built with the compilers' sanitizers (address, undefined behaviour) where the platform has them; libFuzzer or AFL entry points are a thin wrapper over
  the same target functions, but the program needs no library: the built in driver is enough.
