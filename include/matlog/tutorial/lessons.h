#ifndef MATLOG_TUTORIAL_LESSONS_H
#define MATLOG_TUTORIAL_LESSONS_H

#include <matlog/core/slice.h>

/*
 * The lessons of the tutorial (plan section 4.9), as data: the section
 * [lessons] of the texts catalog (texts/en.mltext, see lessons.c), so a lesson
 * is added or a word mended in a file without a rebuild. A lesson is a
 * list of steps; a step says what to do and how to tell that it was done by
 * watching the real editor: the key that was pressed, the lines of the
 * session and their results, the state of the editor. Nothing here knows how
 * the editor works, so adding a lesson is adding rows.
 *
 * Stage 1 teaches the tool (the keys and the commands), stage 2 the syntax,
 * with exercises that have a target: the check evaluates what was written,
 * so any spelling that gives the result is accepted.
 *
 * The model answer of a step is a script of keys: letters are typed as they
 * are, a key between angle brackets is pressed (`<Enter>`, `<Ctrl+Enter>`,
 * `<Up>`, `<F3>`), and `<<` is a `<`. The tests play the answers of every
 * step in a real editor and fail when a lesson cannot be finished; the
 * command :tutorial show types the answer for the user to read.
 */

typedef enum MlCheck
{
	ML_CHECK_ACTION = 0, /* the key pressed has the action arg (MlAction) */
	ML_CHECK_KEY,        /* the key pressed is the key arg (MlKeyCode) */
	ML_CHECK_NOTES,      /* the session has at least arg notes */
	ML_CHECK_RESULT,     /* a line written in this step has the result param */
	ML_CHECK_ERROR,      /* a line of this step is an error (code, or any) */
	ML_CHECK_VALUE,      /* the expression param has the value param2 now */
	ML_CHECK_DOMAIN,     /* the domain of the line or the block is arg */
	ML_CHECK_STATE,      /* the editor is in the state arg (MlInputState) */
	ML_CHECK_STATE_NOT,  /* it is not */
	ML_CHECK_COMMAND,    /* a command that begins with param was run */
	ML_CHECK_STEPS,      /* the steps of a line are shown */
	ML_CHECK_WATCH       /* the watch window is shown (arg 1) or hidden (0) */
} MlCheck;

/* The states of the editor the checks name (the order of MlInputState). */
#define ML_TUT_MATH    0
#define ML_TUT_NOTE    1
#define ML_TUT_COMMAND 2
#define ML_TUT_SELECT  3
#define ML_TUT_HELP    4

typedef struct MlStep
{
	const char *text;   /* what to do, plain text */
	MlCheck    check;
	int32_t    arg;
	const char *param;
	const char *param2;
	const char *hint;   /* shown after a few keys that did not do it */
	const char *answer; /* the keys of the model answer */
} MlStep;

typedef struct MlLesson
{
	const char   *id;
	const char   *title;
	int32_t      stage;      /* 1: the tool, 2: the syntax */
	uint64_t     step_count;
	const MlStep *steps;
} MlLesson;

/*
 * Frees the lessons that are built from the texts and kept for the life of the
 * program; they are built again when asked for. Meant for the end of a test
 * run, so that the memory still in use is the memory that leaks.
 */
void ml_lessons_release (void);

uint64_t ml_lesson_count (void);
MlLesson ml_lesson_at (uint64_t index);

/* The index of the lesson with the id; ml_lesson_count () when none. */
uint64_t ml_lesson_find (MlSlice id);

/* The index of the first lesson of a stage, ml_lesson_count () if none. */
uint64_t ml_lesson_first (int32_t stage);

#endif /* MATLOG_TUTORIAL_LESSONS_H */
