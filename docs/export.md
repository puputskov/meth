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
- Other domains and includes are written as code. Errors are left out
  unless `errors` is given.

# Words after the format
- `body` only the body, to paste into another document.
- `errors` the errors of lines too.
- `3-12` only those lines (1 based).
- a path to write to instead.

`matlog export FILE FORMAT` does the same from the [command line](cli).
