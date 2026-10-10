#include <meth/tutorial/tutorial.h>

#include <meth/core/utf8.h>
#include <meth/ui/ghost.h>

void ml_tutorial_init (MlTutorial *t)
{
	memset (t, 0, sizeof *t);
}

static void begin_step (MlTutorial *t, const MlTutorialView *v)
{
	t->keys           = 0;
	t->start_lines    = v->session->lines.len;
	t->start_commands = v->commands_run;
	t->skip_next      = 0;
}

void ml_tutorial_start (
	MlTutorial           *t,
	uint64_t             lesson,
	const MlTutorialView *v
)
{
	ml_tutorial_init (t);
	t->active = lesson < ml_lesson_count ();
	t->lesson = lesson;
	begin_step (t, v);
}

void ml_tutorial_stop (MlTutorial *t)
{
	t->active = 0;
}

MlLesson ml_tutorial_lesson (const MlTutorial *t)
{
	return ml_lesson_at (t->lesson);
}

MlStep ml_tutorial_step (const MlTutorial *t)
{
	MlStep none;

	memset (&none, 0, sizeof none);
	none.text = "";

	return ml_choose (
		t->active && t->step < ml_lesson_at (t->lesson).step_count,
		ml_lesson_at (t->lesson).steps [t->step],
		none
	);
}

/* ---- the checks -------------------------------------------------------- */

/* 1 when a line written since the step began has the result. */
static int32_t some_result (
	const MlTutorial       *t,
	const MlTutorialView   *v,
	MlStep                 step,
	int32_t                errors
)
{
	int32_t found = 0;

	for (uint64_t i = t->start_lines; i < v->session->lines.len && !found; i++)
	{
		MlLineView   line = ml_session_get (v->session, i);
		MlResultView res  = ml_results_get (v->results, i);

		if (line.kind == ML_LINE_MATH && errors)
		{
			found = (
				   res.kind == ML_RESULT_ERROR
				&& (
					   step.param == NULL
					|| step.param [0] == '\0'
					|| ml_slice_equal (res.code, ml_slice_cstr (step.param))
				)
			);
		}

		else if (line.kind == ML_LINE_MATH)
		{
			found = (
				   res.kind == ML_RESULT_VALUE
				&& ml_slice_equal (res.text, ml_slice_cstr (step.param))
			);
		}
	}

	return found;
}

/* 1 when the expression has the value in the environment at the end. */
static int32_t has_value (
	const MlTutorial     *t,
	const MlTutorialView *v,
	MlStep               step
)
{
	int32_t ok = 0;

	if (v->session->lines.len > t->start_lines)
	{
		MlStr   out;
		MlGhost g;

		ml_str_init (&out);
		g = ml_ghost (
			&out,
			v->results,
			ML_DOMAIN_ALGEBRA,
			v->results->lines.len,
			ml_slice_cstr (step.param)
		);
		ok = (
			   g.result == ML_OK
			&& g.shown
			&& ml_slice_equal (ml_str_view (out), ml_slice_cstr (step.param2))
		);
		ml_str_free (&out);
	}

	return ok;
}

static uint64_t note_count (const MlSession *s)
{
	uint64_t n = 0;

	for (uint64_t i = 0; i < s->lines.len; i++)
	{
		n += ml_session_get (s, i).kind == ML_LINE_NOTE;
	}

	return n;
}

static int32_t passes (
	const MlTutorial     *t,
	const MlTutorialView *v,
	MlStep               step
)
{
	int32_t ok = 0;

	if (step.check == ML_CHECK_ACTION)
	{
		ok = v->action == (MlAction)step.arg;
	}

	else if (step.check == ML_CHECK_KEY)
	{
		ok = v->key_code == (MlKeyCode)step.arg && v->key_mods == 0;
	}

	else if (step.check == ML_CHECK_NOTES)
	{
		ok = note_count (v->session) >= (uint64_t)step.arg;
	}

	else if (step.check == ML_CHECK_RESULT)
	{
		ok = some_result (t, v, step, 0);
	}

	else if (step.check == ML_CHECK_ERROR)
	{
		ok = some_result (t, v, step, 1);
	}

	else if (step.check == ML_CHECK_VALUE)
	{
		ok = has_value (t, v, step);
	}

	else if (step.check == ML_CHECK_DOMAIN)
	{
		ok = (int32_t)v->domain == step.arg;
	}

	else if (step.check == ML_CHECK_STATE)
	{
		ok = v->state == step.arg;
	}

	else if (step.check == ML_CHECK_STATE_NOT)
	{
		ok = v->state != step.arg;
	}

	else if (step.check == ML_CHECK_COMMAND)
	{
		MlSlice want = ml_slice_cstr (step.param);

		ok = (
			   v->commands_run > t->start_commands
			&& v->last_command.len >= want.len
			&& ml_slice_equal (
				ml_slice_sub (v->last_command, 0, want.len),
				want
			)
		);
	}

	else if (step.check == ML_CHECK_STEPS)
	{
		ok = v->steps_open;
	}

	else if (step.check == ML_CHECK_WATCH)
	{
		ok = v->watch == step.arg;
	}

	return ok;
}

