# matlog

Matlog is an interactive math shell for quick calculation, beautiful presentation of results, and learning.
It is written in C (C11) with no external dependencies, built with premake5 for VS2022.

## 1. Goals

- Do math quickly in a terminal-like interactive environment.
- Present results beautifully (typeset-style output, plots, transformation visualizations).
- Show step-by-step solutions so the user can learn while working.
- Support symbolic work: simplification, derivatives, integrals.
- Persist everything to files so experiments can be resumed and others opened.
- Be a single self-contained toolkit: no third party libraries.

## 2. Domains

Each domain is a mode (see section 3).

| Domain | Scope |
|---|---|
| Algebra | expressions, simplification, derivatives, integrals, limits, **matrices and vectors** (arithmetic, inverse, determinant, decompositions, transformations, dot and cross products) and **equations** (`solve`, systems of equations: linear first, nonlinear later) |
| Lambda calculus | parsing, reduction, step by step beta reduction |
| Projective geometric algebra (PGA) | multivectors, geometric products, motors; both 2D `R(2,0,1)` and 3D `R(3,0,1)` |
| Boolean algebra | expressions, truth tables, simplification |
| Units | SI units and conversions, dimensional analysis (cross-cutting, usable in any mode) |
| Complex numbers and quaternions | arithmetic, polar form, rotations (section 4.17) |
| Statistics | descriptive statistics, regression, distributions (section 4.22) |
| Images | load, convolution kernels, pointwise maps (section 4.20) |

### 2.1 Domain details
- Integration scope, in order: polynomials and the table integrals (powers, `exp`, `ln`, trig), linearity and constant factors,
  then substitution and integration by parts, then rational functions (partial fractions). A general Risch-style algorithm is out of scope.
- PGA is implemented generically over a signature `(p, q, r)` (basis blades as bitmasks, metric from the signature).
  `R(2,0,1)` and `R(3,0,1)` are presets, selectable in PGA mode (e.g. `:pga 2` / `:pga 3`). Same code serves both.

## 3. Editing: no modes, and the domain of a block

Two independent layers.

### 3.0 Free form editing, no modes
The tool has no modes. It is edited like a normal text editor: the document is a list of lines, there is always a text cursor in one of them, and
typing edits that line in place. There is no key to start or to leave editing. The keys, all done:
- **Text:** Left, Right, Home, End move in the line; Up and Down go to the neighbouring line and keep the column; Left at the start and Right at the end
  cross to the neighbour; Backspace at the start joins the line with the previous one, Delete at the end joins the next one in; Enter splits the line at
  the cursor; `Shift` with Left, Right, Home, End selects text, Backspace, Delete or typing replace the selection. A line is written into the session when
  the cursor leaves it and on every command, so there is nothing to commit by hand. `Esc` in a math block writes the line and leaves the block: a new
  note starts below it (an empty new line of the block becomes the note); in a note it only writes the line.
- **Selection, as in other editors:** `Shift` with Left, Right, Up, Down, Home, End selects text from the other end, also across lines (`Ctrl` too
  moves by words); `Ctrl+A` selects all, `Ctrl+L` the cursor line with its line break. Backspace, Delete, Enter or typing replace the selection (the
  lines it touches become one), a plain Left or Right goes to its start or end, `Ctrl+C`, `Ctrl+X`, `Ctrl+V` copy, cut and paste: text is pasted at
  the cursor (a text of several lines splits the line), whole lines keep their kinds and domains and are pasted as lines. `Alt+Up` and `Alt+Down` move
  the lines the selection covers (a selection that ends at the start of a line leaves that line out). Without a selection `Ctrl+C`, `Ctrl+X` and
  `Ctrl+V` work on the line the cursor is in (paste goes below it) and `Alt+Up/Down` move that line. `Ctrl+P` with several lines selected starts the
  command line with their range. Selected text is drawn reversed, and a reversed column marks a line break that is inside the selection.
- **Lines:** `Ctrl+Home` and `Ctrl+End` go to the first and the last line, `Ctrl+Up` and `Ctrl+Down` to the previous and the next block.
- **Notes are the default:** the tool starts on a note and Enter goes on with a note. `Ctrl+Enter` or `Shift+Enter` (terminals report one or the other) starts a block of the
  domain used last at once; `Ctrl+B` opens the block choice first: the status line shows the domains (`-- BLOCK -- algebra  [matrix]  vector ...`), Left/Right/
  Up/Down/Tab move the choice, a letter jumps to the next domain starting with it, Enter starts the block (an empty line becomes its first line, else
  the line is split at the cursor), Esc cancels. Enter in a block goes on with the block; Enter on an empty line of a block ends it (a blank line
  closes the block and a note follows). `Ctrl+N` turns a line into a note and back. `Alt+Left` and `Alt+Right` change the domain of a math line, in a
  note the domain of the next blocks.
- **Folds:** `Alt+Enter` folds the block of the cursor line (or opens it again). A folded block is one row; it is not edited but selected, so the
  selection keys work on it as a whole, Enter or a typed character opens it and moves the cursor into it, and Down from the last block gives a new note.
- **Others:** `Ctrl+Z` undo, `Ctrl+Shift+Z` and `Ctrl+Y` redo, `F3` the steps of the line (shown under it), `Ctrl+S` save, `Ctrl+Q` quit, `Ctrl+P` the
  command line (`:q :w :open :mode :fold :unfold :foldall :unfoldall :steps :undo :redo` and the ranges `3,5d`, `%y`, `2,4m 10`; a selection starts it
  with its range filled in). The tool saves a file given on the command line after every change.
- **Parentheses:** with the cursor on or right after a bracket the bracket and its partner are drawn bold and underlined, a bracket without a partner
  bold and red; the pairs are counted per kind: `( )`, `[ ]`, `{ }`.
- **Pages and words:** `Page Up` and `Page Down` move the cursor a page of lines (the height of the text area), `Shift` selects; Ctrl+Left at the
  start of a line goes to the end of the one before, Ctrl+Right at the end to the start of the next, and Ctrl+Backspace and Ctrl+Delete join lines
  there. The key table of section 4.15 makes all of these keys changeable.
- **Mouse:** the terminal is asked for the left button, drags and the wheel (SGR reports on a VT terminal, console mouse events on Windows). A click puts the
  cursor in the text under it, also in another line; a click on a folded block selects the block; a drag selects text from where the button went down, across
  lines; the wheel moves the cursor three lines. A click in a result, a steps row or the status line does nothing. Wide notes with Markdown marks hidden are
  hit by their raw text, so the cursor can land a few columns off there. Double click for words is not built.

### 3.1 Replaced vim keys (kept for the history of the design)
The first versions were modal (Normal, Insert, Note and Visual states with `j k dd yy p v u ] [ z g :`). They were removed in favour of 3.0; the line
operations below are still the specification of what the lines can do, and they are single undo steps.
- **Normal:** navigate and manage lines. `j`/`k` move between lines, `dd` deletes a line, `yy` copies one, `Enter` re-edits one.
  Domain mode is switched here.
- **Insert (math):** type an expression in the current domain. Ghost mode and completion are active.
- **Note:** type free text. No parsing, no ghost mode, no math completion. Entered from normal mode with `n`.
- **Line operations** (all of them are single undo steps):
  - Counts: a number before a command repeats it or sets how many lines it takes: `5j`, `3dd`, `3yy`, `2p`, `12G` (go to line 12).
  - Delete, copy, paste: `dd` deletes (into the register), `yy` copies, `p` / `P` paste the whole register after / before the cursor line. The register holds
    whole lines with their kind and domain, any number of them.
  - Select: `v` starts a selection of whole lines (Visual state); `j k G gg 5j` extend it, `o` goes to the other end, `d` / `x` delete it, `y` copies it,
    `p` replaces it with the register, `Esc` or `v` leave. The selected lines are drawn reversed.
  - Move: `Alt+Up` / `Alt+Down` move the cursor line (or the whole selection) one line, the cursor follows.
  - Ranges on the command line: `:3,5d`, `:%y`, `:.,$d`, `:2,4m 10` (move to after line 10, `0` is the top), `:2,4t $` (copy to the end); from Visual `:` starts
    with the range of the selection filled in.
  - Undo and redo: `u` and `Ctrl+r` (or `:undo`, `:redo`) take back and redo the last user action, also a multi-line one: a line typed, an edit, a paste, a
    move, a delete of ten lines each count as one. The history is kept in memory (up to 10000 changes) and forgotten when another file is opened.
- `Esc` returns to normal mode. A session starts in insert mode, in the last used domain.

### 3.2 Domain mode
- Selects the math domain and thus the grammar, operators and available functions
  (e.g. `*` means something different in matrix vs. PGA vs. lambda calculus).
- Switched from normal mode, e.g. `gm` (matrix), `gl` (lambda), or `:mode pga`.
- Shown in the prompt, e.g. `matrix>`.
- Each line is tagged with the domain it was written in. Switching is not a restart; prior lines keep their domain.
- Variables and functions are shared across all domains. Type checks reject misuse (e.g. a matrix where a lambda is required).

### 3.3 Blocks: notes and code, like the cells of a notebook
The session is read as a sequence of **blocks**, as in a notebook: free text, then a block of code in one domain, then more text. This replaces the domain
tag on every line (`alg>` on each row) by one header per block, and gives the text a place of its own.
- **Code block**: a run of consecutive math lines in the same domain. Its first row carries the gutter (`alg>`, `mat>` ...); the other rows of the block
  have an empty gutter and a thin rule at the left edge that shows how far the block goes. A change of domain, a note, or a blank line ends the block and the
  next math line starts a new one, so the structure needs no markers of its own. The lines in a block still evaluate one after the other in the shared
  environment, exactly as now (variables flow down through blocks and domains).
- **Text block**: a run of consecutive note lines. It is drawn as plain wrapped text: no gutter, no line number column inside the text, no evaluation, no
  ghost mode, no math completion. Note mode edits it like today; the lines of a text block are the paragraphs, and a blank line separates two.
