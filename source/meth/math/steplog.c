#include <meth/math/steplog.h>

#include <meth/core/utf8.h>

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

void ml_steplog_align (MlStepLog *log)
{
	MlStr    out;
	uint64_t width = 0;
	MlResult r     = ML_OK;

	ml_str_init (&out);

	if (log != NULL && log->status == ML_OK)
	{
		/* the width of the longest text in front of a tab, in code points */
		for (uint64_t pos = 0; pos < log->text.len; pos++)
		{
			uint64_t start = pos;
			uint64_t tab   = log->text.len;

			while (pos < log->text.len && log->text.data [pos] != '\n')
			{
				tab = ml_choose (
					log->text.data [pos] == '\t' && tab == log->text.len,
					pos,
					tab
				);
				pos++;
			}

			if (tab < log->text.len && tab < pos)
			{
				uint64_t w = ml_utf8_count (
					ml_slice_sub (ml_str_view (log->text), start, tab - start)
				);

				width = ml_choose (w > width, w, width);
			}
		}

		for (uint64_t pos = 0; r == ML_OK && pos < log->text.len; pos++)
		{
			char c = log->text.data [pos];

			if (c == '\t')
			{
				uint64_t col = 0;
				uint64_t at  = pos;

				/* the text of this line in front of the tab */
				while (at > 0 && log->text.data [at - 1] != '\n')
				{
					at--;
				}

				col = ml_utf8_count (
					ml_slice_sub (ml_str_view (log->text), at, pos - at)
				);

				for (uint64_t k = col; r == ML_OK && k < width + 3; k++)
				{
					r = ml_str_append (&out, ml_slice_cstr (" "));
				}
			}

			else
			{
				r = ml_str_append (
					&out,
					ml_slice_sub (ml_str_view (log->text), pos, 1)
				);
			}
		}

		if (r == ML_OK)
		{
			ml_str_free (&log->text);
			log->text = out;
		}

		else
		{
			ml_str_free (&out);
			log->status = r;
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
