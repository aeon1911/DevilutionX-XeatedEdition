/**
 * @file gmenu.cpp
 *
 * Implementation of the in-game navigation and interaction.
 */
#include "gmenu.h"

#include <cstdint>

#include "DiabloUI/ui_flags.hpp"
#include "control.h"
#include "controls/axis_direction.h"
#include "controls/controller_motion.h"
#include "engine.h"
#include "engine/clx_sprite.hpp"
#include "engine/load_cel.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "options.h"
#include "stores.h"
#include "utils/language.h"
#include "utils/stdcompat/algorithm.hpp"
#include "utils/stdcompat/optional.hpp"
#include "utils/str_cat.hpp"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

// Width of the slider menu item, including the label.
constexpr int SliderItemWidth = 490;

// Horizontal dimensions of the slider value
constexpr int SliderValueBoxLeft = 16 + SliderItemWidth / 2;
constexpr int SliderValueBoxWidth = 287;

constexpr int SliderValueBorderWidth = 2;
constexpr int SliderValueLeft = SliderValueBoxLeft + SliderValueBorderWidth;
constexpr int SliderValueWidth = SliderValueBoxWidth - 2 * SliderValueBorderWidth;
constexpr int SliderValueHeight = 29;
constexpr int SliderValuePaddingTop = 10;
constexpr int SliderMarkerWidth = 27;

constexpr int SliderFillMin = SliderMarkerWidth / 2;
constexpr int SliderFillMax = SliderValueWidth - SliderMarkerWidth / 2 - 1;

constexpr int GMenuTop = 117;
constexpr int GMenuItemHeight = 45;

OptionalOwnedClxSpriteList optbar_cel;
OptionalOwnedClxSpriteList PentSpin_cel;
OptionalOwnedClxSpriteList option_cel;
OptionalOwnedClxSpriteList sgpLogo;
bool isDraggingSlider;
TMenuItem *sgpCurrItem;
int LogoAnim_tick;
uint8_t LogoAnim_frame;
void (*gmenu_current_option)();
int sgCurrentMenuIdx;
const GMenuCompactStyle *sgCompactStyle;
int sgCompactScroll;

constexpr int CompactMaxWidth = 600;
constexpr int CompactPadding = 12;
constexpr int CompactTitleHeight = 40;
constexpr int CompactRowHeight = 26;
constexpr int CompactFooterHeight = 48;

struct CompactLayout {
	Rectangle box;
	int listTop;
	int visibleRows;
};

/** @brief Height of a menu item, reduced when the items wouldn't fit above the main panel. */
int GmenuItemHeight()
{
	if (sgCurrentMenuIdx <= 0)
		return GMenuItemHeight;
	const int available = GetMainPanel().position.y - (GMenuTop + GetUIRectangle().position.y);
	return clamp(available / sgCurrentMenuIdx, 36, GMenuItemHeight);
}

int GmenuCurrentIndex()
{
	return sgpCurrItem != nullptr ? static_cast<int>(sgpCurrItem - sgpCurrentMenu) : 0;
}

CompactLayout GmenuGetCompactLayout()
{
	const int width = std::min(CompactMaxWidth, gnScreenWidth - 16);
	const int top = 8;
	const int bottom = std::max(GetMainPanel().position.y, top + 200) - 8;
	const int chromeHeight = CompactTitleHeight + CompactFooterHeight + 2 * CompactPadding;
	const int rowsThatFit = std::max((bottom - top - chromeHeight) / CompactRowHeight, 1);
	const int visibleRows = std::max(std::min(sgCurrentMenuIdx, rowsThatFit), 1);
	const int height = chromeHeight + visibleRows * CompactRowHeight;
	const int x = (gnScreenWidth - width) / 2;
	const int y = top + (bottom - top - height) / 2;
	return { { { x, y }, { width, height } }, y + CompactPadding + CompactTitleHeight, visibleRows };
}

void GmenuCompactEnsureVisible(int visibleRows)
{
	int index = GmenuCurrentIndex();
	// Keep the section title of the first item of a section visible
	if (index > 0 && sgpCurrentMenu[index - 1].isHeader())
		index--;
	if (index < sgCompactScroll)
		sgCompactScroll = index;
	if (GmenuCurrentIndex() >= sgCompactScroll + visibleRows)
		sgCompactScroll = GmenuCurrentIndex() - visibleRows + 1;
	sgCompactScroll = clamp(sgCompactScroll, 0, std::max(sgCurrentMenuIdx - visibleRows, 0));
}

