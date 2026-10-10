#include <meth/platform/fs.h>

/*
 * A page has no files. Every call fails or finds nothing, so the code that
 * reads settings, includes and sessions from disk behaves as on a machine
 * with an empty disk. The page keeps sessions in the browser instead.
 */

int32_t ml_fs_exists (MlSlice path)
{
	(void)path;
	return 0;
}

MlResult ml_fs_read_file (MlStr *out, MlSlice path)
{
	(void)out;
	(void)path;
	return ML_ERR_IO;
}

MlResult ml_fs_write_file_atomic (MlSlice path, MlSlice data)
{
	(void)path;
	(void)data;
	return ML_ERR_IO;
}

MlResult ml_fs_remove (MlSlice path)
{
	(void)path;
	return ML_ERR_IO;
}

MlResult ml_fs_make_dirs (MlSlice path)
{
	(void)path;
	return ML_ERR_IO;
}

MlResult ml_fs_list (MlStr *out, MlSlice dir, MlSlice suffix)
{
	(void)out;
	(void)dir;
	(void)suffix;
	return ML_OK;
}

uint64_t ml_fs_stamp (MlSlice path)
{
	(void)path;
	return 0;
}
