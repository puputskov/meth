#include <matlog/tutorial/lessons.h>

#include <matlog/core/vec.h>
#include <matlog/platform/key.h>
#include <matlog/session/session.h>
#include <matlog/texts/texts.h>
#include <matlog/ui/keymap.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The lessons are the section [lessons] of the texts catalog (texts/
 * en.mltext), built into lists of steps whenever the texts are new:
 *
 *	notes.title = Writing and notes
 *	notes.stage = 1
 *	notes.2.check = key
 *	notes.2.arg   = Up
 *	notes.2.text  = Press Up to go back to your note.
 *	notes.2.hint  = The key with the arrow pointing up.
 *	notes.2.answer = <Up>
 *
 * A lesson is an id with a title and a stage; its steps are numbered from 1
 * and a step needs `check` (the kind of check, see lessons.h) and `text`,
 * and has `arg` for the kinds that take one, `param` and `param2` where the
 * check takes text, the `answer` and an optional `hint`. The lessons come in
 * the order of the file, those of stage 1 first. A step the program cannot
 * make sense of (a check or an argument it does not know) is left out.
 */

ML_VEC_DEFINE (StepVec, MlStep)
ML_VEC_DEFINE (LessonVec, MlLesson)
ML_VEC_DEFINE (IdVec, MlStr)

/* A lesson found in the catalog: where its steps are in the list. */
typedef struct Span
{
	uint64_t first;
	uint64_t count;
	int32_t  stage;
	uint64_t id; /* index in ids */
} Span;

ML_VEC_DEFINE (SpanVec, Span)

static StepVec   steps;
static LessonVec lessons;
static IdVec     ids;
static uint64_t  built = 0; /* the generation of the texts they come from */

static const char *const check_names [] = {
	[ML_CHECK_ACTION]    = "action",
	[ML_CHECK_KEY]       = "key",
	[ML_CHECK_NOTES]     = "notes",
	[ML_CHECK_RESULT]    = "result",
	[ML_CHECK_ERROR]     = "error",
	[ML_CHECK_VALUE]     = "value",
	[ML_CHECK_DOMAIN]    = "domain",
	[ML_CHECK_STATE]     = "state",
	[ML_CHECK_STATE_NOT] = "state_not",
	[ML_CHECK_COMMAND]   = "command",
	[ML_CHECK_STEPS]     = "steps",
	[ML_CHECK_WATCH]     = "watch"
};

static const char *const state_names [] = {
	[ML_TUT_MATH]    = "math",
	[ML_TUT_NOTE]    = "note",
	[ML_TUT_COMMAND] = "command",
	[ML_TUT_SELECT]  = "select",
	[ML_TUT_HELP]    = "help"
};

/* The text of the entry lessons.<id>[.<step>].<name>, NULL if none. */
static const char *entry (const char *id, uint64_t step, const char *name)
{
	char code [160];

	if (step > 0)
	{
		snprintf (
			code,
			sizeof code,
			"lessons.%s.%u.%s",
			id,
			(uint32_t)step,
			name
		);
	}

	else
	{
		snprintf (code, sizeof code, "lessons.%s.%s", id, name);
	}

	return ml_text_find (code);
}

static int32_t index_in (const char *const *names, uint64_t count, MlSlice word)
{
	int32_t found = -1;

	for (uint64_t i = 0; i < count && found < 0; i++)
	{
		if (
			   names [i] != NULL
			&& ml_slice_equal (word, ml_slice_cstr (names [i]))
		)
		{
			found = (int32_t)i;
		}
	}

	return found;
}

/* The number the argument of a step stands for; ok is cleared if none. */
static int32_t step_arg (MlCheck check, const char *word, int32_t *ok)
{
	MlSlice w   = ml_slice_cstr (ml_choose (word != NULL, word, ""));
	int32_t arg = 0;

	*ok = 1;

	if (check == ML_CHECK_ACTION)
	{
		MlAction a = ml_action_from_name (w);

		arg = (int32_t)a;
		*ok = a != ML_ACT_COUNT;
	}

	else if (check == ML_CHECK_KEY)
	{
		MlKeyParse p = ml_key_parse_any (w);

		arg = (int32_t)p.key.code;
		*ok = p.result == ML_OK;
	}

	else if (check == ML_CHECK_DOMAIN)
	{
		arg = -1;

		for (uint32_t d = 0; d < ML_DOMAIN_COUNT && arg < 0; d++)
		{
			const char *name = ml_domain_name ((MlDomain)d);

			if (ml_slice_equal (w, ml_slice_cstr (name)))
			{
				arg = (int32_t)d;
			}
		}

		*ok = arg >= 0;
	}

	else if (check == ML_CHECK_STATE || check == ML_CHECK_STATE_NOT)
	{
		arg = index_in (state_names, ml_count (state_names), w);
		*ok = arg >= 0;
	}

	else if (check == ML_CHECK_WATCH)
	{
		arg = ml_slice_equal (w, ml_slice_cstr ("on"));
		*ok = arg || ml_slice_equal (w, ml_slice_cstr ("off"));
	}

	else if (check == ML_CHECK_NOTES)
	{
		arg = atoi (ml_choose (word != NULL, word, "0"));
	}

	return arg;
}

