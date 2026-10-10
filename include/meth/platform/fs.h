#ifndef METH_PLATFORM_FS_H
#define METH_PLATFORM_FS_H

#include <meth/core/err.h>
#include <meth/core/slice.h>
#include <meth/core/str.h>

/* All paths are UTF-8. */

/* 1 when path names an existing file. */
int32_t ml_fs_exists (MlSlice path);

/* Appends the whole content of the file to out. */
MlResult ml_fs_read_file (MlStr *out, MlSlice path);

/*
 * Replaces the file with data. The data is written to "<path>.tmp" first and
 * moved over the target, so a crash never leaves a half written file.
 */
MlResult ml_fs_write_file_atomic (MlSlice path, MlSlice data);

MlResult ml_fs_remove (MlSlice path);

/*
 * Creates the directory and the ones above it that are missing. A directory
 * that is already there is not an error.
 */
MlResult ml_fs_make_dirs (MlSlice path);

/*
 * Appends the names of the files in a folder whose name ends with suffix
 * (suffix may be empty), each followed by a newline, in no particular order.
 * A folder that is not there lists nothing.
 */
MlResult ml_fs_list (MlStr *out, MlSlice dir, MlSlice suffix);

/*
 * A number that changes whenever the file is written (its modification time
 * and size), 0 when there is no such file. Meant for noticing that a file
 * was edited, not for ordering files by age.
 */
uint64_t ml_fs_stamp (MlSlice path);

#endif /* METH_PLATFORM_FS_H */
