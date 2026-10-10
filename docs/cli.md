id: cli
title: The command line
summary: Evaluate, run, export and read the docs from scripts.
keywords: cli, scripts, eval, run, lex, parse, steps, json, exit code, stdin
see: export, settings, fileformat, docs

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
matlog config list|get|set|path  the settings
matlog examples [NAME]     the worked examples, or one as a file
matlog vars FILE[:LINE]    the names defined at a line, with types and values
matlog docs list           the pages of the documentation
matlog help [CMD]          the usage
```

`--format json` gives machine readable output, `--session FILE` makes the
definitions of a session visible to `eval`, `--symbols` turns symbols on.
`--domain NAME` makes `eval` and `steps` read the text in a domain; the
algebra, `boolean` and `lambda` domains are evaluated. The lines of one
call share their definitions, and in JSON the `type` of a result is the
domain name:

```code
matlog eval --domain boolean "p = a & b" "p | a"
matlog eval --domain lambda "succ = λn f x. f (n f x)" "succ 2"
matlog steps --domain lambda "(λx. x x) (λy. y)"
```

`steps` gives the beta steps of a lambda term and the truth table of a
boolean expression.
An argument `-` reads standard input. The exit code is 0 for success, 1
when an expression was an error, 2 for a usage error and 3 for a file
error.

```example
1/3 + 1/6
=> 1/2
```
