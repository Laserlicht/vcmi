/*
 * H3TownValue.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3TownValue.h"

#include "H3ArmyPlanner.h"
#include "H3CombatEstimate.h"
#include "H3ObjectValue.h"
#include "H3Valuations.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/StartInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"

#include <algorithm>

namespace H3AI
{

int townsOnMap(H3Context & ctx)
{
	if(ctx.cachedTownsOnMap >= 0)
		return ctx.cachedTownsOnMap;

	int count = 0;

	for(const CGObjectInstance * object : ctx.cb->getAllVisitableObjs())
		if(object->ID == Obj::TOWN)
			++count;

	ctx.cachedTownsOnMap = std::max(1, count);

	return ctx.cachedTownsOnMap;
}

int townVisitValue(H3Context & ctx, const CGHeroInstance * hero, const CGTownInstance * town)
{
	// SS 4B.3 - hero::AI_town_visit_value @ 0x52B1E0
	if(town == nullptr || hero == nullptr)
		return 0;

	int64_t v = 0;

	auto stateIt = ctx.heroStates->find(hero->id);
	const HeroValuations val = stateIt != ctx.heroStates->end()
		? stateIt->second.valuations
		: computeHeroValuations(hero);

	// SS 4B.3 - the Conflux arm (town type 8) routes to 0x525BF0, the Magic University
	// visit value:
	//   __thiscall(hero *this /*ecx*/, int *skills /*edx*/, bool chargesGold /*stack*/)
	//   if (hero->d[0x101] >= 8)                     return 0;   // 8 secondary skills
	//   if (chargesGold && gpCurPlayer->gold < 2000) return 0;   // pd + 0xB4
	//   total = 0;
	//   for (each of the 4 skills the university offers) {
	//       if (!g_heroClass[hero->d[0x30]].b[0x18 + skill]) continue;  // class cannot learn
	//       if (hero->secSkill[skill] /*+0xC9*/ > 0)         continue;  // already has it
	//       if (!hero::can_learn_skill(hero, skill, 1))      continue;  // 0x524DD0
	//       total += hero::AI_secondary_skill_value(hero, skill, 1);    // 0x524690, SS 4.12
	//   }
	//   return total;
	// The class table is at [0x67DCEC], 64 bytes per class, with the per-skill
	// "this class may learn it" bytes at + 0x18.
	// VCMI has no Magic University object, so there is nothing to evaluate here.

	if(town->mageGuildLevel() > 0)
	{
		if(!hero->hasSpellbook())
		{
			if(ctx.cb->getResourceAmount(GameResID::GOLD) >= 500)
				v += 1000;
		}
		else
		{
			const int maxLevel = hero->getSecSkillLevel(SecondarySkill::WISDOM) + 2;

			for(int level = 0; level < maxLevel && level < static_cast<int>(town->spells.size()); ++level)
			{
				for(const SpellID & spell : town->spells[level])
				{
					if(!hero->spellbookContainsSpell(spell))
						v += spellValue(ctx, hero, spell);
				}
			}
		}
	}

	// SS 4B.3 - the once-per-hero faction special, keyed on the bitmask hero + 0x121
	// indexed by town id.
	if(hero->visitedObjects.count(town->id) == 0)
	{
		switch(town->getFactionID().getNum())
		{
		case 2: // Tower
			// SS 4B.3 - hero->d[0x486], the value of +1 KNOWLEDGE (+0x482 is spell
			// duration; the two were transposed in an earlier reading).
			v += val.valueOfKnowledge;
			break;

		case 3: // Inferno
			v += val.valueOfSpellPower;
			break;

		case 4: // Necropolis - nothing
			break;

		case 5: // Dungeon
			v += static_cast<int64_t>(val.experienceValue * 1000.0f);
			break;

		case 6: // Stronghold
		case 7: // Fortress
			v += static_cast<int64_t>(experienceForLevel(hero->level) * val.experienceValue);
			break;

		default:
			break;
		}
	}

	return static_cast<int>(v);
}

