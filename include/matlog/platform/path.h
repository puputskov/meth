#ifndef MATLOG_PLATFORM_PATH_H
#define MATLOG_PLATFORM_PATH_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>

/*
 * Pure string operations on paths. Both '/' and '\' are accepted as
 * separators; joined paths are written with '/', which every supported
 * OS API accepts.
 */

/* Directory part of path without the trailing separator ("" when there is none). */
MlSlice ml_path_dir (MlSlice path);

/* File name part of path (everything after the last separator). */
MlSlice ml_path_name (MlSlice path);

/* 1 when path starts with a separator, a drive ("C:") or is a UNC path. */
int32_t ml_path_is_absolute (MlSlice path);

/*
 * Appends dir + '/' + name to out. An empty dir appends just name, and an
 * absolute name replaces dir.
 */
MlResult ml_path_join (MlStr *out, MlSlice dir, MlSlice name);

#endif /* MATLOG_PLATFORM_PATH_H */
