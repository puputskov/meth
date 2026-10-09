#include <matlog/platform/config.h>
#include <matlog/platform/path.h>

#include <stdlib.h>

MlResult ml_config_os_dir (MlStr *out)
{
	const char *xdg  = getenv ("XDG_CONFIG_HOME");
	const char *home = getenv ("HOME");
	MlResult   r     = ML_ERR_IO;
	MlStr      base;

	ml_str_init (&base);

	if (xdg != NULL && xdg [0] == '/')
	{
		r = ml_path_join (out, ml_slice_cstr (xdg), ml_slice_cstr ("matlog"));
	}

	else if (home != NULL && home [0] != '\0')
	{
#ifdef __APPLE__
		r = ml_path_join (
			&base,
			ml_slice_cstr (home),
			ml_slice_cstr ("Library/Application Support")
		);
#else
		r = ml_path_join (
			&base,
			ml_slice_cstr (home),
			ml_slice_cstr (".config")
		);
#endif

		if (r == ML_OK)
		{
			r = ml_path_join (out, ml_str_view (base), ml_slice_cstr ("matlog"));
		}
	}

	ml_str_free (&base);
	return r;
}
