id: editor
title: The editor and its keys
summary: Typing, moving, selecting, blocks, folds and undo.
keywords: keys, keyboard, shortcuts, block, domain, fold, select, undo, note, math line, ctrl+enter
see: commands, keys, fileformat

The editor works like a plain text editor: the cursor is always in a line,
and typing edits it. A new line is a note by default. A math line shows its
result after it as you type, and the result of the line being typed is
shown dim as a preview.

All control keys can be changed, see [keys](keys).

# Notes and blocks
A note is free text; the Markdown marks `**bold**`, `*italic*`,
`<u>underline</u>`, `~~strike~~` and `` `code` `` are drawn as such. A
run of math lines in one domain is a block. Enter on an empty line of a
block ends it.

```keys
Ctrl+Enter     start a block of the domain used last
Shift+Enter    the same, for terminals that cannot report Ctrl+Enter
Ctrl+B         choose the domain of the new block first
Ctrl+N         turn the line into a note, or back
Alt+Left/Right the previous or next domain of the line
Esc            write the line into the session
```

# Moving and selecting
```keys
Left Right Up Down Home End   move the cursor, across lines too
Ctrl+Left/Right               move by words
Page Up / Page Down           move a page of lines
Ctrl+Home / Ctrl+End          first and last line
Ctrl+Up / Ctrl+Down           previous and next block
Shift + a move key            select text, also across lines
Ctrl+A / Ctrl+L               select everything / the line
```
The mouse puts the cursor where you click, drags select, and the wheel
moves three lines.

# Editing
```keys
Ctrl+C Ctrl+X Ctrl+V   copy, cut, paste (the line, without a selection)
Alt+Up / Alt+Down      move the selected lines
Alt+Enter              fold or unfold the block
Ctrl+Z                 undo
Ctrl+Y or Ctrl+Shift+Z redo
F3                     show or hide the steps of the line
Ctrl+S                 save
Ctrl+Q                 quit
Ctrl+P                 the command line, see commands
```
Backspace at the start of a line joins it with the line before; Backspace
on the first, empty line of a document turns a block into a note. The
[commands](commands) page lists what Ctrl+P accepts.