- **Collapse**: `za` toggles the block under the cursor (`zc` closes, `zo` opens, `zM` / `zR` close / open all). A closed code block is one row: its header,
  the number of lines, its first line and the last result (`alg> 12 lines   a = 3 ...  = 10`). A closed text block shows its first line. Collapsing never
  changes what is evaluated: the definitions of a closed block stay visible below it, like a cell that was run. The state is part of the view, not of the
  file; it is kept in the settings file per session path and block number so it survives a restart.
- **Moving around**: `]]` / `[[` jump to the next / previous block, `j` and `k` still go line by line (a closed block is one step). On a closed block
  `dd`, `yy`, `Alt+Up/Down` and `p` take the whole block as the line. (The vim keys are replaced by section 3.0: a folded block is one selected row there, and the selection keys, copy, cut, paste and the move keys take it
  as a whole. Built: the folds are kept in `settings.mlcfg`, section `[folds]`, as `path = 0 12 40` (the first lines of the folded blocks), restored when the
  session is opened and written when they change; the path is the one given on the command line.)
- **Domain switching** (`gm`, `gl`, `Alt+Left/Right`) sets the domain of the next line that is typed; typing it in another domain than the block above starts a
  new block. On a line in the middle of a block the same keys change the domain of the whole block.
- **File format**: unchanged. A line still starts with `m:domain ` (math) or `# ` (note), because the blocks follow from the lines, so old files, diffs and the
  includes keep working, and files written by hand need no markers. (If a way to split two blocks of one domain without a blank line is ever wanted, a
  line `m:algebra` with no text can do it, the same way a blank line does.)
- **Screen**: the row layout already counts a variable number of rows per line (typeset matrices, steps); a closed block is a one row height, a text block
  is its wrapped height. Scrolling, the cursor row and the Visual highlight work on lines as before.
- **Interaction with the rest**: the tutorial and the documentation examples use the same blocks; export writes a text block as paragraphs and a code block
  as a code environment; the proof domain (section 4.12) is a code block whose steps align under one header.
- Built: blocks, the one gutter per code block with a rule down to a corner at the last line, text blocks without gutter, `za zo zc zM zR`, `]] [[`.
- Placement: before the highlight and export work (both need to know blocks), after milestone 3. "Done when": the rows of a session show one header per
  block; text blocks wrap and have no gutter; collapse hides and shows blocks and keeps the environment; tests drive the keys and check the layout rows.

## 4. Core features

### 4.1 Evaluation and step by step
- Steps are shown only on request. A normal evaluation prints just the result.
- Steps can be requested for any line, including previously typed ones: in normal mode, move to the line and press `s`
  (or `:steps` on the current line). The steps expand under that line and can be collapsed again.
- Typing `?` at the end of an expression requests steps for it as it is evaluated.
- Steps are not stored in the file. They are regenerated by re-evaluating the line in the environment as it was when that line
  ran (variables and functions defined above it, including included files). This needs:
  - evaluation to be deterministic and side-effect free apart from definitions,
  - cheap environment snapshots per line (persistent/immutable bindings, or replaying lines up to that point and caching).
- Simplification, derivative, integral and reductions record their rule applications as steps; the rule engine takes a flag
  so step recording costs nothing when not requested.

### 4.2 Ghost mode (live preview)
- Active whenever the user is typing.
- Continuously tries to parse and evaluate the current input and shows the result next to the cursor.
- Shown only when the input is a mathematically valid, complete expression; otherwise nothing is shown.
- Must be side-effect free (no variable assignment, no file writes) and fast/cancellable.

### 4.2.1 Input syntax and inline prettifying
- Both ASCII and unicode input are accepted. They are equivalent: the parser treats `sqrt(x)` and `√x` as the same expression.
- While typing in insert mode, ASCII forms are rewritten inline into their pretty unicode form as soon as the token is complete:
  `sqrt(x)` to `√x`, `x^2` to `x²`, `pi` to `π`, `<=` to `≤`, `->` to `→`, `\alpha` to `α`, `lambda x.` to `λx.`.
- Rewrites are table driven (one table of ASCII form to unicode form, per domain plus a global one) and can be undone with backspace
  immediately after the rewrite, restoring the ASCII text.
- Rewriting only happens in math lines, never in notes.
- Session files are stored as UTF-8 and keep the pretty form. Hand-written ASCII in a file still parses.
- All text is UTF-8 internally. The Windows console is switched to UTF-8 (code page 65001) at startup; cursor movement and
  column width must be by code point, not byte.

### 4.3 Autocompletion
- Context-based: current mode, defined variables/functions, units, and previous lines.

### 4.4 Notes
- The user can write free-text notes between math lines (a note line type, stored in the session file).
- **Formatting in Markdown**: notes use the inline syntax of Markdown, so the file stays plain text and the same text works in the export and in any Markdown
  viewer: `**bold**`, `*italic*` or `_italic_`, `<u>underline</u>`, `~~strike~~` and `` `code` ``, nested (`**bold and *italic***`). The rules are those of
  Markdown: a mark opens before a non space and closes after one, so `2 * 3 * 4` is text, an underscore inside a word (`snake_case`, `x_1`) is text, a mark
  without a partner stays as typed, and a backslash makes a mark plain (`\*`). A code span is not formatted. On the screen the marks are taken away and the
  text is drawn bold, italic, underlined, struck through or as code; while the note is being edited its text is shown as typed, marks included.
- **Replacements in notes**: the rewrites of the math lines work in notes too, so `pi` becomes `π`, `\alpha` `α`, `<=` `≤`, `->` `→`, `x^2` `x²` and
  `sqrt(x)` `√x`; backspace right after a rewrite brings the typed text back, as in math lines. The rules of one domain only (the logic symbols, `lambda`) are
  not active in notes. A word that is really a word can be kept by undoing the rewrite (backspace).
- **Horizontal rule**: a note line that is only `---` (also `***` or `___`, three or more) is drawn as a line across the whole width of the screen, as
  in Markdown, and exported as one (`<hr>`, `\hrule`). While the line is edited it is shown as typed.
- **Free form editing**: inside notes the cursor moves like in a text editor. Up and Down go to the neighbouring note and keep the column, Left at the
  start of a note goes to the end of the previous one and Right at the end to the start of the next, Backspace at the start joins the note with the
  previous one and Delete at the end joins it with the next, and Enter splits the note at the cursor. Movement stops at the edge of the block of notes.

### 4.5 Units
- Quantities carry SI dimensions; operations check and transform dimensions.
- Show how one SI unit expression reduces to another (e.g. `N*m` to `J`).

### 4.6 Visualization
- Show transformations (e.g. matrix applied to a shape/basis vectors) and plots.
- Terminal only: VT/ANSI escape sequences, unicode math typesetting (fractions, roots, matrices drawn with box characters), plots and transformations drawn with braille/block characters.
- **Plots** (built, 2026-10-10): a plot is a value (`ML_VALUE_PLOT`, `math/plotdata.h`) made by `hist (v, k)`, `scatter (x, y)` and `plot (x, y)`; it can be
  named, and the line shows a one line summary. `F7` or `:plot [on|off]` on the line draws it in the plot panel at the right (`ui/plotpanel`), above the
  watch window when that is on; the drawing is `ui/plot` (blocks for histograms, braille for points and lines). Which plots are on is kept with the folds
  in the settings file (`p3` in the entry), not in the `.mlog`. Page `plots` and the example `stats-plot-your-data`.
  Not built: plots of functions (`plot (f, a, b)`), the view of a matrix transformation (basis vectors and a shape), a fitted line over a scatter plot,
  axis titles, theme classes for the plot (it uses the result colour), a panel under the editor for narrow terminals, the unit names in the completion.

### 4.7 Persistence and sessions
- Everything the user enters is stored to files: lines, their domains, notes. Results are not stored; they are recomputed when a session is opened (keeps files diffable and never stale; steps are regenerated the same way).
- One session per file. Plain text, human readable, diffable in git and editable by hand.
- Extension `.mlog`, e.g. `physics.mlog`. Files live wherever the user puts them (no forced folder).
- `matlog physics.mlog` opens or creates that file. Plain `matlog` opens a scratch session in the current directory.
- In the shell, `:open other.mlog` switches session. A recent files list makes returning easy.
- File format (UTF-8, CRLF or LF accepted, written with LF): one entry per line, `<tag> <text>`.
  - `#` note line: `# some free text`
  - `m:<domain>` math line: `m:matrix A = [[1,2],[3,4]]`
  - `include "path.mlog"` include line (no tag)
  - blank lines are kept as-is and shown as blank.
  - Domain names: `algebra`, `lambda`, `pga2`, `pga3`, `boolean`. Matrices, vectors and equations are part of `algebra` (decision of 2026-10-10: their operators mean the same as in algebra, they share the variables, and the matrix, vector and equations domains were never evaluated); `m:matrix`, `m:vector` and `m:equations` in older files read as `algebra`. A domain exists only where the meaning of the operators differs (`*` in PGA, `^` in boolean, application in lambda).
  - A line that fails to parse is still stored; it is shown with its error instead of a result.
- Saved after every committed line (write to a temp file then rename, so a crash never corrupts the session).

### 4.8 Includes
- `include "constants.mlog"` runs the other file's lines first, making its variables and functions available.
- Paths are relative to the including file.
- Includes may not form a cycle. The include stack is tracked and a cycle is reported as an error.
- Included files are read-only from the including session; edit them by opening them with `:open`.
- Each included line keeps its own domain; its definitions are visible in every domain of the including session.
- The including file never stores copies of the included file's results.

### 4.9 Tutorial mode
Status: built (`tutorial/lessons` as data, `tutorial/tutorial` as the observer of the editor, `ui/tutorialpanel`; F6 skips, Shift+F6 goes back; every
lesson is played with its key script by the tests). Not built: lessons for PGA, the stage 2 lessons for export, and files (`:w`, `:open`).

An interactive tutorial that teaches the tool by doing, inside the real editor. Started with `matlog --tutorial`, `:tutorial` or on the very first
run (when no session file exists), and left at any time with `:q` or `:tutorial off`. It has two stages; the second one can be started on its own
with `:tutorial syntax`. Progress (the last finished lesson) is kept in a small file next to the user's files, so the tutorial can be resumed.

