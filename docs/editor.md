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
{ACTION_BLOCK_NEW}      start a block of the domain used last
{ACTION_BLOCK_PICK}     choose the domain of the new block first
{ACTION_NOTE_TOGGLE}    turn the line into a note, or back
{ACTION_DOMAIN_PREV}    the previous domain of the line
{ACTION_DOMAIN_NEXT}    the next domain of the line
Esc                     write the line into the session
```
A terminal that cannot report the key of a new block may send another key
for it; `:keys` lists every key of every action.

# The watch window
`{ACTION_WATCH}` (or `:watch`) shows a panel at the right of the editor with every name
that is defined at the line of the cursor: variables, functions and the
definitions of the boolean and lambda domains, each with its type and its
value. Move the cursor up and the panel shows the names as that line left
them; a star marks what the line itself defined or changed. A terminal under
70 columns has no room for it. `matlog vars FILE:LINE` prints the same.

# Plots
A line that gives a plot (`hist`, `scatter`, `plot`, see [plots](plots))
shows only a one line summary. `{ACTION_PLOT}` (or `:plot`) on that line draws it in the
panel at the right, and the same key again hides it. See [plots](plots).

# Moving and selecting
```keys
Left Right Up Down Home End   move the cursor, across lines too
Ctrl+Left/Right               move by words
Page Up / Page Down           move a page of lines
{ACTION_GOTO_FIRST}           the first line
{ACTION_GOTO_LAST}            the last line
{ACTION_BLOCK_PREV}           the previous block
{ACTION_BLOCK_NEXT}           the next block
Shift + a move key            select text, also across lines
{ACTION_SELECT_ALL}           select everything
{ACTION_SELECT_LINE}          select the line
```
The mouse puts the cursor where you click, drags select, and the wheel
moves three lines.

# Editing
```keys
{ACTION_COPY}          copy (the line, without a selection)
{ACTION_CUT}           cut
{ACTION_PASTE}         paste
{ACTION_LINES_UP}      move the selected lines up
{ACTION_LINES_DOWN}    move the selected lines down
{ACTION_FOLD}          fold or unfold the block
{ACTION_UNDO}          undo
{ACTION_REDO}          redo
{ACTION_STEPS}         show or hide the steps of the line
{ACTION_SAVE}          save
{ACTION_QUIT}          quit
{ACTION_COMMAND}       the command line, see commands
```
Backspace at the start of a line joins it with the line before; Backspace
on the first, empty line of a document turns a block into a note. The
[commands](commands) page lists what {ACTION_COMMAND} accepts.
