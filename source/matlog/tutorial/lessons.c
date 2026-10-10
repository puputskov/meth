#include <matlog/tutorial/lessons.h>

#include <matlog/platform/key.h>
#include <matlog/ui/keymap.h>

/* ---- stage 1: the tool ------------------------------------------------- */

static const MlStep notes_steps [] =
{
	{
		.text   = "Welcome. A new line is a note, free text. Type a sentence "
		          "and press Enter.",
		.check  = ML_CHECK_NOTES,
		.arg    = 1,
		.hint   = "Type some letters, then press the Enter key.",
		.answer = "My first note<Enter>"
	},
	{
		.text   = "Press Up to go back to your note. The cursor moves across "
		          "lines like in any text editor.",
		.check  = ML_CHECK_KEY,
		.arg    = ML_KEY_UP,
		.hint   = "The key with the arrow pointing up.",
		.answer = "<Up>"
	},
	{
		.text   = "Press End to jump to the end of the line, then Enter: a "
		          "new line starts below it.",
		.check  = ML_CHECK_KEY,
		.arg    = ML_KEY_ENTER,
		.hint   = "End is next to Home. Then the Enter key.",
		.answer = "<End><Enter>"
	}
};

static const MlStep math_steps [] =
{
	{
		.text   = "Press {ACTION_BLOCK_NEW}. A math line starts, in a block "
		          "of the domain used last (algebra at first).",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_BLOCK_NEW,
		.hint   = "Hold Ctrl and press Enter. Some terminals send Shift and "
		          "Enter together for it.",
		.answer = "<ACTION_BLOCK_NEW>"
	},
	{
		.text   = "Type 1/3 + 1/6. The dim text after it is a preview of the "
		          "result. Press Enter to keep the line.",
		.check  = ML_CHECK_RESULT,
		.param  = "1/2",
		.hint   = "Type the characters 1/3 + 1/6 exactly, then Enter.",
		.answer = "1/3 + 1/6<Enter>"
	},
	{
		.text   = "Lines work in order: a name you define is known below it. "
		          "Type a = 12 and press Enter.",
		.check  = ML_CHECK_RESULT,
		.param  = "12",
		.hint   = "Type a = 12 and press Enter.",
		.answer = "a = 12<Enter>"
	},
	{
		.text   = "Use it: type a/8 + 1 and press Enter.",
		.check  = ML_CHECK_RESULT,
		.param  = "5/2",
		.hint   = "a/8 + 1 with a being 12 gives 5/2.",
		.answer = "a/8 + 1<Enter>"
	},
	{
		.text   = "A line that cannot be computed shows a message in red. "
		          "Type 1/0 and press Enter.",
		.check  = ML_CHECK_ERROR,
		.param  = "div_zero",
		.hint   = "One divided by zero.",
		.answer = "1/0<Enter>"
	},
	{
		.text   = "Enter on an empty line of a block ends the block, and a "
		          "note starts below it. Press Enter.",
		.check  = ML_CHECK_STATE,
		.arg    = ML_TUT_NOTE,
		.hint   = "The line you are on is empty. Press Enter on it.",
		.answer = "<Enter>"
	}
};

static const MlStep moving_steps [] =
{
	{
		.text   = "{ACTION_GOTO_FIRST} goes to the first line of the session.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_GOTO_FIRST,
		.answer = "<ACTION_GOTO_FIRST>"
	},
	{
		.text   = "{ACTION_GOTO_LAST} goes to the last line.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_GOTO_LAST,
		.answer = "<ACTION_GOTO_LAST>"
	},
	{
		.text   = "{ACTION_BLOCK_PREV} jumps to the block before, "
		          "{ACTION_BLOCK_NEXT} to the next one. Press "
		          "{ACTION_BLOCK_PREV}.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_BLOCK_PREV,
		.answer = "<ACTION_BLOCK_PREV>"
	},
	{
		.text   = "{ACTION_BLOCK_NEXT} jumps to the next block.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_BLOCK_NEXT,
		.answer = "<ACTION_BLOCK_NEXT>"
	}
};

static const MlStep editing_steps [] =
{
	{
		.text   = "Go up one line and press {ACTION_SELECT_LINE} to select "
		          "the whole line.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_SELECT_LINE,
		.hint   = "Press Up, then hold Ctrl and press L.",
		.answer = "<Up><ACTION_SELECT_LINE>"
	},
	{
		.text   = "{ACTION_COPY} copies the selected lines.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_COPY,
		.answer = "<ACTION_COPY>"
	},
	{
		.text   = "Press Down to leave the selection, then {ACTION_PASTE} to "
		          "paste the copy below.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_PASTE,
		.answer = "<Down><ACTION_PASTE>"
	},
	{
		.text   = "{ACTION_UNDO} undoes the last change. Try it.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_UNDO,
		.answer = "<ACTION_UNDO>"
	},
	{
		.text   = "{ACTION_REDO} redoes what was undone.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_REDO,
		.answer = "<ACTION_REDO>"
	}
};

