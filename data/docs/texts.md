id: texts
title: Texts and languages
summary: The messages, hints and lessons are files; change a word or add a language without a rebuild.
keywords: texts, language, translation, mltext, localization, message, hint, typo, catalog
see: settings, tutorial

Every text the tool shows you is data in a text file, not part of the program:
the error messages, the messages of the editor, the hints in an empty editor,
the summaries of the keys, the settings and the commands of the command line, its help, and the lessons of the tutorial. The program only holds
the stable code of a text, such as `errors.div_zero`. So a mistake in a word, a
new hint or a translation needs no rebuild.

# The catalog file
A catalog is a plain UTF-8 file, `.mltext`, in the form of the settings file:

```code
# a comment
[errors]
div_zero = division by zero
undefined_name_of = undefined name '{name}'
[hints]
13 = {F9} does something that a hint should tell
    on two lines
```

`[section]` gives its name to the entries after it; the code of an entry is
`section.name`. A line that starts with a space or a tab continues the entry
above it. `\n` is a line break and `\\` a backslash. `{name}` is a place the
program fills in; in the hints the parts in braces are drawn as keys, and
`{ACTION_UNDO}` is the key of the action `undo` (see [keys](keys)).

A long text that has its own line breaks and indents, such as the help of the
command line (`matlog help`), is a block. The entry ends in `= <<`, the lines
that follow are taken as they are (spaces, `#` and backslashes included), and
a line that is only `>>` ends it:

```code
[help]
lex = <<
matlog lex TEXT [--format text|json]

Prints the tokens of TEXT.
>>
```

# Your own texts
The English texts are built into the program (`texts/en.mltext` in the
sources). To change them, make the folder `texts` in the settings folder (see
[settings](settings)) and put a file there named after the language, `en.mltext`
for English or `de.mltext` for a translation, and set the language:

```code
:set language de
```

An entry in your file replaces the one with the same code; an entry that the
program does not know yet is added, which is how a new hint is made (the next
number in `[hints]`). What your file does not have stays English, and a code
that no file has is shown as the code itself, so a missing text is easy to
find. A line in your file that is not an entry is ignored.

# The lessons
The lessons of the [tutorial](tutorial) are catalog files too, one lesson per
file: `lessons/en/01-notes.mltext`, `02-math.mltext` and so on in the sources.
The number in the name sets the order. A file holds the section `[lessons]` of
one lesson, an id with a title and a stage, and its steps numbered from 1:

```code
[lessons]
mine.title = My lesson
mine.stage = 2

mine.1.check = steps
mine.1.text = Show the steps of a line.
mine.1.hint = Move to a line and press the key for the steps.
mine.1.answer = <ACTION_STEPS>

mine.2.check = state
mine.2.arg = help
mine.2.text = Open the help.
mine.2.answer = <ACTION_HELP>
```

A step has `check`: what the editor must show for the step to be done (one
of `action`, `key`, `notes`, `result`, `error`, `value`, `domain`, `state`,
`state_not`, `command`, `steps`, `watch`), `arg` for some of them (the name
of an action, a key, a domain or a state, `on` or `off`, a number), `param` and
`param2` for the checks that compare text, the `text` you read, an optional
`hint` and the `answer`: the keys of a model answer, where a key between `<`
and `>` is pressed. To add a lesson, put such a file in the folder `lessons/en`
(or the folder of your language) of the settings folder; the files are read in
the order of their names, after the built in ones, and a step the program does
not understand is left out. The tutorial plays your lesson after the lessons of
its stage.