Shape of a lesson:
- A tutorial session is an ordinary session (a scratch `.mlog` that is never saved over the user's files) with a lesson panel drawn above the
  line area: the title, a short text, and the current task ("Press `j` to move down one line").
- A lesson is a list of steps. Each step has a prompt and a check that watches the real editor: a key event, a state change, the content of a
  line, or the result of the line. The tutorial only observes the editor (through the same key events and session), it has no private input path,
  so everything the user learns is exactly what the tool does.
- A step is done when its check passes, and the panel marks it and shows the next one. Wrong tries get a hint after a few attempts, never an error.
  `Tab` skips a step, `Shift+Tab` goes back, and the last lines of the panel always show how to quit.
- Where a step needs a starting text (a line to move to, a line to fix) the tutorial inserts it as a note or a math line first.
- Lessons are data: a table of lesson, steps, texts and checks (`tutorial/lessons.h`), so adding or changing a lesson does not need new code.
  Texts are plain UTF-8 and use the same pretty symbols the tool shows.

Stage 1, usage of the tool (the keys and the commands):
1. Insert and normal state: type a line, `Esc`, `i`, `Enter` to edit a line, what the status line says.
2. Moving: `j k`, `gg G`, arrows, `:12`, scrolling in a long session.
3. Editing lines: `dd`, `yy`, `p`, `P`, undoing a rewrite with backspace.
4. Notes: `n`, how notes differ from math lines.
5. Domains: `gm gl ga ...`, `Alt+Left/Right`, the gutter, `:mode`, `:pga 2|3`.
6. Files: `:w`, `:wq`, `:open`, `:q!`, includes, what is saved and what is recomputed.
7. Results and ghost mode: the live preview, results at the end of a line, errors in red.
8. Steps: `s` and `?` once steps exist (milestone 4 and later; the lesson is added then).
9. Command line: `Tab` completion and the list of all commands.

Stage 2, the syntax (what to type, domain by domain):
1. Numbers and operators: exact fractions, `^`, unary minus (`-2^2`), factorial, decimals, the approximation marker `≈`.
2. Names: variables, `a = 3`, functions `f(x) = 2x + 1`, redefinition, built-in functions and constants.
3. Implicit multiplication and the whole-word names (`2x`, `xy`, `2(x+1)`).
4. ASCII and unicode spellings and the inline rewrites (`sqrt(x)` to `√x`, `pi`, `<=`).
5. Errors: reading the message, the marked range, fixing the line.
6. One lesson per further domain as it is implemented: matrices and vectors (`[[1,2],[3,4]]`, `^T`, `^-1`, `det`, `dot`), equations,
   lambda calculus, boolean algebra, PGA, units. Each lesson is added together with its domain (its "done when" includes the lesson).
- Syntax lessons use exercises with a target: "write a line whose result is `1/2`", "define `g` so that `g(2)` is `7`". The check evaluates what the
  user wrote, so every correct answer is accepted, not just one spelling. A "show me" key types the model answer into the line for the user to read.

Tests: every lesson is driven in the test executable by feeding it the model answer (keys or text) and checking that it completes, so a lesson that
can no longer be finished (a key was renamed, a syntax changed) fails the build.

### 4.10 Syntax highlighting and themes
- Math lines are drawn with syntax highlighting: the text of a line is split into classes and each class has a colour from the active theme.
  Classes: number, name (variable), function name (built in and user defined, told apart from variables by looking in the environment), constant
  (`π`, `e`), operator, parenthesis and bracket (the matching pair of the one at the cursor is emphasised, an unmatched one is marked), unit,
  keyword (`include`, `lambda`), and error (the byte range of a parse or evaluation error is underlined or drawn in the error colour).
  Notes, the line number and the gutter have their own classes, as do the result, the ghost preview, the status line and the lesson panel of the
  tutorial.
- The classes come from the lexer (token kinds with byte ranges), so highlighting follows the same rules as the parser, also for the unicode spellings,
  and never needs a second grammar. Text that does not lex or parse is still highlighted token by token, with the bad range marked. Each domain can add
  its own classes later (matrix brackets, boolean operators, lambda binders, PGA blades) through a per domain table of token kind to class.
- A theme is a table of class to style: foreground and background colour, bold, dim, underline, reverse. Colours are given as 24 bit RGB with a
  fallback chain for terminals that cannot show them: 256 colours, then the 16 ANSI colours, then no colour at all (only bold, dim, underline and
  reverse). The level is detected from the terminal (`COLORTERM`, `TERM`; on Windows the console version) and can be forced with `:colors 16|256|24bit|off`.
  `NO_COLOR` turns colour off.
- Built in themes: a dark one, a light one, and a high contrast one (for readability; every pair of foreground and background in it keeps a contrast
  ratio of at least 7:1, checked by a test), plus a monochrome one that only uses bold, dim and underline. The default follows the terminal's background
  when it can be queried (OSC 11), otherwise dark.
- Themes are chosen with `:theme name` (Tab completes the names) and the choice is remembered in the settings file (section 4.15). A user theme is a plain text file `name.theme` in the settings folder, one `class = style` entry per line, for example
  `number = #b5cea8` or `error = #f44747 underline`; a line that does not parse is reported and ignored, and classes that are missing fall back to the
  built in dark theme, so a theme file can be short. `:theme reload` reads the files again.
- The screen draws styled text through the same row strings it already compares to the last frame, so changing the theme redraws every row once and a
  keystroke still redraws only the rows that changed. Column widths never include escape sequences.
- Highlighting is a pure function (text, domain, environment snapshot, theme) to a list of styled runs, so it is unit tested without a terminal, and ghost
  mode and the tutorial reuse it for their own text.

### 4.11 Export mode
Export turns the session into a document. It is a mode of its own, in the same sense as the tutorial: `:export` opens an export panel (format, range,
options, a preview of the first rows, and the path), and `:export <format> [path]` does it in one go. Formats: `latex`, `mathml`, `markdown`, `png`
(image) and `pdf`. Without a path the file is named after the session (`physics.mlog` gives `physics.tex`, `physics.html`, `physics.md`, `physics.png`,
`physics.pdf`) and is written next to it, atomically like a session file. An existing file is never overwritten without asking (`:export! ...` forces).
- What is exported: the lines of the session in order, with their results recomputed (the same results as on the screen). Notes become text, math lines
  become typeset math with their result, matrices become real matrices, errors are left out unless `errors` is given, includes are expanded or kept as a
  reference (`includes expand|ref`), and steps are added for the lines that have them when `steps` is given. A range (`:export pdf 3-12`) or the visual
  selection exports only those lines. Section headings can come from notes that start with `#` after the marker (a note `# # Title` is a heading).
- One structure for all formats. The exporter first builds a small document tree from the session (paragraphs, math expressions as a tree of math
  nodes, matrices, tables of steps), and each format is a writer over that tree, so a new format is a new writer and the content is the same everywhere.
  The math nodes come from the parsed AST (with the results as exact fractions), not from the text of the line, so `x²`, `√x`, `a/b` and `[[1,2],[3,4]]`
  are typeset as real math, in every format. The same tree can later drive a typeset display on screen.
- latex: a complete `.tex` file with `amsmath` (fractions with `\frac`, roots with `\sqrt`, matrices with `pmatrix`, aligned lines with `align*`),
  or just the body with `body` for pasting into another document. Greek letters and symbols map to their commands; text is escaped.
- mathml: presentation MathML inside an HTML file (`<math>` elements, with the text of the line as the `alttext` for accessibility), also usable as a
  fragment. No scripts, no external files.
- markdown: notes as paragraphs and headings, math as `$...$` and `$$...$$` LaTeX blocks (the form most renderers accept), with the plain unicode text
  of the line in a code span when `plain` is given, so the file is readable without a renderer.
- png and pdf: written by the program itself, since there are no libraries. Both need glyphs for text and math, so a small built in font (a bitmap font
  at a few sizes, with the Greek letters and the math symbols the tool uses) is rasterised for png, which is deflate encoded with a small own encoder
  (stored or fixed Huffman blocks are enough) and CRC and Adler checksums; pdf is written as text objects with the standard base fonts for text and
  drawn paths for the radical signs, fraction bars and big brackets, with the math layout (boxes, baselines, super and subscripts, fractions) shared
  with png. Page size (`a4`, `letter`), margins, font size, and light or dark colours (the theme of section 4.10, with a print safe light default) are
  options.
- Layout: a small box layout engine in `export/layout` (width, height, depth, baseline per box; horizontal and vertical lists; fractions, roots,
  scripts, matrices with brackets) that the png and pdf writers use, and that a future on screen typeset display can use too.
- Options are given as words after the format (`:export pdf a4 dark steps 3-12`) and remembered per session in the settings file. Everything has a
  default, so `:export pdf` alone gives a good result.
- Tests: writers are checked on fixed small sessions against expected text (latex, mathml, markdown byte for byte), png by decoding the file with a test
  decoder and comparing size and a few pixels, pdf by checking the structure (the cross reference table offsets are right, every object parses, page
  count) and that the text of the notes is present.
- Placement: after milestone 3 for latex, mathml and markdown (they need only the AST and the results), after the layout engine for png and pdf. The
  syntax of every new domain brings its exporters (matrix is first); a domain is not done until its lines export in every format.

Status: latex, mathml and markdown are built (`export/math` typesets a statement from its tree with a stack on the heap, `export/export` builds
blocks from the session and has one writer per format), with `:export[!] FORMAT [body] [errors] [3-12] [path]` in the editor and
`matlog export FILE FORMAT [words]` on the command line. Not built yet: png and pdf (they need the layout engine), the export panel and its preview,
options other than the four words (`dark`, `steps`, `includes`, page size), options remembered per session, and the visual selection as the range.
The boolean and the lambda domain are typeset too (`export/logicmath`, from the text of the line, with `ml_bool_symbol_at` shared with the highlighter); the
lines of the PGA domains and the includes are written as code until their domains are typeset.

### 4.12 Proofs and their validation
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

### 4.13 Command line interface for scripts and AI agents
The tool must be usable without the interactive screen, by scripts and by AI agents, through command line parameters. The interactive shell is one
front end on the same library; the other one is a batch front end that reads, evaluates and prints, and never needs a terminal.
- Entry points: `matlog eval "<expr>"...` (evaluate expressions or lines, one result per argument, using the optional `--session file.mlog` as the
  environment), `matlog run file.mlog` (evaluate a session and print every line with its result), `matlog check file.mlog` (the proofs of a session: exit
  code 0 when every theorem is complete, 1 when something is not valid, 2 when something is only unchecked), `matlog steps file.mlog:LINE` (the steps of
  one line), `matlog export file.mlog --format latex|mathml|markdown|png|pdf -o out` (section 4.11), `matlog test "<claim>"` (try a claim on
  values, print counterexamples), `matlog fmt file.mlog` (normalise ASCII to the pretty form), `matlog lex|parse "<text>"` (tokens and tree, for tooling)
  and `matlog --version`. Reading `-` takes the text from standard input, so lines can be piped in; a file opened with `matlog file.mlog` and no
  command is the interactive shell as before, and there is `matlog repl --plain` (a line oriented loop with no screen control, one prompt line per input,
  for agents that drive a process through pipes).
- Output formats: `--format text` (the default, human readable, plain unicode, no colour unless asked), `--format json` (one stable, documented JSON
  object per result: `{"line":3,"domain":"algebra","input":"...","kind":"value|error|none","value":"7/2","exact":true,"approx":null,"block":[...],
  "error":{"code":"div_zero","message":"...","start":4,"len":3}}`, and for proofs per step status, reason and expected form), and `--format latex` for
  results. JSON is one object per line (JSON Lines) so it can be streamed. Errors carry a machine readable code, never only prose, and the codes are the
  enum names of the library (section 4.1) so they stay stable.
- Behaviour fit for automation: deterministic output (no timestamps, no colour or cursor codes when the output is not a terminal, `NO_COLOR` honoured),
  exit codes with a fixed meaning (0 success, 1 a calculation or proof result that is an error or invalid, 2 usage error, 3 file or I/O error), errors
  on standard error as well as in the JSON, no prompts and no questions (an operation that would overwrite needs `--force`), a time limit (`--timeout ms`)
  and the size limits of the evaluator reported as errors rather than hanging, and `--session` files are only read unless `--write` is given.
- Discoverability for agents: `matlog help` and `matlog help <command>` print complete usage with examples, `matlog syntax` prints the grammar and the list
  of built in functions, constants and domains, `matlog schema` prints the JSON shapes, and `matlog commands --format json` lists every command with its
  parameters, so an agent can learn the whole tool from the tool. The same text feeds the tutorial and the documentation, written once.
- Placement: a `cli` module with the argument parser and the batch front end (`source/app/` keeps only the wiring), built on the library without the `ui`
  and `platform` console code, so it also works where there is no console. After milestone 3 the first commands are `eval`, `run`, `lex`, `parse` and the
  JSON output, since they need only what exists; `check` comes with the proof system, `export` with section 4.11.
- Tests: the command line is driven in the test executable through the argument parser and the batch front end with captured output (stdout, stderr and
  exit code), with golden files for the text and JSON forms, so a change of the output format is a visible change in the tests.

### 4.14 Documentation: browsable and searchable in the tool and as a website
All user documentation (the syntax of every domain, the commands and keys, the built in functions and constants, the file format, the command line
interface, export, proofs, the tutorial's topics) is written once, in `docs/`, and read in two places that behave the same: inside the tool, and as a
generated static website.
- Source format: plain text pages in `docs/` in a small, strict markup (headings, paragraphs, lists, code blocks, links between pages by name,
  definition lists, tables, and a few marked blocks: `example` for a piece of matlog input with its expected result, `keys` for a key binding,
  `command` for a `:command` or a CLI command, `syntax` for a grammar rule). One file per topic with a header (`id`, `title`, `summary`, `keywords`,
  `see also`), grouped in a table of contents file. Math in the pages is written in matlog's own pretty syntax, so a page is also a valid piece of
  session. The markup is Markdown-compatible where it can be (so the pages are readable and editable as plain text anywhere) and has a parser in the
  `docs` module of the library, no external tool needed.
- Examples are live: every `example` block is evaluated by the tool when the docs are built and checked against its written result, so a page can
  never show a result that is wrong or a syntax that no longer works (a test builds all pages and fails on any mismatch). In the tool an example can be
  inserted into the current session with one key, or evaluated in place; on the website the same examples carry their results as written, and the
  website can run them in the browser when the web build below is present.
- In the tool: `:help` opens the browser, `:help topic` goes to a page, `F1` opens help at the topic of what is under the cursor
  (the domain, the function name at the cursor, the command being typed). The viewer is a full screen page with the table of contents, scrolling,
  following links (Tab/Enter, Backspace for back, a history), and word wrapped, themed text (section 4.10) with the example and key blocks drawn as such.
  Pages are compiled into the executable (a generated table of the page sources, so the tool needs no files next to it) and also readable from a
  `docs` folder when present, which overrides them for editing.
- Search: `/` in the viewer (or `:help search words`) searches titles, keywords, headings and the full text, ranked (title and keyword hits first, then
  headings, then body, all words must match, case and accent insensitive, prefix matching for the last word), with a result list that shows the page and
  the line of the match. The index is built from the pages at start (a few hundred pages are instant) by plain code in the library, no dependency.
  Typing a function name or a command in the command line completes from the same index, and the autocompletion hints (section 4.3) link to the page.
- On the command line: `matlog help topic`, `matlog docs search words [--format json]`, `matlog docs show id [--format text|json|markdown]` and
  `matlog docs list`, so scripts and AI agents read the same documentation, with page ids, titles, summaries and examples as data (section 4.13). The
  generated help texts of the command line (usage, `syntax`, `schema`, `commands`) come from these pages instead of being written twice.
- The website: `matlog docs build site/` writes a static site from the same pages: one HTML file per page, the table of contents as navigation, the
  search index as one JSON file, a small search script and a stylesheet (the themes of section 4.10 as light and dark styles), the syntax highlighting of
  the examples done by the same highlighter and written out as HTML, the maths in MathML (section 4.11). Everything is plain files that any static host
  can serve; it works without scripts for reading and uses a small script for search, keyboard navigation (the same keys as the tool: `/` search, `g`
  `h` go home, `n` `p` next and previous page) and the optional running of examples. The interactive features are the same as in the tool, written once
  as data (the pages and the index) and drawn by two front ends: the terminal viewer and the web page.
- Running the tool in the browser: a later, optional step builds the library for WebAssembly (it is plain C11 with a thin platform layer, so only
  the platform and console code are replaced by an in memory file system and a canvas or DOM text surface). It would let the website offer a try-it
  prompt on every example, running the real evaluator. The page format and the index are designed so this needs no change to the pages.
- Quality: the build checks every link between pages, every `see also`, every keyword that is searched for in the tutorial, that every built in function,
  command, key binding, domain and error code has a page or a section (a list of those is generated from the code and compared with the pages), and that
  no page is unreachable from the table of contents. A feature is not done until its page exists, as for the tutorial lessons and the exporters.
- Placement: the page format, the parser, the viewer in the tool and the search come after milestone 3 (they need only the editor and the library); the
  first pages cover what exists (algebra syntax, matrices, keys, commands, the command line, the file format); the website build comes with the
  export module (it uses its MathML writer). Each later domain adds its pages as part of its "done when".

### 4.15 Settings and key bindings
- One plain text settings file `settings.mlcfg` in the settings folder of the user (Windows `%APPDATA%\matlog`, Linux `$XDG_CONFIG_HOME/matlog` or
  `~/.config/matlog`, macOS `~/Library/Application Support/matlog`; the folder is found in the `platform` module). The theme file `.theme`, the
  tutorial progress and the history of the command line live in the same folder. `MATLOG_CONFIG` overrides the folder, which also makes the tests and
  the CLI independent of the real home of the user.
- The format is line based, `key = value`, `#` starts a comment, sections in `[brackets]` group the keys (`[editor]`, `[display]`, `[keys]`; the editor has no modes, so one key table serves the
  text and the line selection). A line that does not parse is reported (file, line number, reason) and ignored, so
  the tool always starts; unknown keys are kept when the file is written again, so a newer file survives an older program.
- Settings: theme, colour level, the default domain, the width of the result column, whether the ghost preview and the live Markdown preview are on,
  how many lines the undo history keeps, whether the last file is reopened at the start, and the confirm before `:q!`. Every setting has a built in
  default, so an empty or missing file is a valid configuration.
- **Key bindings**: a binding maps a key (`j`, `Ctrl+r`, `Alt+Left`, `F2`, a sequence of keys such as `dd` or `g g`) in one input state to a named
  action (`move-down`, `delete-line`, `undo`, `open-note-below`, ...). The editor looks up keys in the table of the state instead of comparing
  characters, and the built in keys are the default table, so the free form keys of section 3.0 are the defaults (a vim like set can be added as a binding set) and a file entry replaces or removes one
  (`j = none`). The table is checked when it is loaded: unknown actions, unknown key names, a key that is a prefix of another in the same state, and
  conflicts are reported. Text keys (typing in insert and note states) are not bindable, only the control keys there.
- **Key bindings are built** (`ui/keymap.h`): 25 actions, the default table, `[keys]` in the settings file, `:keys`, `:map`, `:unmap`, the page `keys`
  of the documentation. Only keys that cannot be typed as text are bindable (Ctrl or Alt, function keys, Shift+Enter). Not built yet: sequences of keys,
  bindings for the keys of the help viewer and the command line, and the tutorial and docs showing the user's own keys.
- **Settings are built** (`session/options.h`, one table for the editor and the CLI): `theme`, `colors`, `highlight`, `ghost`; `:set`, `:config`
  (`path`, `reload`), `matlog config list|get|set|path`, the page `settings`. Not built yet, because the features they switch do not exist: the default
  domain, the width of the result column, the live Markdown preview, the undo history size, reopening the last file, the confirm before `:q!`.
  Each is one more row in the table when its feature is there.
- In the tool: `:set key value` changes a setting (Tab completes names and values) and writes it to the file, `:set` alone lists them, `:map state key
  action` and `:unmap state key` change a binding, `:map` lists them, `:config` shows the path of the file and opens the settings page of the
  documentation, `:config reload` reads the file again. The documentation (section 4.14) and the tutorial show the keys of the user, not the
  defaults, since both read the same table; a generated reference of all actions is a documentation page.
- **Key names in texts are placeholders** (built, 2026-10-10: `ml_keymap_expand`, the hints, the tutorial texts and answers `<ACTION_UNDO>`, the pages of the documentation, the check in `test_keymap.c`; the key table page `keys` lists the keys themselves; a placeholder followed by two spaces starts a column that keeps its width; not built: the website build expanding the pages with the default table, the status line and message texts when they name a key, `F6` of the tutorial as an action): a text that names a key, such as the hints, the status line, the messages, the help
  and the tutorial prompts (`{Ctrl+Z} undoes, {Ctrl+Y} redoes, {Ctrl+S} saves, {Ctrl+Q} quits`), must not contain the key itself but the action it
  stands for, written `{ACTION_UNDO}`, `{ACTION_REDO}`, `{ACTION_SAVE}`, `{ACTION_QUIT}` (the enum name of the action in `ui/keymap.h`). One
  function expands them from the keymap when the text is shown, so the user's own bindings appear (the first key of the action, or its name
  when it has none), and a changed default key never means going through the strings by hand. The pages of the documentation use the same
  placeholders, expanded by the viewer and by the website build (with the default table). A test checks that every placeholder names an action and
  that no text still contains a literal key name such as `Ctrl+Z`, so a missed string cannot stay.
- The file is written atomically (temporary file, then rename) and only when the user changed something, with the order and the comments of the
  existing file kept.
- The CLI (section 4.13) reads the same settings for the display options and ignores the key bindings; `matlog config get|set|list|path` reads and
  changes settings without the screen, for scripts and agents.

### 4.16 Examples and snippets

- **Examples** are worked sessions that show how to solve real problems with matlog, written as `.mlog` files in `examples/` with notes that explain each
  step. They are built in like the documentation pages (compiled in, browsable in the help viewer under an "Examples" entry, searchable), and `:examples`
  opens a table of contents grouped by area; picking one opens it as a read only session that can be copied into the user's own file. Every example is
  evaluated by a test, so it never goes stale.
- They cover every area (arithmetic, symbolic, matrices, units, boolean, lambda, PGA, statistics, images) and interesting algorithms and problems
  with their solution: solving linear systems, least squares fit, Newton's method, change of basis, rotations with motors, Church numerals and
  recursion with the Y combinator, minimising a boolean circuit, and a robot arm.
- **Inverse kinematics** is a flagship example: a planar two link arm (forward kinematics with rotations, the closed form inverse with `acos`,
  the Jacobian and its pseudo inverse, damped least squares iteration), then a 3D arm with motors once PGA exists. It shows what the tool must be able
  to do and drives requirements for functions, vectors and plots.
- **Snippets** are small named pieces of text that expand in place: `:snippet name` or a completion entry inserts a template (a line or a block
  with placeholders; Tab moves to the next one). Built in snippets (a matrix template, a loop style sum, a rotation) plus the user's own in a
  `snippets` file in the settings folder; they appear in autocompletion (section 4.3) and in the help.

**Built so far (2026-10-10):** `examples/*.mlog` (eleven examples, compiled in, one help page each with the live results, the contents page `examples`,
`:examples`, `:example NAME` which inserts the lines after the cursor, `matlog examples [NAME]`), `snippets/*.mlog` (nine, plus the user's files in the
`snippets` folder of the settings folder; `:snippet [NAME]`, the page `snippets`). Every example is run by a test and must not have an error. Not built:
the Tab through placeholders, snippets in the math completion (section 4.3), `matlog snippets`, opening an example as a read only session, and the
examples that need later features (statistics, images, PGA motors, the full inverse kinematics with approximate matrices: matrices hold exact numbers only).

### 4.17 More number systems: complex numbers and quaternions

- **Complex numbers** are a value kind with exact rational parts (`3 + 4i`, `i^2 = -1`): arithmetic, `conj`, `re`, `im`, `abs`, `arg`, polar form,
  `sqrt` of negatives, roots of unity, and complex roots in `solve` (the `not_real` error becomes a result where it makes sense). Matrices may hold
  complex entries.
- **Quaternions** are a value kind (`1 + 2i + 3j + 4k`, or `quat (w, x, y, z)`): the Hamilton product, conjugate, norm, inverse, normalise, conversion
  from and to axis angle and rotation matrices, `slerp`, rotating a vector (`q v q*`). Shown in the same way as results are today, with exact or approximate
  parts. They connect to PGA (motors, milestone 7) and to the inverse kinematics example.
- Both get highlight classes, completion, steps for the product, export in LaTeX and MathML, JSON types `complex` and `quaternion`, and documentation pages.

**Built so far (2026-10-10):** `math/hyper.h`: the value kinds `complex` and `quaternion` with one four part representation (exact or approximate), `+ - * /`
and powers, the units `i`, `j`, `k`, `sqrt` of a negative number, `re im conj abs arg polar quat rotate`, `exp ln sin cos` of a complex number, the JSON
types, the watch window types, three examples and the page `complex`. Not built: complex entries in matrices, complex roots from `solve`, `slerp`,
`normalize`, conversion of a quaternion from and to an axis and angle and to a rotation matrix, the LaTeX and MathML export of the new values, steps for
the product, and the completion of the units. The names `j` and `k` hide nothing but are values where a value is needed, so a mistyped variable `k` is
the quaternion unit instead of an undefined name; a variable or a parameter of the name wins.

### 4.18 Standard library

- A library of useful functions written in matlog's own language and included by default (read only, acyclic includes as in section 4.8, so a user can
  read and copy them): number theory (`isprime`, `factor`, `fib`, `binomial`), sequences and sums (`sum`, `prod`, `range`), root finding (`newton`,
  `bisect`), numeric integration and ODE steps (`simpson`, `rk4`), interpolation and fits (`lerp`, `polyfit`, `lstsq`), vector helpers (`proj`,
  `gram_schmidt`), geometry (rotation matrices, `deg`, `rad`), physical constants with units, and the stat functions of section 4.22.
