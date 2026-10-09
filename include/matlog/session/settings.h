#ifndef MATLOG_SESSION_SETTINGS_H
#define MATLOG_SESSION_SETTINGS_H

#include <matlog/core/err.h>
#include <matlog/core/slice.h>
#include <matlog/core/str.h>

/*
 * Entries of the settings file (plan section 4.15): `key = value` lines
 * grouped in `[sections]`, `#` starts a comment line. Everything else in the
 * file, comments and entries this program does not know included, is kept
 * when an entry is written. The file is settings.mlcfg in the folder of
 * ml_config_dir; a missing file is an empty one.
 *
 * The text functions work on a string and know nothing about files, so they
 * are tested without any. The file functions never fail the caller for a file
 * that is not there; a file that cannot be written is ML_ERR_IO.
 */

typedef struct MlSetting
{
	int32_t found;
	MlSlice value; /* a view into the text that was searched */
} MlSetting;

/* The value of key in section, found is 0 when there is no such entry. */
MlSetting ml_settings_text_get (MlSlice text, MlSlice section, MlSlice key);

/*
 * Appends to out the text with the entry of key in section set to value: an
 * existing entry is replaced in place, a new one goes to the end of its
 * section, and a section that is missing is made at the end of the text.
 */
MlResult ml_settings_text_set (
	MlStr   *out,
	MlSlice text,
	MlSlice section,
	MlSlice key,
	MlSlice value
);

/* Appends to out the text without the entry of key in section. */
MlResult ml_settings_text_remove (
	MlStr   *out,
	MlSlice text,
	MlSlice section,
	MlSlice key
);

/* Appends the value of an entry of the file to out; found is 0 if none. */
MlResult ml_settings_get (
	MlStr    *out,
	MlSlice  section,
	MlSlice  key,
	int32_t  *found
);

/* Writes an entry into the file (only when it changes the file). */
MlResult ml_settings_set (MlSlice section, MlSlice key, MlSlice value);

/* Removes an entry from the file (only when there is one to remove). */
MlResult ml_settings_remove (MlSlice section, MlSlice key);

#endif /* MATLOG_SESSION_SETTINGS_H */
