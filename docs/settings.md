id: settings
title: Settings
summary: The settings file, the settings you can change and how.
keywords: settings, config, set, mlcfg, ghost, highlight, theme, colors, preferences
see: themes, keys, commands, cli

All settings are kept in one plain text file, `settings.mlcfg`, in the
settings folder: `%APPDATA%\matlog` on Windows, `$XDG_CONFIG_HOME/matlog`
or `~/.config/matlog` on Linux, and `~/Library/Application Support/matlog`
on macOS. `MATLOG_CONFIG` names another folder. A missing file is a valid
configuration: every setting has a default.

The file has `[sections]` and `key = value` lines, and `#` starts a comment
line. A line that does not make sense is ignored, and matlog keeps what it
does not know when it writes the file:

```code
[display]
theme = light
ghost = off

[keys]
Ctrl+K = command
```

# The settings
```command
theme      the colour theme: dark, light, contrast, mono or a name.theme file
colors     24bit, 256, 16 or off; the default follows the terminal
highlight  on or off: the syntax colours
ghost      on or off: the dim preview of the result while you type
```

# Changing them
```command
:set               list the settings and their values
:set ghost         show one
:set ghost off     change it; the file is written
:config            this page
:config path       where the file is
:config reload     read the file again, the key bindings too
```

`matlog config` does the same without the screen: `matlog config list`,
`matlog config get ghost`, `matlog config set ghost off` and
`matlog config path`. The key bindings have their own section, see
[keys](keys); the folds of your sessions are kept in `[folds]`.
