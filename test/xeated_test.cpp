#include <gtest/gtest.h>

#include "engine/animationinfo.h"
#include "inv.h"
#include "items.h"
#include "multi.h"
#include "options.h"
#include "player.h"
#include "qol/stash.h"
#include "storm/storm_net.hpp"
#include "xeated/xeated.hpp"

namespace devilution {
namespace {

class XeatedTest : public ::testing::Test {
public:
	void SetUp() override
	{
		Players.resize(1);
		MyPlayer = &Players[0];
		*MyPlayer = {};
		MyPlayer->_pWFrames = 8;
		gbIsMultiplayer = false;
		SNetInitializeProvider(SELCONN_LOOPBACK, nullptr);
		XeatedOptions &options = sgOptions.Xeated;
		options.walkSpeed.SetValue(WalkSpeed::Normal);
		options.alwaysRun.SetValue(false);
		options.invincible.SetValue(false);
		options.infiniteMana.SetValue(false);
		options.oneHitKill.SetValue(false);
		options.noDurabilityLoss.SetValue(false);
		options.experienceRate.SetValue(100);
		options.goldRate.SetValue(100);
		options.dropRate.SetValue(100);
		options.dropQuality.SetValue(DropQuality::Normal);
		options.itemLevelBonus.SetValue(0);
	}