void GmenuCompactCycle(TMenuItem &item)
{
	uint16_t step = item.sliderStep();
	step = step >= item.sliderSteps() ? 0 : step + 1;
	item.setSliderStep(step);
	item.fnMenu(false);
}

void GmenuDrawCompact(const Surface &out)
{
	const CompactLayout layout = GmenuGetCompactLayout();
	const Rectangle &box = layout.box;
	GmenuCompactEnsureVisible(layout.visibleRows);

	DrawHalfTransparentRectTo(out, box.position.x, box.position.y, box.size.width, box.size.height);
	DrawHalfTransparentRectTo(out, box.position.x, box.position.y, box.size.width, box.size.height);
	DrawHorizontalLine(out, box.position, box.size.width, PAL16_YELLOW + 4);
	DrawHorizontalLine(out, box.position + Displacement { 0, box.size.height - 1 }, box.size.width, PAL16_YELLOW + 4);
	DrawVerticalLine(out, box.position, box.size.height, PAL16_YELLOW + 4);
	DrawVerticalLine(out, box.position + Displacement { box.size.width - 1, 0 }, box.size.height, PAL16_YELLOW + 4);

	const int innerX = box.position.x + CompactPadding;
	const int innerWidth = box.size.width - 2 * CompactPadding;
	DrawString(out, _(sgCompactStyle->title), { { innerX, box.position.y + CompactPadding }, { innerWidth, CompactTitleHeight - 8 } },
	    { UiFlags::FontSize30 | UiFlags::ColorGold | UiFlags::AlignCenter | UiFlags::VerticalCenter, 2 });
	DrawHorizontalLine(out, { innerX, layout.listTop - 4 }, innerWidth, PAL16_YELLOW + 8);

	const int lastRow = std::min(sgCompactScroll + layout.visibleRows, sgCurrentMenuIdx);
	for (int i = sgCompactScroll; i < lastRow; i++) {
		TMenuItem &item = sgpCurrentMenu[i];
		const Rectangle row { { innerX, layout.listTop + (i - sgCompactScroll) * CompactRowHeight }, { innerWidth, CompactRowHeight } };
		if (item.isHeader()) {
			DrawString(out, _(item.pszStr), row, { UiFlags::FontSize24 | UiFlags::ColorWhitegold | UiFlags::AlignCenter | UiFlags::VerticalCenter, 1 });
			continue;
		}
		const bool selected = &item == sgpCurrItem;
		const Rectangle textRect { row.position + Displacement { 20, 0 }, { row.size.width - 40, row.size.height } };
		if (selected) {
			DrawHalfTransparentRectTo(out, row.position.x, row.position.y + 1, row.size.width, row.size.height - 2, PAL16_RED + 10);
			const ClxSprite pentagram = (*pSPentSpn2Cels)[PentSpn2Spin()];
			ClxDraw(out, { row.position.x + 3, row.position.y + row.size.height / 2 + 6 }, pentagram);
			ClxDraw(out, { row.position.x + row.size.width - 15, row.position.y + row.size.height / 2 + 6 }, pentagram);
		}
		UiFlags color = UiFlags::ColorGold;
		if (!item.enabled())
			color = UiFlags::ColorBlack;
		else if (selected)
			color = UiFlags::ColorWhite;
		DrawString(out, _(item.pszStr), textRect, { UiFlags::FontSize24 | color | UiFlags::VerticalCenter, 1 });
		if (item.isSlider() && sgCompactStyle->getValue != nullptr) {
			std::string value = sgCompactStyle->getValue(item);
			if (selected)
				value = StrCat("< ", value, " >");
			DrawString(out, value, textRect, { UiFlags::FontSize24 | (selected ? UiFlags::ColorWhite : UiFlags::ColorBlue) | UiFlags::AlignRight | UiFlags::VerticalCenter, 1 });
		}
	}

	if (sgCurrentMenuIdx > layout.visibleRows) {
		const int trackX = box.position.x + box.size.width - 6;
		const int trackHeight = layout.visibleRows * CompactRowHeight;
		DrawVerticalLine(out, { trackX, layout.listTop }, trackHeight, PAL16_GRAY + 10);
		const int thumbHeight = std::max(trackHeight * layout.visibleRows / sgCurrentMenuIdx, 8);
		const int thumbY = layout.listTop + (trackHeight - thumbHeight) * sgCompactScroll / std::max(sgCurrentMenuIdx - layout.visibleRows, 1);
		FillRect(out, trackX - 1, thumbY, 3, thumbHeight, PAL16_YELLOW + 4);
	}

	const int footerTop = layout.listTop + layout.visibleRows * CompactRowHeight + 4;
	DrawHorizontalLine(out, { innerX, footerTop }, innerWidth, PAL16_YELLOW + 8);
	if (sgCompactStyle->getFooter != nullptr && sgpCurrItem != nullptr) {
		const std::string footer = WordWrapString(sgCompactStyle->getFooter(*sgpCurrItem), innerWidth, GameFont12, 1);
		DrawString(out, footer, { { innerX, footerTop + 6 }, { innerWidth, CompactFooterHeight - 8 } },
		    { UiFlags::FontSize12 | UiFlags::ColorWhitegold | UiFlags::AlignCenter, 1, 13 });
	}
}

