/**
 * @file xeated/cheatmenu.cpp
 *
 * Xeated Edition: in-game walk speed and cheat menu.
 */
#include "xeated/cheatmenu.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "gamemenu.h"
#include "gmenu.h"
#include "options.h"
#include "player.h"
#include "utils/language.h"
#include "utils/stdcompat/algorithm.hpp"
#include "utils/str_cat.hpp"
#include "xeated/xeated.hpp"

namespace devilution {

namespace xeated {

namespace {

enum class RowKind : uint8_t {
	Header,
	Option,
	Action,
	WarpLevel,
	Back,
};

enum class ValueFormat : uint8_t {
	Plain,
	Percent,
	Bonus,
};

struct CheatRow {
	RowKind kind;
	const char *label;
	OptionEntryBase *option = nullptr;
	ValueFormat format = ValueFormat::Plain;
	std::string (*action)() = nullptr;
	const char *help = nullptr;
};

int WarpDestination = 1;
std::string LastMessage;
const TMenuItem *LastMessageItem = nullptr;

std::string ActionRestore() { return RestoreHealthAndMana(); }
std::string ActionLevelUp() { return LevelUp(1); }
std::string ActionLevelUp10() { return LevelUp(10); }
std::string ActionStatPoints() { return AddStatPoints(5); }
std::string ActionMaxStats() { return MaximizeStats(); }
std::string ActionLearnSpells() { return LearnAllSpells(); }
std::string ActionSpellLevel() { return RaiseSpellLevels(1); }
std::string ActionSpellLevelMax() { return RaiseSpellLevels(MaxSpellLevel); }
std::string ActionGold10k() { return AddGold(10000); }
std::string ActionGold100k() { return AddGold(100000); }
std::string ActionIdentify() { return IdentifyAllItems(); }
std::string ActionRepair() { return RepairAllItems(); }
std::string ActionRevealMap() { return RevealMap(); }
std::string ActionWarp() { return WarpToLevel(WarpDestination); }

std::vector<CheatRow> &Rows()
{
	static std::vector<CheatRow> rows;
	if (!rows.empty())
		return rows;

	XeatedOptions &o = sgOptions.Xeated;
	rows = {
		{ RowKind::Header, N_("Movement") },
		{ RowKind::Option, N_("Walk Speed"), &o.walkSpeed },
		{ RowKind::Option, N_("Always Run"), &o.alwaysRun },
		{ RowKind::Header, N_("Hero and Attributes") },
		{ RowKind::Option, N_("Invincible"), &o.invincible },
		{ RowKind::Option, N_("Infinite Mana"), &o.infiniteMana },
		{ RowKind::Option, N_("One Hit Kill"), &o.oneHitKill },
		{ RowKind::Option, N_("No Durability Loss"), &o.noDurabilityLoss },
		{ RowKind::Action, N_("Restore Life and Mana"), nullptr, ValueFormat::Plain, &ActionRestore, N_("Fills the life and mana globes.") },
		{ RowKind::Action, N_("Level Up"), nullptr, ValueFormat::Plain, &ActionLevelUp, N_("Gains one character level (with its stat points).") },
		{ RowKind::Action, N_("Level Up x10"), nullptr, ValueFormat::Plain, &ActionLevelUp10, N_("Gains ten character levels (with their stat points).") },
		{ RowKind::Action, N_("Add 5 Stat Points"), nullptr, ValueFormat::Plain, &ActionStatPoints, N_("Distribute them with the + buttons of the character panel.") },
		{ RowKind::Action, N_("Maximize Attributes"), nullptr, ValueFormat::Plain, &ActionMaxStats, N_("Strength, magic, dexterity and vitality go to the class maximum.") },
		{ RowKind::Header, N_("Spells") },
		{ RowKind::Action, N_("Learn All Spells"), nullptr, ValueFormat::Plain, &ActionLearnSpells, N_("Learns every spell available in this game at level 1 or more.") },
		{ RowKind::Action, N_("All Spells +1 Level"), nullptr, ValueFormat::Plain, &ActionSpellLevel, N_("Raises every known spell by one level (maximum 15).") },
		{ RowKind::Action, N_("All Spells to Level 15"), nullptr, ValueFormat::Plain, &ActionSpellLevelMax, N_("Raises every known spell to the maximum level.") },
		{ RowKind::Header, N_("Gold, Experience and Drops") },
		{ RowKind::Option, N_("Experience Gain %"), &o.experienceRate, ValueFormat::Percent },
		{ RowKind::Option, N_("Gold Drop %"), &o.goldRate, ValueFormat::Percent },
		{ RowKind::Option, N_("Drop Chance %"), &o.dropRate, ValueFormat::Percent },
		{ RowKind::Option, N_("Drop Quality"), &o.dropQuality },
		{ RowKind::Option, N_("Item Level Bonus"), &o.itemLevelBonus, ValueFormat::Bonus },
		{ RowKind::Action, N_("Add 10000 Gold"), nullptr, ValueFormat::Plain, &ActionGold10k, N_("Gold goes to the inventory, what doesn't fit goes to the stash.") },
		{ RowKind::Action, N_("Add 100000 Gold"), nullptr, ValueFormat::Plain, &ActionGold100k, N_("Gold goes to the inventory, what doesn't fit goes to the stash.") },
		{ RowKind::Header, N_("Items and World") },
		{ RowKind::Action, N_("Identify All Items"), nullptr, ValueFormat::Plain, &ActionIdentify, N_("Identifies the equipped, inventory and belt items.") },
		{ RowKind::Action, N_("Repair All Items"), nullptr, ValueFormat::Plain, &ActionRepair, N_("Restores the durability of the equipped, inventory and belt items.") },
		{ RowKind::Action, N_("Reveal Map"), nullptr, ValueFormat::Plain, &ActionRevealMap, N_("Reveals the automap of the current dungeon level.") },
		{ RowKind::WarpLevel, N_("Warp Destination"), nullptr, ValueFormat::Plain, nullptr, N_("Dungeon level used by \"Warp to Level\".") },
		{ RowKind::Action, N_("Warp to Level"), nullptr, ValueFormat::Plain, &ActionWarp, N_("Travels directly to the selected dungeon level.") },
		{ RowKind::Back, N_("Previous Menu") },
	};
	return rows;
}

std::vector<TMenuItem> &MenuItems()
{
	static std::vector<TMenuItem> items;
	return items;
}

void HandleItem(bool activate);
void Noop(bool /*activate*/)
{
}

const CheatRow *RowOf(const TMenuItem &item)
{
	std::vector<TMenuItem> &items = MenuItems();
	if (items.empty() || &item < items.data() || &item >= items.data() + Rows().size())
		return nullptr;
	return &Rows()[&item - items.data()];
}

void SyncItems()
{
	std::vector<CheatRow> &rows = Rows();
	std::vector<TMenuItem> &items = MenuItems();
	for (size_t i = 0; i < rows.size(); i++) {
		const CheatRow &row = rows[i];
		TMenuItem &item = items[i];
		switch (row.kind) {
		case RowKind::Option:
			if (row.option->GetType() == OptionEntryType::Boolean) {
				gmenu_slider_steps(&item, 1);
				item.setSliderStep(**static_cast<OptionEntryBoolean *>(row.option) ? 1 : 0);
			} else {
				auto *list = static_cast<OptionEntryListBase *>(row.option);
				gmenu_slider_steps(&item, static_cast<int>(list->GetListSize()) - 1);
				item.setSliderStep(static_cast<uint16_t>(list->GetActiveListIndex()));
			}
			break;
		case RowKind::WarpLevel:
			WarpDestination = clamp(WarpDestination, 1, MaxWarpLevel());
			gmenu_slider_steps(&item, MaxWarpLevel() - 1);
			item.setSliderStep(static_cast<uint16_t>(WarpDestination - 1));
			break;
		default:
			break;
		}
	}
}

void HandleItem(bool activate)
{
	TMenuItem *item = gmenu_get_current_item();
	if (item == nullptr)
		return;
	const CheatRow *row = RowOf(*item);
	if (row == nullptr)
		return;

	switch (row->kind) {
	case RowKind::Option:
		if (row->option->GetType() == OptionEntryType::Boolean)
			static_cast<OptionEntryBoolean *>(row->option)->SetValue(item->sliderStep() != 0);
		else
			static_cast<OptionEntryListBase *>(row->option)->SetActiveListIndex(item->sliderStep());
		break;
	case RowKind::WarpLevel:
		WarpDestination = item->sliderStep() + 1;
		break;
	case RowKind::Action:
		if (activate && row->action != nullptr) {
			LastMessageItem = item;
			LastMessage = row->action();
		}
		break;
	case RowKind::Back:
		if (activate) {
			SaveOptions();
			gamemenu_on();
		}
		break;
	case RowKind::Header:
		break;
	}
}

std::string GetValue(const TMenuItem &item)
{
	const CheatRow *row = RowOf(item);
	if (row == nullptr)
		return {};
	if (row->kind == RowKind::WarpLevel)
		return StrCat(WarpDestination);
	if (row->kind != RowKind::Option)
		return {};

	std::string value { row->option->GetValueDescription() };
	switch (row->format) {
	case ValueFormat::Percent:
		return StrCat(value, "%");
	case ValueFormat::Bonus:
		return StrCat("+", value);
	case ValueFormat::Plain:
		break;
	}
	return value;
}

std::string GetFooter(const TMenuItem &item)
{
	const CheatRow *row = RowOf(item);
	if (row == nullptr)
		return {};
	if (&item == LastMessageItem && !LastMessage.empty())
		return LastMessage;
	if (row->kind == RowKind::Option)
		return std::string { row->option->GetDescription() };
	if (row->help != nullptr)
		return std::string(_(row->help));
	return std::string(_("Left/Right: change value. A: confirm. B: back."));
}

void OnCancel()
{
	SaveOptions();
	gamemenu_on();
}

constexpr GMenuCompactStyle CheatMenuStyle {
	N_("Xeated Edition Cheats"),
	&GetValue,
	&GetFooter,
	&OnCancel,
};

} // namespace

void OpenCheatMenu()
{
	if (!IsActive())
		return;

	std::vector<CheatRow> &rows = Rows();
	std::vector<TMenuItem> &items = MenuItems();
	if (items.empty()) {
		for (const CheatRow &row : rows) {
			switch (row.kind) {
			case RowKind::Header:
				items.push_back({ GMENU_HEADER, row.label, &Noop });
				break;
			case RowKind::Option:
			case RowKind::WarpLevel:
				items.push_back({ GMENU_ENABLED | GMENU_SLIDER, row.label, &HandleItem });
				break;
			case RowKind::Action:
			case RowKind::Back:
				items.push_back({ GMENU_ENABLED, row.label, &HandleItem });
				break;
			}
		}
		items.push_back({ GMENU_ENABLED, nullptr, nullptr });
	}

	LastMessage.clear();
	LastMessageItem = nullptr;
	gmenu_set_compact_items(items.data(), &SyncItems, &CheatMenuStyle);
}

} // namespace xeated

} // namespace devilution
