/**
 * @file xeated/xeated.hpp
 *
 * Xeated Edition: walk speed options and single player cheats.
 */
#pragma once

#include <cstdint>
#include <string>

#include "options.h"

namespace devilution {

struct Player;

namespace xeated {

/** @brief Version of the Xeated Edition changes (the base DevilutionX version is PROJECT_VERSION). */
constexpr const char XeatedVersion[] = "1.0.0";

/** @brief Walk animation frames skipped for each WalkSpeed preset. */
int8_t SkippedWalkFramesForSpeed(WalkSpeed speed);

/** @brief Whether the Xeated Edition options are applied in the current game (single player only). */
bool IsActive();

/** @brief Whether the Xeated Edition options are applied to the given player. */
bool AppliesTo(const Player &player);

/**
 * @brief Returns the number of walk animation frames to skip for the player.
 * @param player The walking player
 * @param vanillaSkippedFrames The value used by the unmodified game (-2 walking, 2 running in town)
 */
int8_t GetWalkSkippedFrames(const Player &player, int8_t vanillaSkippedFrames);

/** @brief Whether the player walks faster than the unmodified game would allow. */
bool IsFastWalking(const Player &player, int8_t vanillaSkippedFrames);

bool IsInvincible(const Player &player);
bool HasInfiniteMana(const Player &player);
bool HasOneHitKill(const Player &player);
bool HasNoDurabilityLoss(const Player &player);

/** @brief Applies the experience rate cheat to the experience gained by the player. */
uint32_t ScaleExperience(const Player &player, uint32_t experience);

/** @brief Applies the gold rate cheat to a dropped gold pile, keeping it within the pile limit. */
int ScaleGoldPile(int value);

/**
 * @brief Number of item rolls a killed monster gets according to the drop rate cheat.
 *
 * Returns 1 when the cheat is inactive. Fractions of 100% give a chance of one extra roll.
 */
int RollMonsterDropCount();

/** @brief Drop quality to use for monster drops. */
DropQuality GetDropQuality();

/** @brief Bonus to add to the level of items dropped by monsters. */
int GetItemLevelBonus();

/** @brief Called once per game tick to apply continuous cheats (invincibility, infinite mana). */
void ProcessGameTick();

// Instant actions of the cheat menu. Each returns a message describing the result.
std::string RestoreHealthAndMana();
std::string LevelUp(int levels);
std::string AddStatPoints(int points);
std::string MaximizeStats();
std::string LearnAllSpells();
std::string RaiseSpellLevels(int levels);
std::string AddGold(int amount);
std::string RevealMap();
std::string IdentifyAllItems();
std::string RepairAllItems();
/** @brief Highest dungeon level that can be warped to in the current game. */
int MaxWarpLevel();
std::string WarpToLevel(int level);

} // namespace xeated

} // namespace devilution
