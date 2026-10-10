#ifndef MATLOG_CORE_NAMES_H
#define MATLOG_CORE_NAMES_H

#include <matlog/core/slice.h>

/*
 * Interned names. Every distinct name has one number, so two names are the
 * same name when their numbers are equal, and tables of names (the words of
 * the syntax, the built in functions, the bindings of an environment) are
 * compared and indexed by integers instead of by their characters.
 *
 * ml_name makes the number of a text (the same text always gives the same
 * number, from 1 on); ml_name_find only looks, and gives ML_NAME_NONE for a
 * text that was never interned, which is the answer to "is this name bound /
 * a word / a function" without growing the table. Use ml_name where a name
 * is created (a definition, the table of words) and ml_name_find where it is
 * looked up. One thread only.
 */

typedef uint32_t MlName;

#define ML_NAME_NONE 0

MlName ml_name (MlSlice text);
MlName ml_name_find (MlSlice text);

/* The text of a name, empty for ML_NAME_NONE or a number never given. */
MlSlice ml_name_text (MlName n);

/* How many names there are; the numbers in use are 1 to this. */
uint32_t ml_name_count (void);

/*
 * A number that changes when the names are released: whoever keeps tables
 * indexed by name numbers builds them again when it differs from the one
 * they saw.
 */
uint64_t ml_names_generation (void);

/* Forgets every name (for the end of a test run); numbers become invalid. */
void ml_names_release (void);

#endif /* MATLOG_CORE_NAMES_H */