- This needs user defined functions with several statements, conditionals, recursion limits and lists/ranges in the language (to be designed: the
  evaluator today has single expression functions). Functions are documented by a comment convention that the help viewer and the completion read.
- Each function has a test and, where it teaches something, an example (section 4.16).

### 4.19 Export of functions to C, C++ and Python

- A function or a block of definitions can be exported as source code: `:export-code FILE.c|.cpp|.py` or `matlog codegen FILE:LINE --lang c|cpp|python`.
  The expression tree of the function is the input; the output is a plain function (doubles, or fixed-size arrays for vectors and matrices) with the
  names of the parameters, a comment holding the original matlog line, and constants and helper functions it needs from the standard library.
- Targets: C11 (`double`, `<math.h>`), C++17 (`std::array`, `<cmath>`, optionally a small header only vector/matrix type), Python 3 (plain functions,
  optionally with NumPy). Exact rationals become `double` constants (with a note), integers stay integers where the type is known.
- Symbolic results are exported after simplification, so `diff` and `integrate` results become code (derivatives and Jacobians for the inverse kinematics
  example). A test compiles the output of the examples with the system compiler, or runs it with Python when present, and compares values.
- Functions that cannot be exported (symbols that stand for nothing, unsupported domains) give a clear error with a byte range.

