#include <matlog/platform/config.h>
#include <matlog/platform/path.h>

#include <stdlib.h>

MlResult ml_config_os_dir (MlStr *out)
{
	const char *base = getenv ("APPDATA");
	MlResult   r     = ML_ERR_IO;

	if (base != NULL && base [0] != '\0')
	{
		r = ml_path_join (out, ml_slice_cstr (base), ml_slice_cstr ("matlog"));
	}

	return r;
}