static const MlStep domains_steps [] =
{
	{
		.text   = "Every math line has a domain: algebra, lambda, pga2, pga3 "
		          "or boolean. The label at the left shows it. "
		          "{ACTION_DOMAIN_NEXT} goes to the next domain.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_DOMAIN_NEXT,
		.hint   = "Hold Alt and press the right arrow.",
		.answer = "<ACTION_DOMAIN_NEXT>"
	},
	{
		.text   = "{ACTION_BLOCK_PICK} chooses the domain of a new block "
		          "first. Press it.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_BLOCK_PICK,
		.answer = "<ACTION_BLOCK_PICK>"
	},
	{
		.text   = "Press Esc to cancel the choice.",
		.check  = ML_CHECK_KEY,
		.arg    = ML_KEY_ESCAPE,
		.answer = "<Esc>"
	},
	{
		.text   = "{ACTION_COMMAND} opens the command line. Type mode boolean "
		          "and press Enter: new blocks are boolean from now on.",
		.check  = ML_CHECK_COMMAND,
		.param  = "mode boolean",
		.hint   = "{ACTION_COMMAND}, then the words mode boolean, then Enter.",
		.answer = "<ACTION_COMMAND>mode boolean<Enter>"
	},
	{
		.text   = "Start a block with {ACTION_BLOCK_NEW}, type a & ~a and "
		          "press Enter. In the boolean domain & is and, ~ is not.",
		.check  = ML_CHECK_RESULT,
		.param  = "false",
		.hint   = "a and not a is never true.",
		.answer = "<ACTION_BLOCK_NEW>a & ~a<Enter>"
	},
	{
		.text   = "Back to algebra: {ACTION_COMMAND}, then mode algebra and "
		          "Enter.",
		.check  = ML_CHECK_COMMAND,
		.param  = "mode algebra",
		.answer = "<ACTION_COMMAND>mode algebra<Enter>"
	}
};

static const MlStep steps_steps [] =
{
	{
		.text   = "Start a block and write det([[1,2],[3,4]]), the "
		          "determinant of a matrix. Press Enter.",
		.check  = ML_CHECK_RESULT,
		.param  = "-2",
		.hint   = "{ACTION_BLOCK_NEW}, then the text, then Enter.",
		.answer = "<ACTION_BLOCK_NEW>det([[1,2],[3,4]])<Enter>"
	},
	{
		.text   = "Go up to that line and press {ACTION_STEPS}: the steps of "
		          "the calculation are shown under it.",
		.check  = ML_CHECK_STEPS,
		.hint   = "Press Up once, then {ACTION_STEPS}.",
		.answer = "<Up><ACTION_STEPS>"
	},
	{
		.text   = "{ACTION_STEPS} again hides the steps.",
		.check  = ML_CHECK_ACTION,
		.arg    = ML_ACT_STEPS,
		.answer = "<ACTION_STEPS>"
	}
};

static const MlStep help_steps [] =
{
	{
		.text   = "{ACTION_HELP} opens the help. Every page of the "
		          "documentation is there, and it can be searched.",
		.check  = ML_CHECK_STATE,
		.arg    = ML_TUT_HELP,
		.answer = "<ACTION_HELP>"
	},
	{
		.text   = "Press q to close the help.",
		.check  = ML_CHECK_STATE_NOT,
		.arg    = ML_TUT_HELP,
		.answer = "q"
	},
	{
		.text   = "{ACTION_WATCH} shows the watch window: every name defined "
		          "at the line of the cursor, with its type and value.",
		.check  = ML_CHECK_WATCH,
		.arg    = 1,
		.answer = "<ACTION_WATCH>"
	},
	{
		.text   = "{ACTION_WATCH} hides it again.",
		.check  = ML_CHECK_WATCH,
		.arg    = 0,
		.answer = "<ACTION_WATCH>"
	}
};

/* ---- stage 2: the syntax ----------------------------------------------- */

