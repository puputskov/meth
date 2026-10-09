#ifndef MATLOG_PLATFORM_FS_H
#define MATLOG_PLATFORM_FS_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>

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

#endif /* MATLOG_PLATFORM_FS_H */
