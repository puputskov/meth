#ifndef METH_SESSION_SETTINGS_H
#define METH_SESSION_SETTINGS_H

#include <meth/core/err.h>
#include <meth/core/slice.h>
#include <meth/core/str.h>

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

/*
 * The index-th entry (counting from 0) of section: its key and value, and the
 * 1 based number of its line. found is 0 past the last entry. Used to read a
 * whole section, such as the key bindings.
 */
typedef struct MlSettingEntry
{
	int32_t  found;
	MlSlice  key;
	MlSlice  value;
	uint64_t line;
} MlSettingEntry;

MlSettingEntry ml_settings_text_entry (
	MlSlice  text,
	MlSlice  section,
	uint64_t index
);

/* Appends to out the text without the entry of key in section. */
MlResult ml_settings_text_remove (
	MlStr   *out,
	MlSlice text,
	MlSlice section,
	MlSlice key
);

/* Appends the path of the settings file to out (ML_ERR_IO: no folder). */
MlResult ml_settings_path (MlStr *out);

/* Appends the whole text of the file to out (nothing when it is missing). */
MlResult ml_settings_read (MlStr *out);

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

/*
 * Loads the texts of the language the settings name (en without one): the
 * built in English texts and the file texts/<language>.mltext over them.
 * Call it at the start of the program and when the file is read again.
 */
MlResult ml_texts_use_settings (uint64_t *bad);

#endif /* METH_SESSION_SETTINGS_H */