bool GmenuCompactLeftMouse()
{
	const CompactLayout layout = GmenuGetCompactLayout();
	if (!layout.box.contains(MousePosition))
		return true;
	if (MousePosition.y < layout.listTop)
		return true;
	const int row = (MousePosition.y - layout.listTop) / CompactRowHeight;
	if (row >= layout.visibleRows)
		return true;
	const int index = sgCompactScroll + row;
	if (index >= sgCurrentMenuIdx)
		return true;
	TMenuItem &item = sgpCurrentMenu[index];
	if (!item.enabled() || item.isHeader())
		return true;
	sgpCurrItem = &item;
	PlaySFX(IS_TITLEMOV);
	if (item.isSlider())
		GmenuCompactCycle(item);
	else
		item.fnMenu(true);
	return true;
}

void GmenuUpDown(bool isDown)
{
	if (sgpCurrItem == nullptr) {
		return;
	}
	isDraggingSlider = false;
	int i = sgCurrentMenuIdx;
	if (sgCurrentMenuIdx != 0) {
		while (i != 0) {
			i--;
			if (isDown) {
				sgpCurrItem++;
				if (sgpCurrItem->fnMenu == nullptr)
					sgpCurrItem = &sgpCurrentMenu[0];
			} else {
				if (sgpCurrItem == sgpCurrentMenu)
					sgpCurrItem = &sgpCurrentMenu[sgCurrentMenuIdx];
				sgpCurrItem--;
			}
			if (sgpCurrItem->enabled()) {
				if (i != 0)
					PlaySFX(IS_TITLEMOV);
				return;
			}
		}
	}
}

void GmenuLeftRight(bool isRight)
{
	if (!sgpCurrItem->isSlider())
		return;

	uint16_t step = sgpCurrItem->sliderStep();
	if (isRight) {
		if (step == sgpCurrItem->sliderSteps())
			return;
		step++;
	} else {
		if (step == 0)
			return;
		step--;
	}
	sgpCurrItem->setSliderStep(step);
	sgpCurrItem->fnMenu(false);
}

int GmenuGetLineWidth(TMenuItem *pItem)
{
	if (pItem->isSlider())
		return SliderItemWidth;

	return GetLineWidth(_(pItem->pszStr), GameFont46, 2);
}

void GmenuDrawMenuItem(const Surface &out, TMenuItem *pItem, int y)
{
	int w = GmenuGetLineWidth(pItem);
	if (pItem->isSlider()) {
		int uiPositionX = GetUIRectangle().position.x;
		ClxDraw(out, { SliderValueBoxLeft + uiPositionX, y + 40 }, (*optbar_cel)[0]);
		const uint16_t step = pItem->dwFlags & 0xFFF;
		const uint16_t steps = std::max<uint16_t>(pItem->sliderSteps(), 2);
		const uint16_t pos = SliderFillMin + step * (SliderFillMax - SliderFillMin) / steps;
		SDL_Rect rect = MakeSdlRect(SliderValueLeft + uiPositionX, y + SliderValuePaddingTop, pos, SliderValueHeight);
		SDL_FillRect(out.surface, &rect, 205);
		ClxDraw(out, { SliderValueLeft + pos - SliderMarkerWidth / 2 + uiPositionX, y + SliderValuePaddingTop + SliderValueHeight - 1 }, (*option_cel)[0]);
	}

	int x = (gnScreenWidth - w) / 2;
	UiFlags style = pItem->enabled() ? UiFlags::ColorGold : UiFlags::ColorBlack;
	DrawString(out, _(pItem->pszStr), Point { x, y }, { style | UiFlags::FontSize46, 2 });
	if (pItem == sgpCurrItem) {
		const ClxSprite sprite = (*PentSpin_cel)[PentSpn2Spin()];
		ClxDraw(out, { x - 54, y + 51 }, sprite);
		ClxDraw(out, { x + 4 + w, y + 51 }, sprite);
	}
}

