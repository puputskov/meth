#ifndef METH_TUTORIAL_TUTORIAL_H
#define METH_TUTORIAL_TUTORIAL_H

#include <meth/session/results.h>
#include <meth/session/session.h>
#include <meth/tutorial/lessons.h>
#include <meth/ui/keymap.h>

/*
 * The tutorial (plan section 4.9): a state that watches the editor and
 * moves from step to step when the step's check passes. It has no input of
 * its own and does not know the editor: after every key the editor gives it
 * a view of what happened (MlTutorialView), and the tutorial decides whether
 * the step is done. Wrong tries are never errors; after a few keys that did
 * not do it the hint of the step is shown.
 *
 * A step is only done by a key pressed after it began: the key that
 * finished the step before has been seen already, and skip_next makes the
 * tutorial ignore the next key (the Enter of the command that started it).
 */

#define ML_TUTORIAL_HINT_KEYS 8

typedef struct MlTutorialView
{
	MlAction         action;       /* of the key just pressed */
	MlKeyCode        key_code;
	uint32_t         key_mods;
	int32_t          state;        /* an MlInputState */
	MlDomain         domain;       /* of the line being typed, else new lines */
	const MlSession  *session;
	const MlResults  *results;
	MlSlice          last_command; /* the text of the command run last */
	uint64_t         commands_run; /* how many commands were run in all */
	int32_t          steps_open;
	int32_t          watch;
} MlTutorialView;

typedef struct MlTutorial
{
	int32_t  active;
	uint64_t lesson;
	uint64_t step;
	uint64_t keys;           /* keys since the step began */
	uint64_t start_lines;    /* the lines the session had when it began */
	uint64_t start_commands;
	int32_t  skip_next;      /* the next key started it: not an answer */
	int32_t  lesson_done;    /* a lesson was finished by the last key */
	int32_t  finished;       /* the last lesson of the tutorial is done */
} MlTutorial;

void ml_tutorial_init (MlTutorial *t);

/* Starts at the first step of a lesson (a view of the editor as it is). */
void ml_tutorial_start (
	MlTutorial           *t,
	uint64_t             lesson,
	const MlTutorialView *v
);

void ml_tutorial_stop (MlTutorial *t);

/* Looks at the editor after a key; moves on when the step is done. */
void ml_tutorial_observe (MlTutorial *t, const MlTutorialView *v);

/* Skips the step, or goes back one (the first step of a lesson stays). */
void ml_tutorial_skip (MlTutorial *t, const MlTutorialView *v);
void ml_tutorial_back (MlTutorial *t, const MlTutorialView *v);

/* The step being done; its text is empty when the tutorial is not active. */
MlStep ml_tutorial_step (const MlTutorial *t);
MlLesson ml_tutorial_lesson (const MlTutorial *t);

/* 1 when the hint of the step should be shown. */
int32_t ml_tutorial_hint_due (const MlTutorial *t);

/* " Tutorial  2 of 15: Math lines  (step 3 of 6)". */
MlResult ml_tutorial_title (MlStr *out, const MlTutorial *t);

/*
 * The keys of the model answer of the step, appended to out in order. A key
 * that stands for an action, <ACTION_UNDO>, is the first key of the action in
 * the key table km. ML_ERR_PARSE for a script that has a key that is not one.
 */
ML_VEC_DEFINE (MlKeyVec, MlKey)
MlResult ml_tutorial_answer (MlKeyVec *out, const MlKeymap *km, MlStep step);

#endif /* METH_TUTORIAL_TUTORIAL_H */
