#include <matlog/math/plotdata.h>

#include <matlog/core/str.h>

void ml_plot_owned_init (MlPlotOwned *p)
{
	p->kind  = ML_PLOT_NONE;
	p->count = 0;
	p->lo    = 0;
	p->hi    = 0;
	MlDoubleVec_init (&p->xs);
	MlDoubleVec_init (&p->ys);
}

void ml_plot_owned_free (MlPlotOwned *p)
{
	MlDoubleVec_free (&p->xs);
	MlDoubleVec_free (&p->ys);
	p->kind = ML_PLOT_NONE;
}

MlResult ml_plot_owned_set (MlPlotOwned *p, const MlPlotData *data)
{
	MlResult r = ML_OK;

	MlDoubleVec_clear (&p->xs);
	MlDoubleVec_clear (&p->ys);

	p->kind  = data->kind;
	p->count = data->count;
	p->lo    = data->lo;
	p->hi    = data->hi;

	for (uint64_t i = 0; i < data->n && r == ML_OK; i++)
	{
		r = MlDoubleVec_push (&p->ys, data->ys [i]);

		if (r == ML_OK && data->xs != NULL)
		{
			r = MlDoubleVec_push (&p->xs, data->xs [i]);
		}
	}

	return r;
}

MlResult ml_plot_summary (MlStr *out, const MlPlotData *data)
{
	MlResult r = ML_OK;

	if (data->kind == ML_PLOT_HISTOGRAM)
	{
		r = ml_str_appendf (
			out,
			"histogram of %llu values in %llu bins",
			(unsigned long long)data->count,
			(unsigned long long)data->n
		);
	}

	else if (data->kind == ML_PLOT_SCATTER)
	{
		r = ml_str_appendf (
			out,
			"scatter plot of %llu points",
			(unsigned long long)data->count
		);
	}

	else
	{
		r = ml_str_appendf (
			out,
			"line plot of %llu points",
			(unsigned long long)data->count
		);
	}

	return r;
}
