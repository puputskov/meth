id: commands
title: Commands
summary: The commands typed after Ctrl+P.
keywords: command line, colon, write, open, quit, mode, steps, symbols, theme, colors, export, delete, yank, move, copy
see: editor, examples, settings, keys, themes, export

Ctrl+P opens the command line (type the command without the colon). Tab
completes what you type: the names of commands and the values of their
first argument.

# Files
```command
:w [path]      write the session (:write)
:wq [path]     write and quit (:x)
:q             quit; :q! quits without saving
:open path     open a file; :open! discards the changes
:export latex|mathml|markdown [body] [errors] [3-12] [path]
```

# The session
```command
:mode name     the domain of new blocks (algebra, lambda, ...)
:pga 2|3       the same for the pga domains
:steps         show or hide the steps of the line
:symbols on|off  names with no value are symbols
:undo  :redo
```

# Lines
A range comes before the command: `3`, `3,5`, `%` for all, `.` for the
current line and `$` for the last (1 based).
```command
:12            go to line 12
:3,5delete     delete the lines (:d)
:3,5yank       copy them (:y)
:3,5move 10    move them after line 10 (:m)
:3,5copy 10    copy them after line 10 (:t)
```

# Examples
```command
:examples          the table of contents of the examples
:example NAME      put the lines of an example after the cursor line
```
See [examples](examples).

# Settings
```command
:set [name [value]]  list, show or change a setting
:config [path|reload]  the settings page, the file, read it again
```
See [settings](settings).

# Keys
```command
:keys          the key bindings in use (also :map)
:map KEY ACTION  bind a key; written to the settings file
:unmap KEY     remove the binding of a key
```
See [keys](keys) for the actions and how to write a key.

# The look
```command
:theme [name|reload]
:colors [24bit|256|16|off]
```
See [themes](themes) for what they do.