### 4.20 Images and kernels

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

### 4.21 Watch window

- A panel (toggle `F4`, or `:watch`) listing every variable and function of the environment at the cursor line: name, type (`exact`, `approx`,
  `matrix 3x3`, `expr`, `quantity`, `complex`, `image 640x480`, `function f(x, y)`, boolean and lambda definitions), and value (shortened to fit, a
  matrix as its dimensions plus the first rows). The list follows the cursor: it shows the environment as that line saw it (the snapshots the
  results already keep), so moving up and down shows how values change, and the names defined or changed by the cursor line are marked.
- Enter on an entry expands it (a full matrix, a long expression), and `d` jumps to the line that defines it. Sorting by order of definition
  or name, and a filter by typing. The panel is drawn in the screen module with theme classes of its own (`watch-name`, `watch-type`, `watch-value`).
- The same data is available from the CLI: `matlog vars FILE[:LINE]` prints the table (`--format json` for tools).

**Built so far (2026-10-10):** the panel at the right (`ui/watchpanel.h`, F4 or `:watch [on|off]`, the setting `watch`), the rows from the results'
snapshots (`session/watch.h`: variables, functions, boolean and lambda definitions, type, value, a star for what the line changed), `matlog vars
FILE[:LINE]` with JSON. Not built: moving into the panel, expanding a long value (Enter), `d` to jump to the definition, sorting and filtering, the
theme classes `watch-name`, `watch-type`, `watch-value` (the panel uses name, function, gutter and result), a panel under the editor for narrow terminals.

### 4.22 Statistics

- Functions over lists/vectors (data can be written `[1, 2, 3]`, loaded from a CSV file with `load_csv ("data.csv")`, or generated with `range`):
  `mean`, `median`, `mode`, `var`, `std` (sample and population forms), `min`, `max`, `quantile`, `iqr`, `cov`, `corr`, `skew`, `kurt`, `cumsum`,
  `sort`, `histogram`, with exact rational results where the data is exact.
- Regression: `linfit (x, y)` (slope, intercept, r squared), `polyfit (x, y, n)`, `lstsq`, with the steps of the normal equations shown as matrix steps.
- Distributions: `normal_pdf`, `normal_cdf` and its inverse, `binomial_pdf`, `poisson_pdf`, `t`/`chi2` quantiles for tests, `zscore`; combinatorics
  (`binomial`, `perm`) from the standard library. Random numbers only through an explicit seeded generator (`rand (seed)`) so results stay
  reproducible when a session is opened again.
- Visualisation (section 4.6): histogram and scatter plots in braille/block characters. Highlight, completion, docs page, examples
  and tests as for every domain; the results are exact where possible, approximate and marked as `≈` otherwise.

