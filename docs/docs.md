id: docs
title: The documentation
summary: How to read and search these pages.
keywords: help, search, docs, pages, viewer, documentation
see: cli

These pages are part of the program. In the editor `:help` opens them,
`:help topic` goes to a page and `:help words` searches. From the command
line:

```command
matlog docs list               the pages: id, title, summary
matlog docs show ID            one page as text (--format json|markdown)
matlog docs search WORDS...    the pages that match, the best first
matlog docs check              check links and the examples
```

# Searching
Every word must be found in the page, in its title, keywords, summary,
headings or text. The last word may be the beginning of a word, and case
does not matter. Matches in the title count most, then keywords and
headings, then the text.

# The pages
A page is a text file `docs/NAME.md` with a header (`id`, `title`,
`summary`, `keywords`, `see`) and a body in Markdown with links to other
pages written `[words](id)`. The examples in the pages are checked
against the evaluator by `matlog docs check`, so a page cannot show a
result that is wrong. `python tools/gen_docs.py` compiles the pages into
the program.
