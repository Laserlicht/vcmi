/*
 * H3RewardValue.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3Context.h"
#include "H3Valuations.h"

#include "../../lib/ResourceSet.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/int3.h"

#include <map>
#include <vector>

VCMI_LIB_NAMESPACE_BEGIN
class CGObjectInstance;
class CGHeroInstance;
class CStackBasicDescriptor;

namespace Rewardable
{
struct Reward;
}
VCMI_LIB_NAMESPACE_END

namespace H3AI
{

/// What one reward of a map object would hand this hero, flattened to the fields the
/// SS 4.8 handlers price.
///
/// The original AI reads an object's contents straight out of the map record - SS 4.8's
/// Pandora arm, the creature-bank loot table, the shrine's spell and the witch hut's
/// skill are all direct memory reads.  VCMI keeps the same information on the object as
/// a resolved Rewardable::Configuration (the random roll is baked in at map init), so
/// the transcription reads it there.
struct RewardContents
{
	TResources resources;
	int64_t experience = 0;
	/// Resolved for the hero, so a "half your mana pool" reward is already in points.
	int manaPoints = 0;
	int movePoints = 0;
	int luck = 0;
	int morale = 0;
	/// Indexed by PrimarySkill, always four entries.
	std::vector<int> primary = std::vector<int>(4, 0);
	std::map<SecondarySkill, int> secondary;
	std::vector<ArtifactID> artifacts;
	/// Spells the reward teaches, including the ones granted as scrolls.
	std::vector<SpellID> spells;
	std::vector<std::pair<CreatureID, int>> creatures;
	/// 0 when the reward reveals nothing, -1 for "the whole map".
	int revealRadius = 0;

	bool empty() const;
};

/// Every first-visit reward `object` would offer `hero`, in configuration order.  Empty
/// for an object that is not rewardable, or whose rewards this hero cannot take.
std::vector<RewardContents> readRewards(const CGObjectInstance * object, const CGHeroInstance * hero);

/// The same for a single Rewardable::Reward, for callers that hold one directly (the
/// Seer Hut reads its quest's reward rather than the object's configuration).
RewardContents readReward(const Rewardable::Reward & reward, const CGHeroInstance * hero);

/// SS 4.8's Pandora arm - "the most complete evaluator" - which is also the shape the
/// creature-bank, campfire and chest arms reuse:
///
///   + rewardExp * hero->xpValue
///   + AI_resource_cost(playerData, rewardResources)
///   + sum_i rewardPrimarySkill[i] * expForNextLevel * xpValue
///   + sum over reward creature stacks: (count - alreadyOwned) * creature AI value
///   + artifactCount * playerData.artifactValue
///   + sum over reward spells: AI_get_spell_value(hero, spell)
///   + morale / luck / mp deltas
///
/// @param moveLimit  charged for a movement-point grant exactly as the Oasis arm does.
int64_t valueOfReward(H3Context & ctx, const CGHeroInstance * hero, const HeroValuations & val,
	const RewardContents & reward, int & moveLimit);

/// SS 4.8a - 0x432220: the number of tiles a scouting reward centred on `center` would
/// newly reveal.  A radius of 0 or less means the whole map.
int newlyRevealedTiles(H3Context & ctx, const int3 & center, int radius);

/// The value of the whole object: each of its first-visit rewards priced with
/// valueOfReward, then combined the way the object grants them - summed when it grants
/// all of them, otherwise the best one, because that is the one the player picks.
int64_t valueOfObjectRewards(H3Context & ctx, const CGHeroInstance * hero, const HeroValuations & val,
	const CGObjectInstance * object, int & moveLimit);

}
