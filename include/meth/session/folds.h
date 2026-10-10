#ifndef METH_SESSION_FOLDS_H
#define METH_SESSION_FOLDS_H

#include <meth/session/session.h>

/*
 * Which blocks of a session are folded is part of the view, not of the file
 * (see session.h). It is kept in the settings file, settings.mlcfg in the
 * settings folder (platform/config.h), in the section
 *
 *	[folds]
 *	notes/physics.meth = 0 12 40
 *
 * one entry per session path, as it was given to the program, with the first
 * line (0 based) of every folded block of more than one line. The lines whose
 * plot is shown in the plot panel are listed in the same entry with a p in
 * front (`0 12 40 p3 p7`), so one entry keeps the view of the session.
 * Everything else in the file, comments and other sections included, is left
 * as it is. An
 * entry for a line that does not start a block of several lines is ignored,
 * so a file that was edited elsewhere never breaks anything.
 */

/* The entry value of the session: "0 12 40", empty when nothing is folded. */
MlResult ml_folds_format (MlStr *out, const MlSession *s);

/* Folds the blocks the settings file has for the path of the session. */
MlResult ml_folds_load (MlSession *s);

/*
 * Writes the folds of the session into the settings file. The file is only
 * touched when its entry for this session changes; ML_OK when there is no
 * settings folder or nothing to write.
 */
MlResult ml_folds_save (const MlSession *s);

#endif /* METH_SESSION_FOLDS_H */