static const MlStep numbers_steps [] =
{
	{
		.text   = "Start a block ({ACTION_BLOCK_NEW}) and write a line whose "
		          "result is 1/2. Any way will do, 2/4 or 1/3 + 1/6.",
		.check  = ML_CHECK_RESULT,
		.param  = "1/2",
		.hint   = "Fractions are exact: 2/4 is 1/2.",
		.answer = "<ACTION_BLOCK_NEW>2/4<Enter>"
	},
	{
		.text   = "^ is the power and -2^2 is -4, not 4. Write a line whose "
		          "result is -4.",
		.check  = ML_CHECK_RESULT,
		.param  = "-4",
		.answer = "-2^2<Enter>"
	},
	{
		.text   = "Write a line whose result is 120 with a factorial: 5!",
		.check  = ML_CHECK_RESULT,
		.param  = "120",
		.answer = "5!<Enter>"
	},
	{
		.text   = "A root that is not a fraction is an approximation, marked "
		          "with \xE2\x89\x88. Write sqrt(2).",
		.check  = ML_CHECK_RESULT,
		.param  = "\xE2\x89\x88 1.41421356",
		.answer = "sqrt(2)<Enter>"
	}
};

static const MlStep names_steps [] =
{
	{
		.text   = "Define a function g so that g(2) is 7. A function is "
		          "written g(x) = ...",
		.check  = ML_CHECK_VALUE,
		.param  = "g(2)",
		.param2 = "7",
		.hint   = "For example g(x) = 3x + 1.",
		.answer = "g(x) = 3x + 1<Enter>"
	},
	{
		.text   = "A name is a whole word: 2x is 2 times x, but xy is one "
		          "name. Define x = 4, then write 3x on the next line.",
		.check  = ML_CHECK_RESULT,
		.param  = "12",
		.answer = "x = 4<Enter>3x<Enter>"
	},
	{
		.text   = "pi and e are constants. Write a line whose result is the "
		          "approximation of pi.",
		.check  = ML_CHECK_RESULT,
		.param  = "\xE2\x89\x88 3.14159265",
		.answer = "pi<Enter>"
	}
};

static const MlStep errors_steps [] =
{
	{
		.text   = "Write a line with a mistake, 2 +, and press Enter. The "
		          "message says what is wrong and the place is marked.",
		.check  = ML_CHECK_ERROR,
		.param  = "",
		.answer = "2 +<Enter>"
	},
	{
		.text   = "Write the line right: a line whose result is 4.",
		.check  = ML_CHECK_RESULT,
		.param  = "4",
		.answer = "2 + 2<Enter>"
	}
};

static const MlStep matrices_steps [] =
{
	{
		.text   = "A matrix is a list of rows: [[1,2],[3,4]]. Write a line "
		          "with its determinant, det([[1,2],[3,4]]), which is -2.",
		.check  = ML_CHECK_RESULT,
		.param  = "-2",
		.answer = "det([[1,2],[3,4]])<Enter>"
	},
	{
		.text   = "Solve the system 2x + y = 3 and x + 3y = 5 with "
		          "solve([[2,1],[1,3]], [3,5]).",
		.check  = ML_CHECK_RESULT,
		.param  = "[[4/5], [7/5]]",
		.answer = "solve([[2,1],[1,3]], [3,5])<Enter>"
	}
};

static const MlStep symbolic_steps [] =
{
	{
		.text   = "Names that stand for nothing are symbols inside the "
		          "symbolic functions. Differentiate x^3 with diff(x^3, x).",
		.check  = ML_CHECK_RESULT,
		.param  = "3x\xC2\xB2",
		.answer = "diff(x^3, x)<Enter>"
	},
	{
		.text   = "Integrate x^2 from 0 to 3: integrate(x^2, x, 0, 3).",
		.check  = ML_CHECK_RESULT,
		.param  = "9",
		.answer = "integrate(x^2, x, 0, 3)<Enter>"
	},
	{
		.text   = "A limit: limit(sin(x)/x, x, 0).",
		.check  = ML_CHECK_RESULT,
		.param  = "1",
		.answer = "limit(sin(x)/x, x, 0)<Enter>"
	}
};

static const MlStep units_steps [] =
{
	{
		.text   = "Units are part of numbers: write 5 km + 300 m.",
		.check  = ML_CHECK_RESULT,
		.param  = "5300 m",
		.answer = "5 km + 300 m<Enter>"
	},
	{
		.text   = "to converts: write 3 ft to m.",
		.check  = ML_CHECK_RESULT,
		.param  = "1143/1250 m",
		.answer = "3 ft to m<Enter>"
	}
};