**Built so far (2026-10-10):** `math/stats.h`: sum count mean median mode var std pvar pstd quantile iqr cov corr skew kurt cumsum sort histogram range
linfit rsq polyfit lstsq binomial perm binomial_pdf poisson_pdf normal_pdf normal_cdf normal_inv zscore, and `min`/`max` of a list; exact while the data is
exact; the page `statistics` and three examples. Not built: loading data from a CSV file (`load_csv`), a seeded random generator, the `t` and `chi2`
quantiles and tests, lists longer than 64 numbers (a matrix has at most 64 rows or columns), histogram and scatter plots in the terminal, and data
given with units.

### 4.23 Fuzzer

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

## 5. Architecture (proposal)

Layers, each a module under `source/matlog/` with public headers in `include/matlog/`:

1. `platform`: console I/O (raw input, VT output, terminal size), file system, time. The only module allowed to include OS headers.
2. `core`: memory arena, strings, dynamic arrays, error handling.
3. `expr`: expression tree (AST) shared by symbolic domains, exact numbers (bignum integers and rationals; floats only on request).
4. `parse`: tokenizer + per-mode parsers producing an AST.
5. `eval` / `symbolic`: evaluation, simplification, differentiation, integration, rule engine that records steps.
6. `domains`: matrix, vector, lambda, pga, boolean, units implementations.
7. `session`: lines, modes, notes, persistence.
8. `ui`: terminal input (line editor, modes, ghost text, completion) and output rendering.
9. `app`: `main.c`, wiring.
10. `ui/theme`, `ui/highlight`: classes, themes, colour levels and the highlighter (section 4.10).
11. `export`: the document tree, the writers (latex, mathml, markdown, png, pdf) and the box layout (section 4.11).
12. `proof`: the statements, the checker core with its trusted rules, the search that proposes justifications (section 4.12).
13. `cli`: the argument parser and the batch front end with the text and JSON output (section 4.13).
14. `docs`: the page parser, the index and search, the terminal viewer and the site builder; the pages themselves are in `docs/` (section 4.14).
15. `tutorial`: lesson data, the step checks that watch the editor, the lesson panel (section 4.9).

## 6. Coding rules

