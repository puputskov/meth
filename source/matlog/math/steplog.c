#include <matlog/math/steplog.h>

void ml_steplog_init (MlStepLog *log)
{
	ml_str_init (&log->text);
	log->status = ML_OK;
}

void ml_steplog_free (MlStepLog *log)
{
	ml_str_free (&log->text);
}

void ml_steplog_line (MlStepLog *log, MlSlice line)
{
	if (log != NULL && log->status == ML_OK)
	{
		log->status = ml_str_append (&log->text, line);

		if (log->status == ML_OK)
		{
			log->status = ml_str_append (&log->text, ml_slice_cstr ("\n"));
		}
	}
}

void ml_steplog_trim (MlStepLog *log)
{
	if (
		   log != NULL
		&& log->text.len > 0
		&& log->text.data [log->text.len - 1] == '\n'
	)
	{
		ml_str_erase (&log->text, log->text.len - 1, 1);
	}
}