int townRecruitValue(H3Context & ctx, const CGHeroInstance * hero, const CGTownInstance * town, int moveLimit)
{
	// SS 4B.5 - AI_town_recruit_value @ 0x52B090
	if(town == nullptr || hero == nullptr)
		return 0;

	ArmyPlanner planner(ctx.cb, ctx.player);

	// SS 4B.5 - bool flag = playerData::get_flag(pd, 0x81).  SS 4B.4a: artifact 0x81 is
	// 129, the Angelic Alliance; it is passed as the planner's `angelicAlliance` flag,
	// which is what lets the planner ignore the alignment-morale penalty.
	const bool flag = ctx.player->anyHeroHasArtifact(ArtifactID::ANGELIC_ALLIANCE);

	const int64_t exchangeVal = planner.evaluateTroopExchange(hero, town, nullptr, flag);
	const int64_t buyVal = planner.evaluatePurchase(town, hero, flag);

	// SS 4B.5 / SS 4B.4a - refuse an exchange that guts the hero.  `plan.armyValueAfter`
	// is the planner field at plan + 0x18, maintained as the planner buys and exchanges;
	// 0x52B090 reads it at [ebp-0x3C] with the planner at [ebp-0x54].  The guard is
	//   if (moveLimit >= 400 && plan.d[0x18] < armyGroup::get_AI_value(&hero->army) / 3)
	//       return 0;
	// (/3 is the 0x55555556 magic with no shift).
	if(moveLimit >= RECRUIT_VALUE_MOVE_LIMIT)
	{
		const int64_t before = armyAIValue(hero);

		if(before > 0 && (before + exchangeVal) < before / RECRUIT_VALUE_ARMY_DIVISOR)
			return 0;
	}

	return static_cast<int>(buyVal + exchangeVal / 2);
}

int townCaptureValue(H3Context & ctx, const CGHeroInstance * hero, const CGTownInstance * town, int moveLimit)
{
	// SS 4B.2 - hero::AI_town_capture_value @ 0x529CB0
	if(town == nullptr || hero == nullptr)
		return 0;

	const CGHeroInstance * defender = town->getVisitingHero();

	if(defender == nullptr)
		defender = town->getGarrisonHero();

	int64_t combat = valueOfCombat(ctx.cb, *ctx.player, hero, defender, town, town);

	const int ownTowns = ctx.cb->howManyTowns();

	if(combat <= CERTAIN_DEFEAT)
	{
		if(ownTowns > 0)
			return 0;

		combat = HOPELESS_TOWN_FALLBACK;
	}

	// three days of income
	int64_t v = static_cast<int64_t>(town->dailyIncome()[GameResID::GOLD] * ctx.player->goldValue() * 3.0);

	// SS 4B.2 - if the town has the special resource building, three days of its output.
	{
		ResourceSet production = town->dailyIncome();
		production[GameResID::GOLD] = 0;

		if(!production.empty())
			v += 3 * ctx.player->resourceCost(production);
	}

	// SS 4B.2 - weeksAhead = (moveLimit - hero->mp) / hero->maxMp + gpGame->w[0x1F63E].
	// SS 4.8a - gpGame + 0x1F63E is the DAY OF THE WEEK (1..7), not a mode word;
	// + 0x1F640 is the week and + 0x1F642 the month, which the Seer Hut's date
	// arithmetic ((month * 4 + week) - 5) * 7 + day confirms.  Adding the day of the
	// week makes the growth term tip over into "a whole week" late in the week.
	const int maxMp = std::max(1, hero->movementPointsLimit());
	const int weeksAhead = (moveLimit - hero->movementPointsRemaining()) / maxMp;
	const bool wholeWeek = weeksAhead >= 7;

	for(size_t level = 0; level < town->creatures.size(); ++level)
	{
		const auto & tier = town->creatures[level];

		if(tier.second.empty())
			continue;

		int n = static_cast<int>(tier.first) + (wholeWeek ? town->creatureGrowth(static_cast<int>(level)) : 0);

		if(n <= 0)
			continue;

		const CCreature * creature = tier.second.back().toCreature();

		if(creature == nullptr)
			continue;

		const int64_t gain = creature->getAIValue() - ctx.player->resourceCost(creature->getFullRecruitCost());

		if(gain > 0)
			v += gain * n;
	}

	v = static_cast<int64_t>((ctx.player->getAttackBonus(town->getOwner()) + 1.0f) * static_cast<float>(v)) + combat;

	// SS 4B.2 - "The first town is worth a flat 5 000 000; every one after that is worth
	// 5e6 / numTowns.  That single line explains most of the AI's early-game rush."
	v += (ownTowns == 0) ? TOWN_BASE_VALUE : TOWN_BASE_VALUE / townsOnMap(ctx);

	return static_cast<int>(std::clamp<int64_t>(v, ABSOLUTE_NO_GO, std::numeric_limits<int>::max()));
}

