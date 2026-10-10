id: tutorial
title: The tutorial
summary: Learn the tool by doing, inside the real editor, in small lessons.
keywords: tutorial, lesson, learn, start, teach, beginner, practice
see: editor, commands, keys

The tutorial teaches matlog by doing. A panel at the top of the screen shows
the lesson and the task; you do the task in the real editor, below it, and
the panel moves on when it sees that you did. Nothing is simulated: the
tutorial only watches the keys and the session, so what you learn is exactly
what the tool does.

# Starting and leaving
```command
:tutorial          start, at the first lesson not finished yet
:tutorial syntax   start at the lessons about what to type
:tutorial next     skip the step (F6)
:tutorial back     go back a step (Shift+F6)
:tutorial show     type the model answer for the step
:tutorial reset    forget the finished lessons
:tutorial off      leave the tutorial; the session stays
```
`matlog --tutorial` starts it from the shell. A command that needs the
session saved first asks; `:tutorial!` starts anyway.

# The lessons
The first stage is the tool: notes, math lines, moving, selecting and undo,
domains and commands, results and steps, help and the watch window. The
second stage is what to type: numbers and operators, names and functions,
reading errors, matrices, derivatives and limits, units, complex numbers,
quaternions and statistics, boolean algebra and lambda calculus.

A wrong try is never an error. After a few keys the panel shows a hint, and
the finished lessons are remembered, so the next start continues after them.
See [the editor](editor) and [the key bindings](keys).
