id: export
title: Export
summary: A session as LaTeX, MathML or Markdown.
keywords: export, latex, tex, mathml, html, markdown, md, typeset, document
see: commands, cli

`:export latex` writes the session as a LaTeX file next to it
(`physics.mlog` gives `physics.tex`); `mathml` writes an HTML page with
MathML (`physics.html`) and `markdown` a Markdown file (`physics.md`).
An existing file is not replaced; `:export!` does.

# What is written
- A note is a paragraph, the note lines of one block are one paragraph.
  A note `# # Title` is a heading and `# ---` a rule; the Markdown marks
  become the formatting of the format.
- A math line of the algebra domain is typeset from its syntax tree: real
  fractions, roots, powers and matrices. Its result follows as `=` or `≈`.
  A definition shows no result.
  Complex numbers and quaternions are part of it (`2 i`, `3 j`), and so are
  vectors and the results of the statistics functions.
- A line of the boolean or the lambda domain is typeset from its text: `∧ ∨ ¬
  ⊕ → ↔` for the operators in any spelling, `⊤` and `⊥`, `λx.` and applications
  with a thin space. A truth table or the steps of a reduction under a line
  are written as code.
- The lines of the other domains and the includes are written as code. A line
  with an error is code too; the error is left out unless `errors` is given.

# Words after the format
- `body` only the body, to paste into another document.
- `errors` the errors of lines too.
- `3-12` only those lines (1 based).
- a path to write to instead.

`matlog export FILE FORMAT` does the same from the [command line](cli).
