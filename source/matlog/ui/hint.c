#include <matlog/ui/hint.h>

static const char *const hints [] = {
	"Just type: a line is a note, {ACTION_BLOCK_NEW} starts a math block",
	"Try {1/3 + 1/6}: results are exact fractions",
	"{ACTION_COMMAND} opens the command line, {Tab} completes what you type",
	"{Esc} writes the line you are typing into the session",
	"Units work too: try {3 km to m}",
	"{ACTION_STEPS} shows the steps of the line under the cursor",
	"{ACTION_DOMAIN_PREV} and {ACTION_DOMAIN_NEXT} change the domain of a line",
	"{:symbols on} lets unknown names stay symbols: x + y",
	"{ACTION_FOLD} folds and unfolds a block of lines",
	"{:theme} changes the colours: dark, light, contrast, mono",
	"{:export latex} writes the session as a LaTeX file next to it",
	"{ACTION_UNDO} undoes, {ACTION_REDO} redoes, {ACTION_SAVE} saves, "
	"{ACTION_QUIT} quits"
};

uint64_t ml_hint_count (void)
{
	return ml_count (hints);
}

MlSlice ml_hint_at (uint64_t n)
{
	return ml_slice_cstr (hints [n % ml_count (hints)]);
}

uint64_t ml_hint_width (MlSlice hint)
{
	uint64_t width = 0;

	for (uint64_t i = 0; i < hint.len; i++)
	{
		width += ml_choose (hint.data [i] == '{' || hint.data [i] == '}', 0, 1);
	}

	return width;
}
