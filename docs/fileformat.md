id: fileformat
title: The session file
summary: The .mlog format.
keywords: mlog, file, format, session, include, note, domain
see: editor, cli

A session is a UTF-8 text file with one entry per line:

```code
# some free text            a note
m:matrix A = [[1,2],[3,4]]  a math line in the given domain
include "constants.mlog"    another session, run in place
```
Anything else is kept as it is, so loading and saving never loses data.
Lines end with LF or CRLF when reading and LF when writing. The folds of a
session are not in the file; they are kept in the
[settings file](themes).

A note that starts with `#` and a space is a heading when exported:
`# # Title`.