/* ---- moving on --------------------------------------------------------- */

static void next_step (MlTutorial *t, const MlTutorialView *v)
{
	MlLesson lesson = ml_lesson_at (t->lesson);

	t->lesson_done = 0;

	if (t->step + 1 < lesson.step_count)
	{
		t->step++;
	}

	else
	{
		t->lesson_done = 1;

		if (t->lesson + 1 < ml_lesson_count ())
		{
			t->lesson++;
			t->step = 0;
		}

		else
		{
			t->finished = 1;
			t->active   = 0;
		}
	}

	begin_step (t, v);
}

void ml_tutorial_observe (MlTutorial *t, const MlTutorialView *v)
{
	t->lesson_done = 0;

	if (t->active && t->skip_next)
	{
		t->skip_next = 0;
	}

	else if (t->active)
	{
		t->keys++;

		if (passes (t, v, ml_tutorial_step (t)))
		{
			next_step (t, v);
		}
	}
}

void ml_tutorial_skip (MlTutorial *t, const MlTutorialView *v)
{
	if (t->active)
	{
		next_step (t, v);
	}
}

void ml_tutorial_back (MlTutorial *t, const MlTutorialView *v)
{
	t->lesson_done = 0;

	if (t->active && t->step > 0)
	{
		t->step--;
	}

	else if (t->active && t->lesson > 0)
	{
		t->lesson--;
		t->step = ml_lesson_at (t->lesson).step_count - 1;
	}

	begin_step (t, v);
}

int32_t ml_tutorial_hint_due (const MlTutorial *t)
{
	MlStep step = ml_tutorial_step (t);

	return (
		   t->active
		&& t->keys >= ML_TUTORIAL_HINT_KEYS
		&& step.hint != NULL
		&& step.hint [0] != '\0'
	);
}

MlResult ml_tutorial_title (MlStr *out, const MlTutorial *t)
{
	MlLesson lesson = ml_lesson_at (t->lesson);

	return ml_str_appendf (
		out,
		" Tutorial  %u of %u: %s  (step %u of %u)",
		(uint32_t)(t->lesson + 1),
		(uint32_t)ml_lesson_count (),
		lesson.title,
		(uint32_t)(t->step + 1),
		(uint32_t)lesson.step_count
	);
}

/* ---- the model answer -------------------------------------------------- */

/* The keys of a script whose placeholders are already expanded. */
static MlResult script_keys (MlKeyVec *out, MlSlice s)
{
	MlResult r   = ML_OK;
	uint64_t pos = 0;

	while (pos < s.len && r == ML_OK)
	{
		MlKey key = {
			.code = ML_KEY_CHAR,
			.cp   = 0,
			.mods = 0,
			.col  = 0,
			.row  = 0
		};

		if (s.data [pos] == '<' && pos + 1 < s.len && s.data [pos + 1] == '<')
		{
			key.cp = '<';
			pos   += 2;
		}

		else if (s.data [pos] == '<')
		{
			uint64_t   end = pos + 1;
			MlKeyParse p;

			while (end < s.len && s.data [end] != '>')
			{
				end++;
			}

			p = ml_key_parse_any (ml_slice_sub (s, pos + 1, end - pos - 1));
			r = ml_choose (
				p.result == ML_OK && end < s.len,
				ML_OK,
				ML_ERR_PARSE
			);

			if (r == ML_OK)
			{
				key = p.key;
			}

			pos = end + 1;
		}

		else
		{
			MlDecoded d = ml_utf8_decode (ml_slice_sub (s, pos, s.len - pos));

			key.cp = d.cp;
			pos   += ml_choose (d.size > 0, d.size, 1);
		}

		if (r == ML_OK)
		{
			r = MlKeyVec_push (out, key);
		}
	}

	return r;
}

MlResult ml_tutorial_answer (MlKeyVec *out, const MlKeymap *km, MlStep step)
{
	MlStr    script;
	MlResult r;

	ml_str_init (&script);
	r = ml_keymap_expand (
		&script,
		km,
		ml_slice_cstr (ml_choose (step.answer != NULL, step.answer, "")),
		'<',
		'>',
		1
	);

	if (r == ML_OK)
	{
		r = script_keys (out, ml_str_view (script));
	}

	ml_str_free (&script);
	return r;
}
