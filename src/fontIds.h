// Korean built-in font IDs. Zero remains the missing-font sentinel.
#pragma once

#define KOPUB_14_FONT_ID (-1446433084)
#define PRETENDARD_10_FONT_ID (1983644244)
#define UI_10_FONT_ID PRETENDARD_10_FONT_ID
#define UI_12_FONT_ID PRETENDARD_10_FONT_ID
#define SMALL_FONT_ID PRETENDARD_10_FONT_ID

static_assert(KOPUB_14_FONT_ID != 0);
static_assert(PRETENDARD_10_FONT_ID != 0);
