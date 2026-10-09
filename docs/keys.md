id: keys
title: Key bindings
summary: The actions of the editor, their default keys and how to change them.
keywords: keys, bindings, map, unmap, shortcut, rebind, keyboard, settings, mlcfg
see: editor, commands

Every control key of the editor is bound to a named action. The table below
lists the actions with the keys they have when nothing is changed;
`:keys` shows the table as it is now.

```keys
undo         Ctrl+Z                         undo the last change
redo         Ctrl+Shift+Z, Ctrl+Y, Ctrl+R   redo what was undone
block-new    Ctrl+Enter, Shift+Enter        start a block, last domain
block-pick   Ctrl+B                         start a block, pick the domain
note-toggle  Ctrl+N                         make the line a note, or back
domain-next  Alt+Right                      the next domain of the line
domain-prev  Alt+Left                       the previous domain of the line
lines-down   Alt+Down                       move the selected lines down
lines-up     Alt+Up                         move the selected lines up
fold         Alt+Enter                      fold or unfold the block
block-next   Ctrl+Down                      go to the next block
block-prev   Ctrl+Up                        go to the previous block
goto-first   Ctrl+Home                      go to the first line
goto-last    Ctrl+End                       go to the last line
select-line  Ctrl+L                         select the line
select-all   Ctrl+A                         select everything
copy         Ctrl+C                         copy the selection or the line
cut          Ctrl+X                         cut the selection or the line
paste        Ctrl+V                         paste
command      Ctrl+P                         open the command line
save         Ctrl+S                         save the session
quit         Ctrl+Q                         save and quit
steps        F3                             show or hide the line's steps
help         F1                             open the help
```

# Changing a key
`:map KEY ACTION` binds a key, `:unmap KEY` removes the binding of a key.
Both are written to the `[keys]` section of the settings file, one line per
key, and are read again at the next start:

```code
[keys]
Ctrl+K = command
Ctrl+Z = none
```

`none` removes a key, so the example gives Ctrl+K the command line and
leaves Ctrl+Z without an action. A key bound twice keeps the later line.
A line that is not a key or not an action is ignored, and the first one is
shown in the status line when the program starts.

# Writing a key
Modifiers joined with `+`, then the key: `Ctrl+Shift+Z`, `Alt+Left`,
`F3`, `Ctrl+Enter`. Letters are not case sensitive. The names of the other
keys are Enter, Esc, Tab, Backspace, Delete, Insert, Left, Right, Up, Down,
Home, End, PageUp, PageDown and F1 to F12.

Only keys that cannot be typed as text can be bound: a key needs Ctrl or
Alt, or is a function key, or is Shift+Enter. Plain characters, the
arrows, Enter, Esc, Backspace and Shift with a movement key stay as they
are.
