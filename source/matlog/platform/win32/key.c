#include <matlog/platform/win32/key.h>

#define CTRL_RIGHT_ALT  0x0001u
#define CTRL_LEFT_ALT   0x0002u
#define CTRL_RIGHT_CTRL 0x0004u
#define CTRL_LEFT_CTRL  0x0008u
#define CTRL_SHIFT      0x0010u

#define VK_BACK_    0x08u
#define VK_TAB_     0x09u
#define VK_RETURN_  0x0Du
#define VK_ESCAPE_  0x1Bu
#define VK_SPACE_   0x20u
#define VK_PRIOR_   0x21u
#define VK_NEXT_    0x22u
#define VK_END_     0x23u
#define VK_HOME_    0x24u
#define VK_LEFT_    0x25u
#define VK_UP_      0x26u
#define VK_RIGHT_   0x27u
#define VK_DOWN_    0x28u
#define VK_INSERT_  0x2Du
#define VK_DELETE_  0x2Eu
#define VK_F1_      0x70u
#define VK_F12_     0x7Bu

typedef struct VkEntry
{
	uint16_t  vk;
	MlKeyCode code;
} VkEntry;

static const VkEntry vk_table [] = {
	{ .vk = VK_BACK_,   .code = ML_KEY_BACKSPACE },
	{ .vk = VK_TAB_,    .code = ML_KEY_TAB },
	{ .vk = VK_RETURN_, .code = ML_KEY_ENTER },
	{ .vk = VK_ESCAPE_, .code = ML_KEY_ESCAPE },
	{ .vk = VK_PRIOR_,  .code = ML_KEY_PAGE_UP },
	{ .vk = VK_NEXT_,   .code = ML_KEY_PAGE_DOWN },
	{ .vk = VK_END_,    .code = ML_KEY_END },
	{ .vk = VK_HOME_,   .code = ML_KEY_HOME },
	{ .vk = VK_LEFT_,   .code = ML_KEY_LEFT },
	{ .vk = VK_UP_,     .code = ML_KEY_UP },
	{ .vk = VK_RIGHT_,  .code = ML_KEY_RIGHT },
	{ .vk = VK_DOWN_,   .code = ML_KEY_DOWN },
	{ .vk = VK_INSERT_, .code = ML_KEY_INSERT },
	{ .vk = VK_DELETE_, .code = ML_KEY_DELETE }
};

static MlKeyCode code_from_vk (uint16_t vk)
{
	MlKeyCode code = ML_KEY_NONE;

	for (uint64_t i = 0; i < ml_count (vk_table) && code == ML_KEY_NONE; i++)
	{
		code = ml_choose (
			vk_table [i].vk == vk,
			vk_table [i].code,
			ML_KEY_NONE
		);
	}

	if (vk >= VK_F1_ && vk <= VK_F12_)
	{
		code = (MlKeyCode)((uint32_t)ML_KEY_F1 + (vk - VK_F1_));
	}

	return code;
}

static uint32_t mods_from_control (uint32_t control)
{
	uint32_t mods = 0;

	mods |= ml_choose (
		(control & CTRL_SHIFT),
		ML_MOD_SHIFT,
		0
	);
	mods |= ml_choose (
		(control & (CTRL_LEFT_CTRL | CTRL_RIGHT_CTRL)),
		ML_MOD_CTRL,
		0
	);
	mods |= ml_choose (
		(control & (CTRL_LEFT_ALT | CTRL_RIGHT_ALT)),
		ML_MOD_ALT,
		0
	);
	return mods;
}

MlDecodeResult ml_key_decode_win32 (MlRawKey raw, uint16_t pending_high)
{
	MlDecodeResult res = {
		.key = {
			.code = ML_KEY_NONE,
			.cp   = 0,
			.mods = 0
		},
		.pending_high = pending_high
	};

	MlKeyCode code = code_from_vk (raw.vk);
	uint32_t  mods = mods_from_control (raw.control);
	uint32_t  cp   = raw.unicode;

	if (code != ML_KEY_NONE)
	{
		res.key.code     = code;
		res.key.mods     = mods;
		res.pending_high = 0;
	}

	else if (cp >= 0xD800 && cp <= 0xDBFF)
	{
		res.pending_high = (uint16_t)cp;
	}

	else if (cp >= 0xDC00 && cp <= 0xDFFF)
	{
		if (pending_high != 0)
		{
			res.key.code = ML_KEY_CHAR;
			res.key.cp   = (
				  0x10000u
				+ (((uint32_t)pending_high - 0xD800u) << 10)
				+ (cp - 0xDC00u)
			);
			res.key.mods = mods & ~ML_MOD_SHIFT;
		}

		res.pending_high = 0;
	}

	else if (cp != 0 || raw.vk == VK_SPACE_)
	{
		cp = ml_choose (
			cp != 0,
			cp,
			' '
		);

		if (cp < 0x20)
		{
			/*
			 * Ctrl+A is 0x01 ... Ctrl+Z is 0x1A, Ctrl+\ ] ^ _ are 0x1C ... 0x1F
			 */
			cp = ml_choose (
				cp <= 26,
				'a' + (cp - 1),
				cp + 64
			);
			mods |= ML_MOD_CTRL;
		}

		else if ((mods & ML_MOD_CTRL) && (mods & ML_MOD_ALT))
		{
			/*
			 * AltGr is reported as Ctrl+Alt; the character is already what the
			 * user typed
			 */
			mods &= ~(ML_MOD_CTRL | ML_MOD_ALT);
		}

		/* shift is already reflected in the character itself */
		mods = ml_choose (
			(mods & (ML_MOD_CTRL | ML_MOD_ALT)),
			mods,
			(mods & ~ML_MOD_SHIFT)
		);

		res.key.code     = ML_KEY_CHAR;
		res.key.cp       = cp;
		res.key.mods     = mods;
		res.pending_high = 0;
	}

	return res;
}
