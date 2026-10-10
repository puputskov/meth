#ifndef METH_PLATFORM_CONFIG_H
#define METH_PLATFORM_CONFIG_H

#include <meth/core/err.h>
#include <meth/core/slice.h>
#include <meth/core/str.h>

/*
 * The settings folder of the user: %APPDATA%\meth on Windows,
 * $XDG_CONFIG_HOME/meth or ~/.config/meth on Linux, ~/Library/Application
 * Support/meth on macOS. The environment variable METH_CONFIG replaces it
 * (which also keeps tests and scripts away from the real one). The folder is
 * not created here, see ml_fs_make_dirs. ML_ERR_IO when no place is known.
 */
MlResult ml_config_dir (MlStr *out);

/* The folder the system puts settings in, without the override. */
MlResult ml_config_os_dir (MlStr *out);

#endif /* METH_PLATFORM_CONFIG_H */
