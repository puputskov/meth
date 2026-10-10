#include <matlog/ui/hint.h>

#include <matlog/texts/texts.h>

uint64_t ml_hint_count (void)
{
	return ml_text_count ("hints");
}

MlSlice ml_hint_at (uint64_t n)
{
	uint64_t count = ml_text_count ("hints");

	return ml_slice_cstr (
		ml_text_nth ("hints", ml_choose (count > 0, n % count, 0))
	);
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