- C11, no dependencies beyond the C standard library and the OS API.
- Target is Windows now (Win32 console API), but all OS code lives in a small `platform` layer so Linux/macOS can be added later.
- Indentation uses ONLY tabs. Spaces are only for alignment.
- Opening brace always on its own line (Allman); empty line after a closing `}` unless followed by `}` (so also between `}` and `else`).
- No early exits: one `return` per function, one cleanup stage at the end, avoid `break`/`continue`/`goto`.
- No recursion: trees and nested data are walked with a loop and an explicit heap stack (or the tree's own links), parsers are
  state machines. The C stack use never depends on the input. See `CLAUDE.md`.
- Space between function name and `(` (`ml_str_clear (&s)`) and before array subscripts (`e.bytes [0]`).
- Align `=` of consecutive assignment lines, and align type / name / `=` / comment columns of consecutive declarations (spaces only).
- Lines are at most 80 columns (tab = 4); long expressions are broken one argument / operand per line.
- No `?:`; use `ml_choose (cond, a, b)` with one argument per line.
- Fixed-width integer types only (`int32_t`, `uint64_t`, ...); sizes and counts are `uint64_t`, not `size_t`.
- Struct initializers use named entries (`{ .width = 2, .height = 5 }`); enum-indexed tables use `[ENUM] = value` entries; loop variables are declared in the `for`.
- Avoid pointers where possible (structs by value, indices, typed containers). See `CLAUDE.md` for the full rules.
- OS specific code lives in `platform/<os>/` (e.g. `source/matlog/platform/win32/`, `include/matlog/platform/win32/`); only the matching folder is built. Portable code stays in `platform/`.
- Headers go in `include/matlog/`, sources in `source/matlog/`, include as `<matlog/x.h>`.
- All build outputs go to `build/`.

## 7. Milestones (proposal)

1. Skeleton: line editor, input states and domain switching, `.mlog` session file, echo. (done)
2. Arithmetic expression parser and evaluator with ghost mode.
3. Matrices and vectors with step by step output.
4. Symbolic simplify, derivative, integral with steps.
5. Units. (done)
6. Lambda calculus and boolean algebra. (done: section 9e)
7. PGA.
8. Visualization and notes polish, completion.
9. Syntax highlighting and themes (section 4.10). The highlighter only needs the lexer, so it can be built any time after milestone 2; each
   later domain adds its token classes with itself.
10. Export mode (section 4.11): latex, mathml and markdown first, png and pdf with the layout engine after.
11. Command line interface for scripts and agents (section 4.13): `eval`, `run`, `lex`, `parse` and JSON output as soon as possible, the rest with the features they serve.
12. Proofs and their validation (section 4.12), after the rule engine of milestone 4 (the checker needs simplification and rewriting).
13. Documentation (section 4.14): the page format and the in-tool viewer and search first, with pages for what exists; the website build with the export module.
14. Tutorial mode (section 4.9). Stage 1 (usage of the tool) can be built any time after milestone 2, since keys, commands, results and ghost mode
   exist; the milestone 3 to 7 lessons of stage 2 are added together with the domains. The whole tutorial is complete when the last domain is done.
15. Settings and key bindings (section 4.15). The key table should come before the tutorial and the documentation so that they can show the
   user's own keys; the settings file can be built any time after milestone 2.
16. Examples and snippets (section 4.16): the browsable table of contents and the first examples can start as soon as the help viewer exists
   (it does); examples that need later features (inverse kinematics with motors, images, statistics) are added with those features, each tested.
17. Complex numbers and quaternions (section 4.17), before PGA's motors are tied to quaternions, and before the polished inverse kinematics example.
18. Statistics (section 4.22) and the standard library (section 4.18). The standard library needs multi statement user functions in the language
   and lists/ranges first; statistics can start with the vector functions that already exist.
19. Watch window (section 4.21), after the editor can show a side panel; it only reads the environment snapshots the results keep.
20. Export of functions to C, C++ and Python (section 4.19), after the standard library, so the helper functions can be exported with the function.
21. Images and kernels (section 4.20), with the PNG reader and writer shared with the export module (milestone 10).
22. Fuzzer (section 4.23). The driver and the first targets (lexer, parser, evaluator, `.mlog` reader) can start any time and are worth doing early,
   since every later feature adds a target.

## 8. Milestone 1 tasks

Goal: run `matlog file.mlog`, type lines in insert/note mode, switch domain, move around in normal mode, and have everything saved and reloaded. Lines are only echoed (no evaluation yet).

Each task lists its files (headers in `include/matlog/`, sources in `source/matlog/`) and a done-when check.

1. **Core basics** (`core/`): `mem.h` (arena allocator), `str.h` (UTF-8 string builder, code point iteration, display width), `vec.h` (growable array macro/helpers), `err.h` (result codes).
   Done when: a small test executable exercises arena, string append and UTF-8 iteration (`π` as multi-byte, combining cases excluded for now).
2. **Platform layer** (`platform/`): `console.h` (enable raw mode and VT output, UTF-8 code page 65001, read key events incl. arrows/Esc/Ctrl, terminal size, resize notice), `fs.h` (read whole file, atomic write via temp + rename, path join, directory of path).
   Done when: a demo prints each key event decoded as a key code plus modifiers, and Esc is told apart from arrow-key sequences.
3. **Session model** (`session/session.h`): `Line {kind, domain, text}`, `Session {path, lines[], cursor}`, insert/delete/replace line, domain enum (`algebra`, `lambda`, `pga2`, `pga3`, `boolean`).
   Done when: unit-style test builds a session in memory and edits it.
4. **`.mlog` reader/writer** (`session/mlog.h`): parse and write the format from section 4.7 (note `# `, math `m:<domain> `, `include "x"`, blank), CRLF and LF both accepted, unknown tag becomes an error line, atomic save.
   Done when: round trip (read, write, read) of a sample file gives identical lines, and a corrupt file never loses data.
5. **Line editor** (`ui/lineedit.h`): single-line UTF-8 buffer with cursor, insert, backspace, delete, left/right/home/end, word jump, by code point, redraw with correct column width.
   Done when: edits work with `π`, `√x`, and wide text without cursor drift.
6. **Rewrite table** (`ui/rewrite.h`): table of ASCII to unicode forms (global + per domain), applied when a token completes, one-step undo with backspace (section 4.2.1). Initial entries: `sqrt(x)` to `√x`, `^2` to `²`, `pi`, `<=`, `>=`, `!=`, `->`, `\alpha`-style Greek, `lambda x.`.
   Done when: typing `x^2 + sqrt(y)` shows `x² + √y` and backspace right after a rewrite restores the ASCII.
7. **Input state machine** (`ui/modes.h`): normal/insert/note states, `Esc`, keys from section 3.1 (`j`,`k`,`dd`,`yy`,`p`,`Enter`,`i`,`n`), domain switch keys (`g` + letter) and `:` command line with `:mode`, `:open`, `:w`, `:q`.
   Done when: all keys work against the session model and the status line shows state + domain (e.g. `-- INSERT -- matrix>`).
8. **Screen rendering** (`ui/screen.h`): draws visible lines from the session (domain tag, note styling), status line, command line, scrolling to keep the cursor visible, minimal redraw, handles resize.
   Done when: a session larger than the terminal scrolls correctly and survives resize.
9. **App wiring** (`app/main.c`): parse args, open or create file (or scratch session), run main loop, save after each committed line, restore terminal on exit and on crash/Ctrl+C.
   Done when: acceptance test below passes.
10. **Build**: `premake5.lua` unchanged except possibly a second small `tests` project (console app) for task 1-5 checks.

Acceptance test (manual): open a new file, write a math line in `matrix`, a note, switch to `lambda`, write another line, `:q`, reopen the file and see the same three lines with the same domains and the terminal restored.

Out of milestone 1: evaluation, ghost mode, autocompletion, includes (parsed and stored, not executed), steps.

## 9. Milestone 2 tasks

Goal: in the algebra domain every math line is parsed and evaluated exactly,
the result is shown at the end of the line, and ghost mode previews the result
of the line being typed. Other domains still only echo their lines.

### 9.1 Decisions this milestone is built on

- Numbers are exact: bignum integers and rationals. A result that is not a
  rational (`sqrt(2)`, `sin(1)`, `pi`) is an approximation: a double that is
  flagged as such, shown as `≈ 1.41421356`, and contagious (exact op
  approximate gives approximate). `sqrt(4)` and `2^-1` stay exact.
- Syntax: ASCII and unicode spellings parse to the same tree (`*`, `×`, `·`;
  `/`, `÷`; `-`, `−`; `x^2`, `x²`; `sqrt(x)`, `√x`; `pi`, `π`).
  `^` is right associative and binds tighter than unary minus (`-2^2 = -4`).
  Postfix `!` is the factorial. Arguments are separated by `,`, decimals use `.`.
- Implicit multiplication: a number followed by a name or `(`, and `)` followed
  by `(`, a name or a number: `2x`, `2(x+1)`, `(a)(b)`, `2pi`. Names are whole
  words (`xy` is one name), so letters are never juxtaposed.
- Statements: an expression, `name = expr` (variable) or `f(x, y) = expr`
  (function). Variables and functions are shared by all domains and live in the
  session environment.
- `0^0`, division by zero, `ln(0)` and similar are errors with a message, never
  a crash. Huge results are refused (more than 100000 bits) so ghost mode stays
  responsive.
- Built in: constants `pi`, `e`; functions `sqrt abs sin cos tan asin acos atan
  ln log exp floor ceil round min max gcd lcm`.

### 9.2 Tasks

1. **Bignum** (`core/bigint.h`): signed arbitrary precision integers on 32 bit
   limbs: from/to uint64 and decimal text, compare, add, subtract, multiply,
   divmod, gcd, power, shifts, bit length, size limit.
   Done when: tests check known products, quotients with all sign combinations,
   `2^200`, `100!`, gcd, and parsing/printing round trips.
2. **Rationals** (`math/rational.h`): normalized numerator/denominator, add,
   subtract, multiply, divide, integer power (also negative), compare, exact
   square root when both parts are perfect squares, to double, parse a decimal
   like `0.75` as 3/4, format as `7/2` and as a decimal expansion with a cut-off.
   Done when: tests cover normalization, negative denominators, powers and the
   decimal formatting of `1/3`, `7/2` and `-5/8`.
3. **Values and errors** (`math/value.h`): a number is exact or approximate; the
   arithmetic of two values (exact stays exact, mixing gives approximate);
   an error carries a code, a message and the byte range in the line.
   Done when: tests cover the mixing rules and every error kind.
4. **Lexer** (`parse/lexer.h`): tokens with byte ranges for numbers, names
   (letters, digits, `_`, Greek and other letters), operators and punctuation,
   the unicode operator spellings, superscript digit runs and `√`.
   Done when: tests tokenize ASCII and unicode versions of the same input to the
   same token kinds, and bad characters give an error token with a range.
5. **Parser** (`parse/parser.h`): precedence climbing into an AST stored as a
   vector of nodes that refer to each other by index (no pointers), with
   implicit multiplication, calls, factorial, assignment and function
   definition. Parsing never reads past the end and reports the position of the
   first problem. `ml_parse_complete` tells ghost mode whether the text is a
   whole expression.
   Done when: tests check precedence, associativity, implicit multiplication,
   unicode/ASCII equivalence, statements and a list of malformed inputs.
6. **Environment** (`math/env.h`): variables and functions as an append-only
   list of bindings; a snapshot is just the current length, so any earlier line
   can be re-evaluated in the environment it saw (needed for steps later) and
   redefinition simply shadows.
   Done when: tests check shadowing and snapshots.
7. **Evaluator** (`math/eval.h`): evaluates a statement tree in an environment
   snapshot, the built-ins, user functions with parameters, recursion depth
   limit, the size limit. Evaluation has no side effects; the caller decides
   whether to keep a new binding.
   Done when: tests check arithmetic, exactness (`1/3+1/6 = 1/2`), the
   approximation flag, every built-in, user functions and every error.
8. **Session evaluation** (`session/results.h`): one result per line, kept next
   to the session and recomputed (not stored in the file) when the session is
   opened and from the changed line downward after each edit. Include lines
   run the included file (cycles are an error). Lines of other domains have no
   result.
   Done when: tests check that definitions flow down, an edit changes the lines
   below it, includes work and a cycle is reported.
9. **Result display** (`ui/screen.c`): the result is shown dim at the end of its
   row (`= 7/2 ≈ 3.5`), errors in red, cut to the width; the row stays clipped
   as before.
   Done when: layout tests show results, errors and clipping.
10. **Ghost mode** (`ui/ghost.h`): while typing in Insert, the buffer is parsed
    and evaluated against the environment as of that line, and the result is
    shown after the line in a dim style, only when the input is a whole valid
    expression that evaluates without error. No bindings are created.
    Done when: tests check that incomplete input (`2+`, `(3`) shows nothing,
    complete input shows the result, errors show nothing and the environment is
    unchanged.
11. **Wiring and acceptance**: results recomputed on edits, ghost text drawn on
    every keystroke, `scratch.mlog` re-evaluated on open.
    Acceptance test (manual): type `a = 3`, `b = a^2 + 1`, `f(x) = 2x + b`,
    `f(a)`, `1/3 + 1/6`, `sqrt(2)`, `10!`; the results are `3`, `10`, `10`, `16`,
    `1/2`, `≈ 1.41421356`, `3628800`; quit, reopen, and see the same results.

Out of milestone 2: step by step, symbolic results (`x + x`), floating point
mode switch, matrices, units, autocompletion.

## 9b. Milestone 3 tasks

Goal: in the matrix and vector domains, exact matrices and vectors can be
written, combined and inverted, shown typeset over several rows, and their
steps can be shown on request.

### Decisions this milestone is built on

- Matrices are exact: every cell is a rational (or an approximation, as in
  milestone 2), so `[[1,2],[3,4]]^-1` has exact fractions.
- Literal: `[[1,2],[3,4]]` is a matrix of rows, `[1,2,3]` is a vector (a column
  of a 3 x 1 matrix for the arithmetic). Names are shared with algebra.
- `*` is the matrix product for matrices and the scalar product for a number
  and a matrix; `+ -` need equal sizes; `^-1` is the inverse, `^T` the
  transpose. Mismatched sizes and singular matrices are errors with a message.
- Built in: `det inv transpose trace rank rref solve identity` for matrices and
  `dot cross norm` for vectors.
- Size limit 64 x 64 so ghost mode stays responsive.

### Tasks

1. **Exact matrices** (`math/matrix.h`): a matrix of rationals in an arena;
   add, subtract, product, scale, transpose, trace, determinant, inverse, rref
   and rank, solving `A x = b`. Done when: tests check known products,
   determinants, inverses (A times inverse of A is identity), singular and
   size errors.
2. **Vectors** (`math/vector.h`): dot, cross (3D), norm, on top of task 1.
3. **Values**: matrix values in `MlValue`, formatting as text.
4. **Syntax**: `[ ]` literals in the lexer and parser, `^T`, `^-1`.
5. **Evaluator**: operators and built-ins on matrix values, errors.
6. **Typeset display**: results of several rows drawn with box characters under
   the line (`ui/screen.c`).
7. **Steps**: the rule recording of section 4.1 for the matrix operations
   (row reduction steps, determinant expansion), shown with `s`.

## 9c. Milestone 4 tasks

Goal: symbolic calculation in the algebra domain: expressions with free symbols can be simplified, expanded, substituted, differentiated and integrated,
with exact results and steps on request.

### Decisions this milestone is built on

- Symbols are free only inside the symbolic functions. A bare `x + y` is still an error when x or y is undefined (so a typo is not silently a result);
  `simplify(x + x)` is `2x`. Names bound to numbers are replaced by their values (`a = 3` then `simplify(a*x + x)` is `4x`), except the symbol being worked on.
  A later switch can make symbols free everywhere.
- Functions: `simplify(f)`, `expand(f)`, `subs(f, x, v)`, `diff(f, x)`, `diff(f, x, n)`, `integrate(f, x)`, `integrate(f, x, a, b)`. User defined functions
  are inlined (`f(x) = x^2` then `diff(f(x), x)`). `pi` and `e` are exact constants; `sin cos tan asin acos atan ln exp abs sqrt` are the known functions.
- A symbolic result is a new kind of value (an expression tree), shown in the pretty syntax (`x²`, `√x`, `1/x`), storable in variables and usable in
  later symbolic calls. A result that has no symbols is an exact number again.
- Integration scope as in 2.1: polynomials, the table integrals, linearity and constant factors, linear substitution (`sin(2x+1)`); parts and rational
  functions are later. What is not found is an error (`cannot_integrate`), never a wrong answer.
- Trees are immutable and live in an arena; nothing recurses (explicit stacks), sizes are limited (200000 nodes per calculation).

### Tasks

1. **Expression trees** (`sym/expr.h`): nodes, comparison, printing. Done when: tests print trees in the pretty syntax and parse them back.
2. **Canonical form** (`sym/simplify.c`): smart constructors for sums, products, powers and functions; `simplify`, `expand`, `subs`. Done when: tests check
   like terms, numeric folding, powers, expansion of (x+1)^n, ordering, and identities such as x - x = 0.
3. **From the syntax tree** (`sym/build.c`): the parsed expression to a tree, with bound names replaced and user functions inlined (explicit scope stack).
4. **Derivative** (`sym/diff.c`): sum, product, power, chain rule, the elementary functions; steps.
5. **Integral** (`sym/integrate.c`): polynomials, table, linear arguments, definite integrals; steps.
6. **Values and evaluator**: the expression value kind, the symbolic functions in the evaluator, environment copies, results, JSON type `expr`.
7. **Steps** for the symbolic functions (`s` on a line).
8. **Limits** (`sym/limit.c`): `limit(f, x, a [, side])`, `a` a point or `inf`/`-inf`, `side` 1 or -1 (both sides must agree without it). An extended
   evaluation (finite values with the side they are approached from, infinities, unknown) gives the limit by substitution, and l'Hopital's rule on
   a quotient, or on a product of a zero and an infinite factor written as a quotient, gives the rest. What is not found is `no_limit`. Definite
   integrals with `inf` bounds use it (`integrate(exp(-x), x, 0, inf)`). Not built: infinity minus infinity (`sqrt(x^2+x) - x`), series.
9. **More integrals** (`sym/ratfun.c`, `sym/intmore.c`): partial fractions for quotients of polynomials (rational roots of the denominator with their
   multiplicities and one irreducible quadratic, solved as an exact linear system; atan or a logarithm for the quadratic), substitution (every inner
   part of the integrand is tried as u), integration by parts (repeated through a work list, by the order inverse trig, log, algebraic, trig, exp,
   and solving I = A + k I when the integral comes back), and the half angle formulas for sin² and cos². An answer from these methods is checked by
   differentiating it at sample points before it is believed. Not built: a quadratic factor that is squared in the denominator, cubics and quartics
   without rational roots, trigonometric substitution. The rules of the steps are the enum `MlRule` with `ml_rule_name`.
10. **Solving** (`sym/solve.c`): `solve(f, x)` gives the real solutions of f = 0 as a set `{-2, 2}`: roots of polynomials (rational roots, then the
   quadratic formula), polynomials in x^k, quotients (without the zeros of the denominator, sums of fractions over one line), and polynomials in
   exp(x), ln(x), a power or a root of x. When the second argument is a name that stands for nothing it is this; with matrices it is still the
   linear system. Not built: systems of equations (in the algebra domain), complex roots, cubic and quartic formulas, trigonometric equations.
11. **Free symbols**: `:symbols on` (or `matlog eval --symbols`) makes a name that stands for nothing a symbol everywhere, so `x + y` is the
   expression `x + y`; off by default so that a mistyped name stays an error. A number defined later hides the symbol.

## 9d. Milestone 5 tasks

Goal: quantities with units in the algebra domain: `5 km + 300 m`, `12 N * 3 m` and `60 km / 1 h` are calculated with the dimensions checked,
and a result can be shown in any unit of the same dimension (`to`), also reduced to a named unit (`N*m` to `J`).

### Decisions this milestone is built on

- A quantity is an exact value and a dimension (seven SI base exponents). Its value is kept in the coherent SI unit, so `5 km` is 5000 with the dimension
  of a length, and a result is shown in the unit that was asked for (`to`), else in SI base units, reduced to a named unit when there is one (`kg·m²/s²` is `J`).
- Units are looked up by whole name first, then as an SI prefix and a unit (`min` is a minute, not milli-inch; `kmin` is not a unit). Symbols (`km`, `µs`,
  `Ω`) and singular long names (`kilometer`) are known; plural forms are not.
- A name that is a variable shadows a unit of the same name, so `m = 3` still works (the unit is reachable as `1 m` only when it is not defined).
- `+ -` and comparisons need equal dimensions (an error otherwise); `* /` multiply and divide dimensions; `^` takes an integer power (and `sqrt` an even one).
  A number added to a quantity is an error, except 0. Dimensionless results are plain numbers again.
- Offset units (degrees Celsius, Fahrenheit) need a separate absolute/difference type and are not in this milestone.
- Dimensions also work in matrices later; for now matrices stay plain numbers.

### Tasks

1. **Dimensions and the unit table** (`math/unit.h`): the dimension vector and its algebra, formatting in SI base units, the table of units with exact
   factors, SI prefixes, lookup, the name of a derived unit. Done when: tests check the algebra, the formatting, prefix rules and every factor. (done)
2. **Quantity values** (`math/value.h`): a value kind with an exact number and a dimension; add, subtract, multiply, divide, power, compare with the
   dimension rules; the error kinds `dimension_mismatch` and `unknown_unit`. Done when: tests check the
   rules, mismatches and exactness. (done: exact quantities only; approximate ones are `not_exact`)
3. **Syntax**: `5 km` needs nothing new (a number followed by a name is already a product, the evaluator reads the name as a unit); `to` converts
   (`3 ft to m`, `60 km/h to m/s`) as the loosest binary operator, the node `to` with the value and the unit expression. The word is only an operator
   after an operand, elsewhere `to` is a name. No synonym `in`: it is the unit inch. (done)
4. **Evaluator**: units resolve after variables and functions; `to` finds the factor; `sqrt`, `abs`, `min`, `max` on quantities. (done: `to` checks the dimension and keeps the value in SI; the unit to show it in comes with the display)
5. **Display**: `= 5000 m` and `= 5 km` after `to`; derived names (`J`); `ml_dim_format` for the rest; JSON type `quantity` with `value` and `unit`. (done: a converted value keeps the unit text and its SI scale, `value` in JSON is the text with the unit; quantities can be assigned to variables)
6. **Completion, highlighting hooks and steps**: unit names in completion, the conversion shown as steps (`3 ft = 3 × 0.3048 m`). (done: the steps; `ml_complete_math` offers variables, functions, constants and unit names for the word at the cursor; wiring it into the editor and the highlight class `unit` come with milestone 9 and the completion work of section 4.3)
7. **Dimensional analysis**: `dim(x)` shows the dimension of a result. (done: `dim (5 km)` is `1 m`, the dimension as one coherent SI unit)

## 9e. Milestone 6 tasks

Goal: the boolean and the lambda domain evaluate their lines, as the algebra domain does: results next to the line, definitions that flow down,
errors with a byte range.

### Decisions this milestone is built on

- Each domain has its own engine behind one small interface (`logic/logic.h`): a line goes in with the definitions made above it, and comes out as a text, an
  optional block, an error (an enum with a machine readable name) and at most one definition. The results list keeps the definitions in order and a
  line sees the ones before it, so editing a line runs the lines below it again.
- A definition is stored fully expanded, so looking up a name never recurses. Free names of a definition stay free (no late binding).
- Boolean: at most 10 names per line, the value is computed as a bit vector over all assignments (every node of the expression is a vector, one pass), the
  result is the minimal sum of products (Quine-McCluskey, essential primes, then a greedy cover) unless the written form is shorter.
- Lambda: terms are de Bruijn terms in an append-only node list; reduction is normal order, one leftmost outermost step at a time, with explicit stacks for
  the search, the substitution, the shifting, the printing and the resolving of names. Nothing recurses.

### Tasks

1. **Shared parts** (`logic/logic.h`): the error enum with code and text, the definition list, the line result. (done)
2. **Boolean domain** (`logic/bool.h`): lexer for all spellings, a shunting yard parser, truth bits, minimal form, `table`, definitions. (done)
3. **Lambda domain** (`logic/lambda.h`): parser with λ contexts on a stack, Church numerals, de Bruijn resolution with definitions, normal order
   reduction, capture avoiding printing, `trace`. (done)
4. **Wiring** (`session/results.h`): lines of both domains run in the results; the lambda and boolean pages of the documentation. (done)
5. **Ghost, highlighting, steps and the command line**: the ghost preview in both domains (`ml_ghost` takes the domain), the classes `logic` and
   `binder` (own scanners in `ui/highlight.c`, entries in all built in themes and their `.theme` files), the steps of a line (`F3`: the beta steps of a
   lambda term, the truth table of a boolean expression) and `matlog eval|steps --domain boolean|lambda` with the domain name as the JSON `type`.
   (done)
6. Later: export of both domains, syntax completion of the names defined above. Equivalence proofs for the proof checker (section 4.12) build on the
   boolean engine.

## 10. Out of scope (for now)

- Own GUI window, GUI toolkit, network features, plugins.

## 11. Decisions log

- Terminal only (VT/ANSI, unicode, braille plots); Windows first behind a `platform` layer.
- Exact rationals + bignum; floats only on request.
- Plain text `.mlog` session files, one session per file, results recomputed on open.
- Modal input (normal/insert/note) plus a separate domain mode; variables shared across domains.
- Steps on request for any line, past or present, regenerated by re-evaluation.
- ASCII and unicode input equivalent, ASCII auto-rewritten to pretty unicode inline.
- Integrals start with polynomials/tables; PGA both 2D and 3D via a generic signature.
- Includes are read-only and acyclic.
- Syntax highlighting is driven by the lexer tokens, with themes as data (built in plus user `.theme` files) and colour levels from 24 bit down to none.
- Export (latex, MathML, markdown, png, pdf) goes through one document tree built from the parsed AST and the results, with a writer per format; png and pdf are written by the program itself.
- Proofs are written as lines of a `proof` domain in the same session files and checked by a small trusted core, never taken on trust; what cannot be decided is reported as unchecked.
- Every feature is available from the command line (batch front end with text and JSON output, stable error codes and exit codes), so scripts and AI agents can use the tool without the screen.
- Documentation is written once in `docs/` as plain pages with live, checked examples; it is read in the tool (browsable, searchable, compiled in) and built into a static website with the same interactivity, and it feeds the command line help.
- A built in interactive tutorial in two stages (usage of the tool, then the syntax); lessons are data, they only observe the real editor, and each is tested by playing the model answer.

## 12. Open questions

Defaults chosen in section 9.1 (approximate numbers, implicit multiplication, 0^0 as an error) can be changed before the milestone 2 tasks are done.

Milestones 1 to 5 are done. What each leaves for later is written in its own section: milestone 3 (the matrix steps are built, a typeset display of
results is), milestone 4 (see its tasks 8 to 11 for what was added after the first version, and what is still missing there), milestone 5 (unit
names in the completion and the highlighting of math lines, which do not exist yet). Built beyond the milestones: the command line interface (eval,
run, lex, parse, steps, help, syntax, schema, commands, version), line operations and undo, blocks and folds, Markdown in notes, the free form editor
with selection, mouse, pages and the settings file for folds (section 3). Highlighting and themes (section 4.10) are built except the settings file entry for the theme, the OSC 11 background query and the highlight classes of the other domains; an empty editor shows rotating hints (`ui/hint`). Next: export, documentation, the tutorial, proofs
(sections 4.9 to 4.14), or milestone 6 (lambda calculus and boolean algebra).