void GameMenuMove()
{
	static AxisDirectionRepeater repeater;
	const AxisDirection moveDir = repeater.Get(GetLeftStickOrDpadDirection(false));
	if (moveDir.x != AxisDirectionX_NONE)
		GmenuLeftRight(moveDir.x == AxisDirectionX_RIGHT);
	if (moveDir.y != AxisDirectionY_NONE)
		GmenuUpDown(moveDir.y == AxisDirectionY_DOWN);
}

bool GmenuMouseIsOverSlider()
{
	int uiPositionX = GetUIRectangle().position.x;
	if (MousePosition.x < SliderValueLeft + uiPositionX) {
		return false;
	}
	if (MousePosition.x >= SliderValueLeft + SliderValueWidth + uiPositionX) {
		return false;
	}
	return true;
}

int GmenuGetSliderFill()
{
	return clamp(MousePosition.x - SliderValueLeft - GetUIRectangle().position.x, SliderFillMin, SliderFillMax);
}

} // namespace

TMenuItem *sgpCurrentMenu;

void gmenu_draw_pause(const Surface &out)
{
	if (leveltype != DTYPE_TOWN)
		RedBack(out);
	if (sgpCurrentMenu == nullptr) {
		LightTableIndex = 0;
		DrawString(out, _("Pause"), { { 0, 0 }, { gnScreenWidth, GetMainPanel().position.y } }, { UiFlags::FontSize46 | UiFlags::ColorGold | UiFlags::AlignCenter | UiFlags::VerticalCenter, 2 });
	}
}

void FreeGMenu()
{
	sgpLogo = std::nullopt;
	PentSpin_cel = std::nullopt;
	option_cel = std::nullopt;
	optbar_cel = std::nullopt;
}

void gmenu_init_menu()
{
	LogoAnim_frame = 0;
	sgpCurrentMenu = nullptr;
	sgpCurrItem = nullptr;
	gmenu_current_option = nullptr;
	sgCurrentMenuIdx = 0;
	isDraggingSlider = false;

	if (HeadlessMode)
		return;

	if (gbIsHellfire)
		sgpLogo = LoadCel("data\\hf_logo3", 430);
	else
		sgpLogo = LoadCel("data\\diabsmal", 296);
	PentSpin_cel = LoadCel("data\\pentspin", 48);
	option_cel = LoadCel("data\\option", SliderMarkerWidth);
	optbar_cel = LoadCel("data\\optbar", SliderValueBoxWidth);
}

bool gmenu_is_active()
{
	return sgpCurrentMenu != nullptr;
}

void gmenu_set_items(TMenuItem *pItem, void (*gmFunc)())
{
	sgCompactStyle = nullptr;
	sgCompactScroll = 0;
	PauseMode = 0;
	isDraggingSlider = false;
	sgpCurrentMenu = pItem;
	gmenu_current_option = gmFunc;
	if (gmenu_current_option != nullptr) {
		gmenu_current_option();
	}
	sgCurrentMenuIdx = 0;
	if (sgpCurrentMenu != nullptr) {
		for (int i = 0; sgpCurrentMenu[i].fnMenu != nullptr; i++) {
			sgCurrentMenuIdx++;
		}
	}
	// BUGFIX: OOB access when sgCurrentMenuIdx is 0; should be set to NULL instead. (fixed)
	sgpCurrItem = sgCurrentMenuIdx > 0 ? &sgpCurrentMenu[sgCurrentMenuIdx - 1] : nullptr;
	GmenuUpDown(true);
	if (sgpCurrentMenu == nullptr)
		SaveOptions();
}

void gmenu_draw(const Surface &out)
{
	if (sgpCurrentMenu != nullptr) {
		GameMenuMove();
		if (gmenu_current_option != nullptr)
			gmenu_current_option();
		if (sgpCurrentMenu == nullptr)
			return;
		if (sgCompactStyle != nullptr) {
			GmenuDrawCompact(out);
			return;
		}
		if (gbIsHellfire) {
			const uint32_t ticks = SDL_GetTicks();
			if ((int)(ticks - LogoAnim_tick) > 25) {
				++LogoAnim_frame;
				if (LogoAnim_frame >= 16)
					LogoAnim_frame = 0;
				LogoAnim_tick = ticks;
			}
		}
		int uiPositionY = GetUIRectangle().position.y;
		const ClxSprite sprite = (*sgpLogo)[LogoAnim_frame];
		ClxDraw(out, { (gnScreenWidth - sprite.width()) / 2, 102 + uiPositionY }, sprite);
		int y = 110 + uiPositionY;
		TMenuItem *i = sgpCurrentMenu;
		if (sgpCurrentMenu->fnMenu != nullptr) {
			while (i->fnMenu != nullptr) {
				GmenuDrawMenuItem(out, i, y);
				i++;
				y += GmenuItemHeight();
			}
		}
	}
}