static const MlStep numbersystems_steps [] =
{
	{
		.text   = "i is the imaginary unit. Multiply (1+2i)*(3-i).",
		.check  = ML_CHECK_RESULT,
		.param  = "5 + 5i",
		.answer = "(1+2i)*(3-i)<Enter>"
	},
	{
		.text   = "The square root of a negative number is complex: sqrt(-4).",
		.check  = ML_CHECK_RESULT,
		.param  = "2i",
		.answer = "sqrt(-4)<Enter>"
	},
	{
		.text   = "Quaternions use i, j and k: write i*j.",
		.check  = ML_CHECK_RESULT,
		.param  = "k",
		.answer = "i*j<Enter>"
	},
	{
		.text   = "Statistics: the mean of a list, mean([1,2,3,4]).",
		.check  = ML_CHECK_RESULT,
		.param  = "5/2",
		.answer = "mean([1,2,3,4])<Enter>"
	}
};

static const MlStep otherdomains_steps [] =
{
	{
		.text   = "Other domains have their own syntax. Switch with "
		          "{ACTION_COMMAND}, mode boolean, then start a block and "
		          "write a | ~a.",
		.check  = ML_CHECK_RESULT,
		.param  = "true",
		.answer = "<ACTION_COMMAND>mode boolean<Enter><ACTION_BLOCK_NEW>a | "
		          "~a<Enter>"
	},
	{
		.text   = "The lambda calculus: switch with mode lambda, start a "
		          "block and write (\xCE\xBBx. x) y. The result is the "
		          "reduced term.",
		.check  = ML_CHECK_RESULT,
		.param  = "y",
		.answer = "<ACTION_COMMAND>mode "
		          "lambda<Enter><ACTION_BLOCK_NEW>(\xCE\xBBx. x) y<Enter>"
	}
};

/* ---- the table --------------------------------------------------------- */

#define STEPS(a) .step_count = ml_count (a), .steps = a

static const MlLesson lessons [] =
{
	{
		.id = "notes",
		.title = "Writing and notes",
		.stage = 1, STEPS (notes_steps)
	},
	{
		.id = "math",
		.title = "Math lines",
		.stage = 1, STEPS (math_steps)
	},
	{
		.id = "moving",
		.title = "Moving around",
		.stage = 1, STEPS (moving_steps)
	},
	{
		.id = "editing",
		.title = "Selecting, copying, undo",
		.stage = 1, STEPS (editing_steps)
	},
	{
		.id = "domains",
		.title = "Domains and commands",
		.stage = 1, STEPS (domains_steps)
	},
	{
		.id = "steps",
		.title = "Results and steps",
		.stage = 1, STEPS (steps_steps)
	},
	{
		.id = "help",
		.title = "Help and the watch window",
		.stage = 1, STEPS (help_steps)
	},
	{
		.id = "numbers",
		.title = "Numbers and operators",
		.stage = 2, STEPS (numbers_steps)
	},
	{
		.id = "names",
		.title = "Names and functions",
		.stage = 2, STEPS (names_steps)
	},
	{
		.id = "errors",
		.title = "Reading errors",
		.stage = 2, STEPS (errors_steps)
	},
	{
		.id = "matrices",
		.title = "Matrices",
		.stage = 2, STEPS (matrices_steps)
	},
	{
		.id = "symbolic",
		.title = "Derivatives, integrals, limits",
		.stage = 2, STEPS (symbolic_steps)
	},
	{
		.id = "units",
		.title = "Units",
		.stage = 2, STEPS (units_steps)
	},
	{
		.id = "numsys",
		.title = "Complex numbers, quaternions, statistics",
		.stage = 2, STEPS (numbersystems_steps)
	},
	{
		.id = "domains2",
		.title = "Boolean algebra and lambda calculus",
		.stage = 2, STEPS (otherdomains_steps)
	}
};

uint64_t ml_lesson_count (void)
{
	return ml_count (lessons);
}

MlLesson ml_lesson_at (uint64_t index)
{
	return lessons [ml_choose (index < ml_count (lessons), index, 0)];
}

uint64_t ml_lesson_find (MlSlice id)
{
	uint64_t found = ml_count (lessons);

	for (
		uint64_t i = 0;
		i < ml_count (lessons) && found == ml_count (lessons);
		i++
	)
	{
		if (ml_slice_equal (id, ml_slice_cstr (lessons [i].id)))
		{
			found = i;
		}
	}

	return found;
}

uint64_t ml_lesson_first (int32_t stage)
{
	uint64_t found = ml_count (lessons);

	for (
		uint64_t i = 0;
		i < ml_count (lessons) && found == ml_count (lessons);
		i++
	)
	{
		if (lessons [i].stage == stage)
		{
			found = i;
		}
	}

	return found;
}
