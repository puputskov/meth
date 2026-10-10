id: texts
title: Texts and languages
summary: The messages, hints and lessons are files; change a word or add a language without a rebuild.
keywords: texts, language, translation, mltext, localization, message, hint, typo, catalog
see: settings, tutorial

Every text the tool shows you is data in a text file, not part of the program:
the error messages, the hints in an empty editor, and (as they are moved) the
help texts of commands and the lessons of the tutorial. The program only holds
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