void gmenu_set_compact_items(TMenuItem *pItem, void (*gmFunc)(), const GMenuCompactStyle *style)
{
	gmenu_set_items(pItem, gmFunc);
	if (sgpCurrentMenu != nullptr)
		sgCompactStyle = style;
}

TMenuItem *gmenu_get_current_item()
{
	return sgpCurrentMenu != nullptr ? sgpCurrItem : nullptr;
}

bool gmenu_presskeys(SDL_Keycode vkey)
{
	if (sgpCurrentMenu == nullptr)
		return false;
	switch (vkey) {
	case SDLK_KP_ENTER:
	case SDLK_RETURN:
		if (sgpCurrItem->enabled()) {
			PlaySFX(IS_TITLEMOV);
			if (sgCompactStyle != nullptr && sgpCurrItem->isSlider())
				GmenuCompactCycle(*sgpCurrItem);
			else
				sgpCurrItem->fnMenu(true);
		}
		break;
	case SDLK_ESCAPE:
		PlaySFX(IS_TITLEMOV);
		if (sgCompactStyle != nullptr && sgCompactStyle->onCancel != nullptr)
			sgCompactStyle->onCancel();
		else
			gmenu_set_items(nullptr, nullptr);
		break;
	case SDLK_SPACE:
		return false;
	case SDLK_LEFT:
		GmenuLeftRight(false);
		break;
	case SDLK_RIGHT:
		GmenuLeftRight(true);
		break;
	case SDLK_UP:
		GmenuUpDown(false);
		break;
	case SDLK_DOWN:
		GmenuUpDown(true);
		break;
	default:
		break;
	}
	return true;
}

bool gmenu_on_mouse_move()
{
	if (!isDraggingSlider)
		return false;

	const uint16_t step = sgpCurrItem->sliderSteps() * (GmenuGetSliderFill() - SliderFillMin) / (SliderFillMax - SliderFillMin);
	sgpCurrItem->setSliderStep(step);
	sgpCurrItem->fnMenu(false);

	return true;
}

bool gmenu_left_mouse(bool isDown)
{
	if (!isDown) {
		if (isDraggingSlider) {
			isDraggingSlider = false;
			return true;
		}
		return false;
	}

	if (sgpCurrentMenu == nullptr) {
		return false;
	}
	if (sgCompactStyle != nullptr)
		return GmenuCompactLeftMouse();
	const Point uiPosition = GetUIRectangle().position;
	if (MousePosition.y >= GetMainPanel().position.y) {
		return false;
	}
	if (MousePosition.y - (GMenuTop + uiPosition.y) < 0) {
		return true;
	}
	int i = (MousePosition.y - (GMenuTop + uiPosition.y)) / GmenuItemHeight();
	if (i >= sgCurrentMenuIdx) {
		return true;
	}
	TMenuItem *pItem = &sgpCurrentMenu[i];
	if (!pItem->enabled()) {
		return true;
	}
	int w = GmenuGetLineWidth(pItem);
	uint16_t screenWidth = GetScreenWidth();
	if (MousePosition.x < screenWidth / 2 - w / 2) {
		return true;
	}
	if (MousePosition.x > screenWidth / 2 + w / 2) {
		return true;
	}
	sgpCurrItem = pItem;
	PlaySFX(IS_TITLEMOV);
	if (pItem->isSlider()) {
		isDraggingSlider = GmenuMouseIsOverSlider();
		gmenu_on_mouse_move();
	} else {
		sgpCurrItem->fnMenu(true);
	}
	return true;
}

void gmenu_slider_set(TMenuItem *pItem, int min, int max, int value)
{
	assert(pItem);
	uint16_t nSteps = std::max<uint16_t>(pItem->sliderSteps(), 2);
	pItem->setSliderStep(((max - min - 1) / 2 + (value - min) * nSteps) / (max - min));
}

int gmenu_slider_get(TMenuItem *pItem, int min, int max)
{
	uint16_t step = pItem->sliderStep();
	uint16_t steps = std::max<uint16_t>(pItem->sliderSteps(), 2);
	return min + (step * (max - min) + (steps - 1) / 2) / steps;
}

void gmenu_slider_steps(TMenuItem *pItem, int steps)
{
	pItem->dwFlags &= 0xFF000FFF;
	pItem->setSliderSteps(steps);
}

} // namespace devilution
