#include <meth/platform/config.h>

#include <stdlib.h>

MlResult ml_config_dir (MlStr *out)
{
	const char *over = getenv ("METH_CONFIG");
	MlResult   r     = ML_OK;

	if (over != NULL && over [0] != '\0')
	{
		r = ml_str_append (out, ml_slice_cstr (over));
	}

	else
	{
		r = ml_config_os_dir (out);
	}

	return r;
}