	void TearDown() override
	{
		gbIsMultiplayer = false;
	}
};

/** Number of game ticks a walk takes with the given amount of skipped frames (same steps as StartWalk + DoWalk). */
int WalkTicks(int8_t skippedFrames)
{
	AnimationInfo animation {};
	// StartWalk is followed by ProcessPlayer in the same game tick, so one more frame is skipped.
	animation.setNewAnimation(std::nullopt, 8, 1, AnimationDistributionFlags::ProcessAnimationPending, skippedFrames + 1);
	int ticks = 0;
	while (true) {
		ticks++;
		// DoWalk checks for the last frame before the animation advances.
		if (animation.isLastFrame())
			return ticks - 1;
		animation.processAnimation();
		if (ticks > 100)
			return -1;
	}
}

int WalkTicksForSpeed(WalkSpeed speed)
{
	sgOptions.Xeated.walkSpeed.SetValue(speed);
	return WalkTicks(xeated::GetWalkSkippedFrames(*MyPlayer, -2));
}

TEST_F(XeatedTest, WalkSpeedPresets)
{
	EXPECT_EQ(WalkTicksForSpeed(WalkSpeed::Normal), WalkTicks(-2));
	EXPECT_EQ(WalkTicksForSpeed(WalkSpeed::Run), WalkTicks(2)); // same speed as Hellfire's run in town
	const int normal = WalkTicksForSpeed(WalkSpeed::Normal);
	const int fast = WalkTicksForSpeed(WalkSpeed::Fast);
	const int run = WalkTicksForSpeed(WalkSpeed::Run);
	const int faster = WalkTicksForSpeed(WalkSpeed::Faster);
	const int turbo = WalkTicksForSpeed(WalkSpeed::Turbo);
	EXPECT_GT(normal, fast);
	EXPECT_GT(fast, run);
	EXPECT_GT(run, faster);
	EXPECT_GT(faster, turbo);
	EXPECT_EQ(normal, 8);
	EXPECT_EQ(fast, 6);
	EXPECT_EQ(run, 4);
	EXPECT_EQ(faster, 3);
	EXPECT_EQ(turbo, 2);
}

TEST_F(XeatedTest, WalkSpeedNormalKeepsVanillaValues)
{
	EXPECT_EQ(xeated::GetWalkSkippedFrames(*MyPlayer, -2), -2);
	// Hellfire's run in town is never slowed down
	EXPECT_EQ(xeated::GetWalkSkippedFrames(*MyPlayer, 2), 2);
	EXPECT_FALSE(xeated::IsFastWalking(*MyPlayer, -2));
}

TEST_F(XeatedTest, AlwaysRunUsesRunSpeedEverywhere)
{
	sgOptions.Xeated.alwaysRun.SetValue(true);
	EXPECT_EQ(xeated::GetWalkSkippedFrames(*MyPlayer, -2), xeated::SkippedWalkFramesForSpeed(WalkSpeed::Run));
	EXPECT_TRUE(xeated::IsFastWalking(*MyPlayer, -2));
	// A faster walk speed is not slowed down by always run
	sgOptions.Xeated.walkSpeed.SetValue(WalkSpeed::Turbo);
	EXPECT_EQ(xeated::GetWalkSkippedFrames(*MyPlayer, -2), xeated::SkippedWalkFramesForSpeed(WalkSpeed::Turbo));
}

TEST_F(XeatedTest, WalkSpeedKeepsAnimationFrames)
{
	sgOptions.Xeated.walkSpeed.SetValue(WalkSpeed::Turbo);
	MyPlayer->_pWFrames = 4;
	// With only 4 frames the walk must still end on the last frame
	EXPECT_LE(xeated::GetWalkSkippedFrames(*MyPlayer, -2) + 1, MyPlayer->_pWFrames - 2);
}

TEST_F(XeatedTest, DisabledInMultiplayer)
{
	sgOptions.Xeated.walkSpeed.SetValue(WalkSpeed::Turbo);
	sgOptions.Xeated.alwaysRun.SetValue(true);
	sgOptions.Xeated.invincible.SetValue(true);
	sgOptions.Xeated.infiniteMana.SetValue(true);
	sgOptions.Xeated.oneHitKill.SetValue(true);
	sgOptions.Xeated.experienceRate.SetValue(1000);
	sgOptions.Xeated.goldRate.SetValue(1000);
	sgOptions.Xeated.dropRate.SetValue(1000);
	sgOptions.Xeated.dropQuality.SetValue(DropQuality::Unique);
	sgOptions.Xeated.itemLevelBonus.SetValue(30);
	gbIsMultiplayer = true;

	EXPECT_EQ(xeated::GetWalkSkippedFrames(*MyPlayer, -2), -2);
	EXPECT_FALSE(xeated::IsInvincible(*MyPlayer));
	EXPECT_FALSE(xeated::HasInfiniteMana(*MyPlayer));
	EXPECT_FALSE(xeated::HasOneHitKill(*MyPlayer));
	EXPECT_EQ(xeated::ScaleExperience(*MyPlayer, 100), 100U);
	EXPECT_EQ(xeated::ScaleGoldPile(100), 100);
	EXPECT_EQ(xeated::RollMonsterDropCount(), 1);
	EXPECT_EQ(xeated::GetDropQuality(), DropQuality::Normal);
	EXPECT_EQ(xeated::GetItemLevelBonus(), 0);
	EXPECT_NE(xeated::AddGold(1000).find("single player"), std::string::npos);
}

TEST_F(XeatedTest, OnlyAppliesToLocalPlayer)
{
	sgOptions.Xeated.invincible.SetValue(true);
	Players.resize(2);
	MyPlayer = &Players[0];
	EXPECT_TRUE(xeated::IsInvincible(Players[0]));
	EXPECT_FALSE(xeated::IsInvincible(Players[1]));
}

TEST_F(XeatedTest, ExperienceRate)
{
	EXPECT_EQ(xeated::ScaleExperience(*MyPlayer, 250), 250U);
	sgOptions.Xeated.experienceRate.SetValue(300);
	EXPECT_EQ(xeated::ScaleExperience(*MyPlayer, 250), 750U);
	sgOptions.Xeated.experienceRate.SetValue(2000);
	EXPECT_EQ(xeated::ScaleExperience(*MyPlayer, 4000000000U), 0xFFFFFFFFU);
}

TEST_F(XeatedTest, AddPlrExperienceUsesRate)
{
	MyPlayer->_pLevel = 1;
	MyPlayer->_pHitPoints = 10 << 6;
	MyPlayer->_pExperience = 0;
	sgOptions.Gameplay.experienceBar.SetValue(false);
	sgOptions.Xeated.experienceRate.SetValue(500);
	AddPlrExperience(*MyPlayer, 1, 100);
	EXPECT_EQ(MyPlayer->_pExperience, 500U);
}

TEST_F(XeatedTest, GoldRateKeepsPileLimit)
{
	EXPECT_EQ(xeated::ScaleGoldPile(321), 321);
	sgOptions.Xeated.goldRate.SetValue(300);
	EXPECT_EQ(xeated::ScaleGoldPile(321), 963);
	sgOptions.Xeated.goldRate.SetValue(1000);
	EXPECT_EQ(xeated::ScaleGoldPile(4000), GOLD_MAX_LIMIT);
}

TEST_F(XeatedTest, DropRateRolls)
{
	EXPECT_EQ(xeated::RollMonsterDropCount(), 1);
	sgOptions.Xeated.dropRate.SetValue(300);
	EXPECT_EQ(xeated::RollMonsterDropCount(), 3);
	sgOptions.Xeated.dropRate.SetValue(150);
	int extra = 0;
	for (int i = 0; i < 1000; i++) {
		const int count = xeated::RollMonsterDropCount();
		ASSERT_TRUE(count == 1 || count == 2);
		extra += count - 1;
	}
	// about half of the kills get an extra roll
	EXPECT_GT(extra, 350);
	EXPECT_LT(extra, 650);
}

TEST_F(XeatedTest, InvincibleIgnoresDamage)
{
	Player &player = *MyPlayer;
	player._pMaxHP = player._pMaxHPBase = player._pHitPoints = player._pHPBase = 100 << 6;

	sgOptions.Xeated.invincible.SetValue(true);
	ApplyPlrDamage(DamageType::Physical, player, 40);
	EXPECT_EQ(player._pHitPoints, 100 << 6);

	sgOptions.Xeated.invincible.SetValue(false);
	ApplyPlrDamage(DamageType::Physical, player, 40);
	EXPECT_EQ(player._pHitPoints, 60 << 6);
}

TEST_F(XeatedTest, ProcessGameTickRefills)
{
	Player &player = *MyPlayer;
	player._pMaxHP = player._pMaxHPBase = 100 << 6;
	player._pHitPoints = player._pHPBase = 30 << 6;
	player._pMaxMana = player._pMaxManaBase = 50 << 6;
	player._pMana = player._pManaBase = 5 << 6;

	xeated::ProcessGameTick();
	EXPECT_EQ(player._pHitPoints, 30 << 6);
	EXPECT_EQ(player._pMana, 5 << 6);

	sgOptions.Xeated.invincible.SetValue(true);
	sgOptions.Xeated.infiniteMana.SetValue(true);
	xeated::ProcessGameTick();
	EXPECT_EQ(player._pHitPoints, 100 << 6);
	EXPECT_EQ(player._pMana, 50 << 6);
}

TEST_F(XeatedTest, AddGoldFillsInventoryThenStash)
{
	Stash.gold = 0;
	xeated::AddGold(12000);
	EXPECT_EQ(MyPlayer->_pGold, 12000);
	EXPECT_EQ(CalculateGold(*MyPlayer), 12000);
	EXPECT_EQ(Stash.gold, 0);

	// 40 inventory cells hold at most 200000 gold
	xeated::AddGold(1000000);
	EXPECT_EQ(MyPlayer->_pGold, InventoryGridCells * GOLD_MAX_LIMIT);
	EXPECT_EQ(Stash.gold, 1012000 - InventoryGridCells * GOLD_MAX_LIMIT);
}

TEST_F(XeatedTest, AddStatPoints)
{
	MyPlayer->_pStatPts = 2;
	xeated::AddStatPoints(5);
	EXPECT_EQ(MyPlayer->_pStatPts, 7);
}

TEST_F(XeatedTest, RepairAndIdentify)
{
	Item &armor = MyPlayer->InvBody[INVLOC_HAND_RIGHT];
	InitializeItem(armor, IDI_WARRSHLD);
	armor._iMaxDur = 30;
	armor._iDurability = 3;
	armor._iMagical = ITEM_QUALITY_MAGIC;
	armor._iIdentified = false;

	xeated::RepairAllItems();
	xeated::IdentifyAllItems();
	EXPECT_EQ(armor._iDurability, 30);
	EXPECT_TRUE(armor._iIdentified);
}

} // namespace
} // namespace devilution
