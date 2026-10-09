#ifndef MATLOG_PLATFORM_CONFIG_H
#define MATLOG_PLATFORM_CONFIG_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>

/*
 * The settings folder of the user: %APPDATA%\matlog on Windows,
 * $XDG_CONFIG_HOME/matlog or ~/.config/matlog on Linux, ~/Library/Application
 * Support/matlog on macOS. The environment variable MATLOG_CONFIG replaces it
 * (which also keeps tests and scripts away from the real one). The folder is
 * not created here, see ml_fs_make_dirs. ML_ERR_IO when no place is known.
 */
MlResult ml_config_dir (MlStr *out);

/* The folder the system puts settings in, without the override. */
MlResult ml_config_os_dir (MlStr *out);

#endif /* MATLOG_PLATFORM_CONFIG_H */
