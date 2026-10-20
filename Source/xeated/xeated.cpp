/**
 * @file xeated/xeated.cpp
 *
 * Xeated Edition: walk speed options and single player cheats.
 */
#include "xeated/xeated.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

#include <fmt/format.h>

#include "automap.h"
#include "control.h"
#include "engine/backbuffer_state.hpp"
#include "engine/demomode.h"
#include "engine/random.hpp"
#include "gamemenu.h"
#include "init.h"
#include "interfac.h"
#include "inv.h"
#include "items.h"
#include "levels/gendung.h"
#include "multi.h"
#include "player.h"
#include "playerdat.hpp"
#include "qol/stash.h"
#include "spells.h"
#include "utils/language.h"
#include "utils/stdcompat/algorithm.hpp"

namespace devilution {

namespace xeated {

namespace {

constexpr int MaxItemLevel = 60;

Player *ActivePlayer()
{
	if (!IsActive() || MyPlayer == nullptr)
		return nullptr;
	return MyPlayer;
}

template <typename F>
void ForEachPlayerItem(Player &player, F &&callback)
{
	for (Item &item : player.InvBody)
		callback(item);
	for (int i = 0; i < player._pNumInv; i++)
		callback(player.InvList[i]);
	for (Item &item : player.SpdList)
		callback(item);
}

std::string NotAvailable()
{
	return std::string(_("Cheats are only available in single player games."));
}

} // namespace

int8_t SkippedWalkFramesForSpeed(WalkSpeed speed)
{
	switch (speed) {
	case WalkSpeed::Fast:
		return 0;
	case WalkSpeed::Run:
		return 2;
	case WalkSpeed::Faster:
		return 3;
	case WalkSpeed::Turbo:
		return 4;
	case WalkSpeed::Normal:
	default:
		return -2;
	}
}

bool IsActive()
{
	return !gbIsMultiplayer && !demo::IsRunning() && !demo::IsRecording();
}

bool AppliesTo(const Player &player)
{
	return IsActive() && &player == MyPlayer;
}

int8_t GetWalkSkippedFrames(const Player &player, int8_t vanillaSkippedFrames)
{
	if (!AppliesTo(player))
		return vanillaSkippedFrames;

	WalkSpeed speed = *sgOptions.Xeated.walkSpeed;
	if (*sgOptions.Xeated.alwaysRun && speed < WalkSpeed::Run)
		speed = WalkSpeed::Run;

	int8_t skippedFrames = std::max(vanillaSkippedFrames, SkippedWalkFramesForSpeed(speed));
	// The walk only ends when the last frame is reached, keep at least two animation frames so the
	// movement is still interpolated between tiles.
	const int8_t maxSkippedFrames = static_cast<int8_t>(std::max(player._pWFrames - 3, 0));
	return std::min(skippedFrames, std::max(maxSkippedFrames, vanillaSkippedFrames));
}

bool IsFastWalking(const Player &player, int8_t vanillaSkippedFrames)
{
	return GetWalkSkippedFrames(player, vanillaSkippedFrames) != vanillaSkippedFrames;
}

bool IsInvincible(const Player &player)
{
	return AppliesTo(player) && *sgOptions.Xeated.invincible;
}

bool HasInfiniteMana(const Player &player)
{
	return AppliesTo(player) && *sgOptions.Xeated.infiniteMana;
}

bool HasOneHitKill(const Player &player)
{
	return AppliesTo(player) && *sgOptions.Xeated.oneHitKill;
}

bool HasNoDurabilityLoss(const Player &player)
{
	return AppliesTo(player) && *sgOptions.Xeated.noDurabilityLoss;
}

uint32_t ScaleExperience(const Player &player, uint32_t experience)
{
	if (!AppliesTo(player))
		return experience;
	const uint64_t scaled = static_cast<uint64_t>(experience) * std::max(*sgOptions.Xeated.experienceRate, 0) / 100;
	return static_cast<uint32_t>(std::min<uint64_t>(scaled, std::numeric_limits<uint32_t>::max()));
}

int ScaleGoldPile(int value)
{
	if (!IsActive())
		return value;
	const int64_t scaled = static_cast<int64_t>(value) * std::max(*sgOptions.Xeated.goldRate, 0) / 100;
	return static_cast<int>(clamp<int64_t>(scaled, 1, GOLD_MAX_LIMIT));
}

int RollMonsterDropCount()
{
	if (!IsActive())
		return 1;
	const int rate = std::max(*sgOptions.Xeated.dropRate, 100);
	int count = rate / 100;
	if (rate % 100 != 0 && GenerateRnd(100) < rate % 100)
		count++;
	return count;
}

DropQuality GetDropQuality()
{
	if (!IsActive())
		return DropQuality::Normal;
	return *sgOptions.Xeated.dropQuality;
}

int GetItemLevelBonus()
{
	if (!IsActive())
		return 0;
	return clamp(*sgOptions.Xeated.itemLevelBonus, 0, MaxItemLevel);
}

void ProcessGameTick()
{
	Player *player = ActivePlayer();
	if (player == nullptr || player->_pHitPoints <= 0 || player->_pmode == PM_DEATH)
		return;

	if (*sgOptions.Xeated.invincible && player->_pHitPoints < player->_pMaxHP) {
		player->RestoreFullLife();
		RedrawComponent(PanelDrawComponent::Health);
	}
	if (*sgOptions.Xeated.infiniteMana && player->_pMana < player->_pMaxMana) {
		player->RestoreFullMana();
		RedrawComponent(PanelDrawComponent::Mana);
	}
}

std::string RestoreHealthAndMana()
{
	Player *player = ActivePlayer();
	if (player == nullptr)
		return NotAvailable();
	if (player->_pHitPoints <= 0)
		return std::string(_("The dead can't be healed."));

	player->RestoreFullLife();
	player->RestoreFullMana();
	RedrawComponent(PanelDrawComponent::Health);
	RedrawComponent(PanelDrawComponent::Mana);
	return std::string(_("Life and mana restored."));
}

std::string LevelUp(int levels)
{
	Player *player = ActivePlayer();
	if (player == nullptr)
		return NotAvailable();
	if (player->_pLevel >= MaxCharacterLevel)
		return std::string(_("Already at the maximum level."));

	for (int i = 0; i < levels && player->_pLevel < MaxCharacterLevel; i++) {
		player->_pExperience = player->_pNextExper;
		NextPlrLevel(*player);
	}
	RedrawEverything();
	return fmt::format(fmt::runtime(_("Level {:d} reached.")), player->_pLevel);
}

std::string AddStatPoints(int points)
{
	Player *player = ActivePlayer();
	if (player == nullptr)
		return NotAvailable();

	player->_pStatPts += points;
	RedrawEverything();
	return fmt::format(fmt::runtime(_("{:d} stat points to distribute in the character panel.")), player->_pStatPts);
}

std::string MaximizeStats()
{
	Player *player = ActivePlayer();
	if (player == nullptr)
		return NotAvailable();

	ModifyPlrStr(*player, player->GetMaximumAttributeValue(CharacterAttribute::Strength) - player->_pBaseStr);
	ModifyPlrMag(*player, player->GetMaximumAttributeValue(CharacterAttribute::Magic) - player->_pBaseMag);
	ModifyPlrDex(*player, player->GetMaximumAttributeValue(CharacterAttribute::Dexterity) - player->_pBaseDex);
	ModifyPlrVit(*player, player->GetMaximumAttributeValue(CharacterAttribute::Vitality) - player->_pBaseVit);
	RedrawEverything();
	return std::string(_("All attributes at their maximum."));
}

std::string LearnAllSpells()
{
	Player *player = ActivePlayer();
	if (player == nullptr)
		return NotAvailable();

	int learned = 0;
	for (uint8_t i = static_cast<uint8_t>(SpellID::Firebolt); i < MAX_SPELLS; i++) {
		const auto spell = static_cast<SpellID>(i);
		if (GetSpellBookLevel(spell) == -1)
			continue;
		if ((player->_pMemSpells & GetSpellBitmask(spell)) == 0 || player->_pSplLvl[i] == 0)
			learned++;
		player->_pMemSpells |= GetSpellBitmask(spell);
		player->_pSplLvl[i] = std::max<uint8_t>(player->_pSplLvl[i], 1);
	}
	RedrawEverything();
	return fmt::format(fmt::runtime(_("{:d} new spells learned.")), learned);
}

std::string RaiseSpellLevels(int levels)
{
	Player *player = ActivePlayer();
	if (player == nullptr)
		return NotAvailable();

	int raised = 0;
	for (uint8_t i = static_cast<uint8_t>(SpellID::Firebolt); i < MAX_SPELLS; i++) {
		const auto spell = static_cast<SpellID>(i);
		if (GetSpellBookLevel(spell) == -1 || (player->_pMemSpells & GetSpellBitmask(spell)) == 0)
			continue;
		const uint8_t newLevel = static_cast<uint8_t>(std::min<int>(player->_pSplLvl[i] + levels, MaxSpellLevel));
		if (newLevel != player->_pSplLvl[i])
			raised++;
		player->_pSplLvl[i] = newLevel;
	}
	RedrawEverything();
	if (raised == 0)
		return std::string(_("No known spell can be raised. Learn spells first."));
	return fmt::format(fmt::runtime(_("{:d} spells raised.")), raised);
}

std::string AddGold(int amount)
{
	Player *player = ActivePlayer();
	if (player == nullptr)
		return NotAvailable();

	const int remaining = AddGoldToInventory(*player, amount);
	player->_pGold = CalculateGold(*player);
	if (remaining > 0) {
		Stash.gold = static_cast<int>(std::min<int64_t>(static_cast<int64_t>(Stash.gold) + remaining, std::numeric_limits<int>::max()));
		Stash.dirty = true;
	}
	RedrawEverything();
	if (remaining > 0)
		return fmt::format(fmt::runtime(_("{:d} gold added ({:d} went to the stash).")), amount, remaining);
	return fmt::format(fmt::runtime(_("{:d} gold added to the inventory.")), amount);
}

std::string RevealMap()
{
	if (ActivePlayer() == nullptr)
		return NotAvailable();
	if (leveltype == DTYPE_TOWN)
		return std::string(_("The town is already known."));

	for (int x = 0; x < DMAXX; x++)
		for (int y = 0; y < DMAXY; y++)
			UpdateAutomapExplorer({ x, y }, MAP_EXP_SHRINE);
	return std::string(_("Map revealed. Open the automap to see it."));
}

std::string IdentifyAllItems()
{
	Player *player = ActivePlayer();
	if (player == nullptr)
		return NotAvailable();

	int identified = 0;
	ForEachPlayerItem(*player, [&identified](Item &item) {
		if (item.isEmpty() || item._iIdentified || item._iMagical == ITEM_QUALITY_NORMAL)
			return;
		item._iIdentified = true;
		identified++;
	});
	CalcPlrInv(*player, true);
	RedrawEverything();
	return fmt::format(fmt::runtime(_("{:d} items identified.")), identified);
}

std::string RepairAllItems()
{
	Player *player = ActivePlayer();
	if (player == nullptr)
		return NotAvailable();

	int repaired = 0;
	ForEachPlayerItem(*player, [&repaired](Item &item) {
		if (item.isEmpty() || item._iDurability == DUR_INDESTRUCTIBLE || item._iDurability >= item._iMaxDur)
			return;
		item._iDurability = item._iMaxDur;
		repaired++;
	});
	CalcPlrInv(*player, true);
	RedrawEverything();
	return fmt::format(fmt::runtime(_("{:d} items repaired.")), repaired);
}

int MaxWarpLevel()
{
	if (gbIsSpawn)
		return 2;
	return gbIsHellfire ? 24 : 16;
}

std::string WarpToLevel(int level)
{
	Player *player = ActivePlayer();
	if (player == nullptr)
		return NotAvailable();
	if (level < 1 || level > MaxWarpLevel())
		return std::string(_("This level doesn't exist in this game."));
	if (player->_pHitPoints <= 0 || player->_pmode == PM_DEATH)
		return std::string(_("The dead can't travel."));
	if (!setlevel && player->isOnLevel(level))
		return std::string(_("You are already on this level."));

	gamemenu_off();
	// Hellfire's hive and crypt are entered from town through their own warps.
	const interface_mode mode = (level == 17 || level == 21) ? WM_DIABTOWNWARP : WM_DIABNEXTLVL;
	StartNewLvl(*player, mode, level);
	return fmt::format(fmt::runtime(_("Traveling to level {:d}...")), level);
}

} // namespace xeated

} // namespace devilution
