#include <meth/platform/config.h>

/* There is no settings folder in a page. */
MlResult ml_config_os_dir (MlStr *out)
{
	(void)out;
	return ML_ERR_IO;
}