/* Step n of the lesson from the catalog; ok is 0 when it cannot be used. */
static MlStep step_of (const char *id, uint64_t n, int32_t *ok)
{
	const char *kind  = entry (id, n, "check");
	const char *text  = entry (id, n, "text");
	int32_t    check  = -1;
	int32_t    good   = kind != NULL && text != NULL;
	MlStep     st;

	memset (&st, 0, sizeof st);

	if (good)
	{
		check = index_in (
			check_names,
			ml_count (check_names),
			ml_slice_cstr (kind)
		);
		good  = check >= 0;
	}

	if (good)
	{
		st.check  = (MlCheck)check;
		st.arg    = step_arg (st.check, entry (id, n, "arg"), &good);
		st.text   = text;
		st.param  = entry (id, n, "param");
		st.param2 = entry (id, n, "param2");
		st.hint   = entry (id, n, "hint");
		st.answer = entry (id, n, "answer");
	}

	*ok = good;

	return st;
}

/* 1 when no entry before number upto has the same lesson id (len bytes). */
static int32_t is_new_id (const char *name, uint64_t len, uint64_t upto)
{
	int32_t fresh = 1;

	for (uint64_t i = 0; i < upto && fresh; i++)
	{
		const char *other = ml_text_nth_name ("lessons", i);

		fresh = !(strncmp (other, name, len) == 0 && other [len] == '.');
	}

	return fresh;
}

/* The steps of the lesson with this id, pushed to steps. */
static MlResult collect_steps (const char *id, Span *span)
{
	MlResult r = ML_OK;

	span->first = steps.len;
	span->count = 0;

	for (uint64_t n = 1; r == ML_OK && entry (id, n, "check") != NULL; n++)
	{
		int32_t ok = 0;
		MlStep  st = step_of (id, n, &ok);

		if (ok)
		{
			r = StepVec_push (&steps, st);
			span->count++;
		}
	}

	return r;
}

/* The lessons of the catalog, in the order of the file. */
static MlResult collect (SpanVec *spans)
{
	MlResult r       = ML_OK;
	uint64_t entries = ml_text_count ("lessons");

	for (uint64_t i = 0; i < entries && r == ML_OK; i++)
	{
		const char *name = ml_text_nth_name ("lessons", i);
		const char *dot  = strchr (name, '.');
		uint64_t    len  = ml_choose (dot != NULL, (uint64_t)(dot - name), 0);

		if (dot != NULL && is_new_id (name, len, i))
		{
			MlStr       id;
			Span        span;
			const char *stage;

			memset (&span, 0, sizeof span);
			ml_str_init (&id);
			r = ml_str_append (&id, ml_slice (name, len));

			if (r == ML_OK)
			{
				r = collect_steps (ml_str_cstr (id), &span);
			}

			if (r == ML_OK)
			{
				stage      = entry (ml_str_cstr (id), 0, "stage");
				span.stage = atoi (ml_choose (stage != NULL, stage, "1"));
				span.id    = ids.len;
				r          = IdVec_push (&ids, id);
			}

			if (r == ML_OK && span.count > 0)
			{
				r = SpanVec_push (spans, span);
			}

			else if (r != ML_OK)
			{
				ml_str_free (&id);
			}
		}
	}

	return r;
}

static void free_all (void)
{
	for (uint64_t i = 0; i < ids.len; i++)
	{
		ml_str_free (&ids.data [i]);
	}

	IdVec_free (&ids);
	StepVec_free (&steps);
	LessonVec_free (&lessons);
}

/* The lessons of one stage after another, each in the order of the file. */
static MlResult make_lessons (const SpanVec *spans)
{
	MlResult r = ML_OK;

	for (int32_t stage = 0; stage < 10 && r == ML_OK; stage++)
	{
		for (uint64_t i = 0; i < spans->len && r == ML_OK; i++)
		{
			Span        sp = spans->data [i];
			const char *id = ml_str_cstr (ids.data [sp.id]);
			const char *title = entry (id, 0, "title");
			MlLesson    l = {
				.id         = id,
				.title      = ml_choose (title != NULL, title, id),
				.stage      = sp.stage,
				.step_count = sp.count,
				.steps      = steps.data + sp.first
			};

			if (ml_choose (sp.stage > 9, 9, sp.stage) == stage)
			{
				r = LessonVec_push (&lessons, l);
			}
		}
	}

	return r;
}

/* Builds the lessons from the catalog again when the texts have changed. */
static void ensure_built (void)
{
	if (built != ml_texts_generation ())
	{
		SpanVec  spans;
		MlResult r;

		SpanVec_init (&spans);
		free_all ();
		r = collect (&spans);

		if (r == ML_OK)
		{
			r = make_lessons (&spans);
		}

		if (r != ML_OK)
		{
			free_all ();
		}

		SpanVec_free (&spans);
		built = ml_texts_generation ();
	}
}

void ml_lessons_release (void)
{
	free_all ();
	built = UINT64_MAX;
}

uint64_t ml_lesson_count (void)
{
	ensure_built ();

	return lessons.len;
}

MlLesson ml_lesson_at (uint64_t index)
{
	MlLesson none = {
		.id         = "",
		.title      = "",
		.stage      = 0,
		.step_count = 0,
		.steps      = NULL
	};

	ensure_built ();

	return ml_choose (index < lessons.len, lessons.data [index], none);
}

uint64_t ml_lesson_find (MlSlice id)
{
	uint64_t found = ml_lesson_count ();

	for (uint64_t i = 0; i < lessons.len && found == lessons.len; i++)
	{
		if (ml_slice_equal (id, ml_slice_cstr (lessons.data [i].id)))
		{
			found = i;
		}
	}

	return found;
}

uint64_t ml_lesson_first (int32_t stage)
{
	uint64_t found = ml_lesson_count ();

	for (uint64_t i = 0; i < lessons.len && found == lessons.len; i++)
	{
		if (lessons.data [i].stage == stage)
		{
			found = i;
		}
	}

	return found;
}
