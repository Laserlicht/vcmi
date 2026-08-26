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

	// SS 4B.3 - the Conflux arm (town type 8) routes to 0x525BF0.
	// TODO: 0x525BF0 is not expanded in the report.

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
			v += val.valueOfOther;
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

	// SS 4B.5 - bool flag = playerData::get_flag(pd, 0x81)
	// TODO: artifact 0x81 (129) is named only by number in the report; what it controls
	// (the planner's `mode`) is documented, but which artifact it is is not.
	const bool flag = false;

	const int64_t exchangeVal = planner.evaluateTroopExchange(hero, town, nullptr, flag);
	const int64_t buyVal = planner.evaluatePurchase(town, hero, flag);

	// SS 4B.5 - refuse an exchange that guts the hero.
	// TODO: `plan.armyValueAfter` is a planner field the report names here but never
	// lists in the SS 4B.4 layout table, so the guard cannot be evaluated exactly.  The
	// value of the army after the simulated exchange is used, which is what the name says.
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

	// SS 4B.2 - weeksAhead = (moveLimit - hero->mp) / hero->maxMp + gpGame->w[0x1F63E]
	// gpGame + 0x1F63E is the scenario "loss condition"/mode word (SS 2).
	// TODO: adding a scenario mode word to a week count is what the instruction stream
	// says, but the report offers no interpretation of it, so the term is dropped.
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
	// 0x640558..0x64056C.
	// TODO: the report gives the table's address but not its contents, so which five
	// artifacts are handed over cannot be reproduced.

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
