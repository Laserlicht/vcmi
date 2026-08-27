/*
 * H3RewardValue.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3RewardValue.h"

#include "H3Constants.h"
#include "H3SecondarySkills.h"
#include "H3SpellValue.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/rewardable/Interface.h"
#include "../../lib/spells/CSpellHandler.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace H3AI
{

namespace
{
/// The luck / morale a reward's hero bonuses add up to.  The original reads the bonus
/// straight out of the object record; VCMI expresses it as a Bonus list, so the same
/// number is recovered by summing the LUCK / MORALE entries.
int bonusTotal(const std::vector<std::shared_ptr<Bonus>> & bonuses, BonusType type)
{
	int total = 0;

	for(const auto & bonus : bonuses)
		if(bonus != nullptr && bonus->type == type)
			total += bonus->val;

	return total;
}
}

bool RewardContents::empty() const
{
	return !resources.nonZero()
		&& experience == 0
		&& manaPoints == 0
		&& movePoints == 0
		&& luck == 0
		&& morale == 0
		&& secondary.empty()
		&& artifacts.empty()
		&& spells.empty()
		&& creatures.empty()
		&& revealRadius == 0
		&& std::all_of(primary.begin(), primary.end(), [](int v) { return v == 0; });
}

RewardContents readReward(const Rewardable::Reward & reward, const CGHeroInstance * hero)
{
	RewardContents out;

	out.resources = reward.resources;

	// A reward may grant whole levels instead of raw experience; SS 4.8 prices only the
	// experience, so the levels are converted the way the engine converts them.
	out.experience = reward.heroExperience;

	if(hero != nullptr)
	{
		for(int level = 0; level < reward.heroLevel; ++level)
			out.experience += experienceForLevel(static_cast<int>(hero->level) + level + 1)
				- experienceForLevel(static_cast<int>(hero->level) + level);

		out.manaPoints = reward.calculateManaPoints(hero);
		out.movePoints = reward.calculateMovePoints(hero);
	}
	else
	{
		out.manaPoints = reward.manaDiff;
		out.movePoints = reward.movePoints;
	}

	out.luck = bonusTotal(reward.heroBonuses, BonusType::LUCK);
	out.morale = bonusTotal(reward.heroBonuses, BonusType::MORALE);

	for(size_t i = 0; i < out.primary.size() && i < reward.primary.size(); ++i)
		out.primary[i] = reward.primary[i];

	for(const auto & skill : reward.secondary)
		out.secondary[skill.first] = skill.second;

	out.artifacts = reward.grantedArtifacts;
	out.spells = reward.spells;

	// A granted scroll teaches its spell for as long as it is carried, which is what the
	// AI is pricing; the scroll's own artifact value is the floor of 10 either way.
	out.spells.insert(out.spells.end(), reward.grantedScrolls.begin(), reward.grantedScrolls.end());

	for(const CStackBasicDescriptor & stack : reward.creatures)
		out.creatures.emplace_back(stack.getId(), stack.getCount());

	if(reward.revealTiles.has_value())
		out.revealRadius = reward.revealTiles->radius > 0 ? reward.revealTiles->radius : -1;

	return out;
}

std::vector<RewardContents> readRewards(const CGObjectInstance * object, const CGHeroInstance * hero)
{
	std::vector<RewardContents> out;

	const auto * rewardable = dynamic_cast<const Rewardable::Interface *>(object);

	if(rewardable == nullptr)
		return out;

	for(ui32 index : rewardable->getAvailableRewards(hero, Rewardable::EEventType::EVENT_FIRST_VISIT))
		out.push_back(readReward(rewardable->configuration.info.at(index).reward, hero));

	return out;
}

int64_t valueOfReward(H3Context & ctx, const CGHeroInstance * hero, const HeroValuations & val,
	const RewardContents & reward, int & moveLimit)
{
	if(hero == nullptr)
		return 0;

	int64_t value = 0;

	// + rewardExp * hero->xpValue
	value += static_cast<int64_t>(static_cast<double>(reward.experience) * val.experienceValue);

	// + AI_resource_cost(playerData, rewardResources)
	value += ctx.player->resourceCost(reward.resources);

	// + sum_i rewardPrimarySkill[i] * expForNextLevel * xpValue.  SS 4.8 prices a
	// primary-skill point as a level's worth of experience; the School of War arm
	// (0x52B790) is the same shape without the skill factor.
	const int primaryPoints = std::accumulate(reward.primary.begin(), reward.primary.end(), 0);

	if(primaryPoints > 0)
		value += static_cast<int64_t>(primaryPoints
			* static_cast<double>(experienceForLevel(hero->level)) * val.experienceValue);

	// + sum over reward creature stacks: (count - alreadyOwnedOfThatType) * AI value,
	// counted only when the hero could actually take the stack.
	const bool hasFreeSlot = hero->stacksCount() < GameConstants::ARMY_SIZE;

	for(const auto & stack : reward.creatures)
	{
		const CCreature * creature = stack.first.toCreature();

		if(creature == nullptr)
			continue;

		int owned = 0;

		for(const auto & slot : hero->Slots())
			if(hero->getCreature(slot.first) != nullptr && hero->getCreature(slot.first)->getId() == stack.first)
				owned += hero->getStackCount(slot.first);

		if(!hasFreeSlot && owned == 0)
			continue;

		value += static_cast<int64_t>(std::max(0, stack.second - owned)) * creature->getAIValue();
	}

	// + artifactCount * playerData.artifactValue.  SS 4.8 prices the pile by the
	// player's average artifact value rather than by the artifacts themselves, even
	// though the contents are known - reproduced rather than "improved".
	value += static_cast<int64_t>(reward.artifacts.size()) * ctx.player->artifactValue();

	// + sum over reward spells: AI_get_spell_value(hero, spell), with the three gates
	// AI_get_spell_value applies (Wisdom, already known, no spellbook).
	for(const SpellID & spell : reward.spells)
	{
		const CSpell * spellData = spell.toSpell();

		if(spellData == nullptr || !hero->hasSpellbook() || hero->spellbookContainsSpell(spell))
			continue;

		if(spellData->getLevel() > hero->getSecSkillLevel(SecondarySkill::WISDOM) + 2)
			continue;

		value += aiGetSpellValue(hero, spell);
	}

	// + the secondary skills, through the SS 4.12 valuer the Witch Hut arm uses.
	for(const auto & skill : reward.secondary)
		if(skill.second > 0 && hero->getSecSkillLevel(skill.first) < skill.second)
			value += secondarySkillValue(ctx, hero, skill.first, true);

	// + morale / luck deltas, scaled into absolute army value the way every SS 4.8
	// luck/morale handler scales them.
	if(reward.morale != 0)
		value += luckMoraleToAbsolute(hero, valueOfMorale(currentMorale(hero), reward.morale));

	if(reward.luck != 0)
		value += luckMoraleToAbsolute(hero, valueOfLuck(currentLuck(hero), reward.luck));

	// + the mana refill.  SS 4.9b precomputes only the two refill sizes the Magic Well
	// and Magic Spring grant, so a partial refill is priced as that fraction of a full
	// one - the reading, stated rather than guessed.
	if(reward.manaPoints > 0 && hero->manaLimit() > 0)
	{
		const double fraction = std::min(1.0,
			static_cast<double>(reward.manaPoints) / static_cast<double>(hero->manaLimit()));

		value += static_cast<int64_t>(fraction * val.valueOfFullMana);
	}

	// + the movement grant, charged to moveLimit exactly as the Oasis arm charges it:
	// a grant that covers the whole approach makes the object free to visit.
	if(reward.movePoints > 0)
	{
		if(moveLimit < reward.movePoints)
		{
			moveLimit = 0;
			value += MOVEMENT_GRANT_SENTINEL;
		}
		else
		{
			moveLimit -= reward.movePoints;
		}
	}

	return value;
}

int newlyRevealedTiles(H3Context & ctx, const int3 & center, int radius)
{
	// SS 4.8a - 0x432220: one point per tile the object would newly reveal.  The
	// per-object-type bonus for anything revealed with an object on it (0x6925AC) is a
	// map-content lookup the AI cannot reach through a callback; the tile count is the
	// dominant term.
	int revealed = 0;

	if(radius <= 0)
	{
		// A reward with no radius reveals the whole map (Cartographer, the View spells).
		const int3 size = ctx.cb->getMapSize();

		for(int z = 0; z < size.z; ++z)
			for(int y = 0; y < size.y; ++y)
				for(int x = 0; x < size.x; ++x)
					if(!ctx.cb->isVisible(int3(x, y, z)))
						++revealed;

		return revealed;
	}

	for(int dy = -radius; dy <= radius; ++dy)
	{
		for(int dx = -radius; dx <= radius; ++dx)
		{
			if(std::sqrt(static_cast<double>(dx * dx + dy * dy)) > radius + 0.5)
				continue;

			const int3 probe(center.x + dx, center.y + dy, center.z);

			if(ctx.cb->isInTheMap(probe) && !ctx.cb->isVisible(probe))
				++revealed;
		}
	}

	return revealed;
}

int64_t valueOfObjectRewards(H3Context & ctx, const CGHeroInstance * hero, const HeroValuations & val,
	const CGObjectInstance * object, int & moveLimit)
{
	const auto * rewardable = dynamic_cast<const Rewardable::Interface *>(object);

	if(rewardable == nullptr || hero == nullptr)
		return 0;

	const std::vector<RewardContents> rewards = readRewards(object, hero);

	if(rewards.empty())
		return 0;

	// An object that grants everything it holds is worth the sum; one that offers a
	// choice is worth the branch the player would take, which is the best of them.
	const bool grantsAll = rewardable->configuration.selectMode == Rewardable::SELECT_ALL;

	int64_t total = 0;
	int64_t best = 0;
	int bestLimit = moveLimit;

	for(const RewardContents & reward : rewards)
	{
		int limit = moveLimit;
		int64_t value = valueOfReward(ctx, hero, val, reward, grantsAll ? moveLimit : limit);

		if(reward.revealRadius != 0)
			value += newlyRevealedTiles(ctx, object->visitablePos(), reward.revealRadius);

		if(grantsAll)
		{
			total += value;
		}
		else if(value > best)
		{
			best = value;
			bestLimit = limit;
		}
	}

	if(grantsAll)
		return total;

	moveLimit = bestLimit;

	return best;
}

}
