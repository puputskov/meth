id: cli
title: The command line
summary: Evaluate, run, export and read the docs from scripts.
keywords: cli, scripts, eval, run, lex, parse, steps, json, exit code, stdin
see: export, fileformat, docs

`matlog` without a command opens the editor (`matlog notes.mlog` opens a
file). With a command it prints on standard output and exits.

```command
matlog eval EXPR...        evaluate expressions, one result each
matlog run FILE            evaluate every line of a session file
matlog lex TEXT            the tokens of a text
matlog parse TEXT          the syntax tree of a text
matlog steps EXPR          the steps of an expression
matlog export FILE FORMAT  a session as a document
matlog theme [NAME]        the built in themes
matlog docs list           the pages of the documentation
matlog help [CMD]          the usage
```

`--format json` gives machine readable output, `--session FILE` makes the
definitions of a session visible to `eval`, `--symbols` turns symbols on.
An argument `-` reads standard input. The exit code is 0 for success, 1
when an expression was an error, 2 for a usage error and 3 for a file
error.

```example
1/3 + 1/6
=> 1/2
```