int townValue(H3Context & ctx, const CGHeroInstance * hero, const CGTownInstance * town, int & moveLimit)
{
	// SS 4B.1 - hero::AI_town_value @ 0x52AB80
	if(town == nullptr || hero == nullptr)
		return 0;

	if(town->getOwner() != hero->getOwner())
	{
		if(town->getOwner().isValidPlayer()
			&& ctx.cb->getPlayerRelations(town->getOwner(), hero->getOwner()) == PlayerRelations::ALLIES)
		{
			return townVisitValue(ctx, hero, town);
		}

		// SS 4B.1 - the visiting-hero combat evaluation whose result is discarded.
		if(town->getVisitingHero() != nullptr)
			(void)valueOfCombat(ctx.cb, *ctx.player, hero, nullptr, town->getVisitingHero(), nullptr);

		return townCaptureValue(ctx, hero, town, moveLimit);
	}

	// ---- our own town ------------------------------------------------------------
	int64_t v = townRecruitValue(ctx, hero, town, moveLimit) + townVisitValue(ctx, hero, town);

	// Grail delivery
	if(hero->hasArt(ArtifactID::GRAIL)
		&& town->getTown()->buildings.count(BuildingID::GRAIL) > 0
		&& !town->hasBuilt(BuildingID::GRAIL))
	{
		if(ctx.victory.condition == H3VictoryCondition::BUILD_GRAIL
			&& (ctx.victory.targetObject == town->id || ctx.victory.position == town->visitablePos()))
		{
			v += VICTORY_CONDITION_OVERRIDE;
		}
		else
		{
			v += artifactValue(ctx, ArtifactID::GRAIL);
		}
	}

	// SS 4B.1 - victory condition 10 (transport a specific artifact) to this town.
	if(ctx.victory.condition == H3VictoryCondition::TRANSPORT_ARTIFACT
		&& (ctx.victory.targetObject == town->id || ctx.victory.position == town->visitablePos())
		&& hero->hasArt(ctx.victory.artifact))
	{
		v += VICTORY_CONDITION_OVERRIDE;
	}

	// fortified-town baseline
	if(town->hasFort())
	{
		if(ctx.victory.condition == H3VictoryCondition::CAPTURE_TOWN
			&& (ctx.victory.targetObject == town->id || ctx.victory.position == town->visitablePos()))
		{
			v += VICTORY_CONDITION_OVERRIDE;
		}
		else
		{
			v += TOWN_BASE_VALUE / townsOnMap(ctx);
		}
	}

	// SS 4B.1 - artifact hand-off to the hero garrisoned here, over the 5-entry table at
	// 0x640558..0x64056C.  SS 4.9b - the five ids are the Legion set: 118 Legs,
	// 119 Loins, 120 Torso, 121 Arms, 122 Head of Legion (the same table
	// type_statue_of_legion_artifact scans).  The hand-off itself is a game-state
	// mutation, which an AI cannot perform in VCMI; only the valuation is reproduced.

	// SS 4B.1 - hero swap (difficulty > 0 and we own more than one hero).
	if(ctx.cb->howManyHeroes(false) > 1 && ctx.cb->getStartInfo()->difficulty > 0)
	{
		const CGHeroInstance * resident = town->getVisitingHero();

		if(resident != nullptr && (5 * primarySkillSum(resident)) / 4 >= primarySkillSum(hero))
		{
			// the resident is already >= 80 % as strong - "skip the rest"
			return static_cast<int>(v);
		}

		// TODO: the report elides the rest of the swap arm ("...").
	}

	return static_cast<int>(std::clamp<int64_t>(v, ABSOLUTE_NO_GO, std::numeric_limits<int>::max()));
}

}
