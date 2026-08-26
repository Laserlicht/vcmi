/*
 * H3ObjectValue.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3ObjectValue.h"

#include "H3ArmyPlanner.h"
#include "H3CombatEstimate.h"
#include "H3TownValue.h"
#include "H3Valuations.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGResource.h"
#include "../../lib/mapObjects/army/CArmedInstance.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/callback/Calendar.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/spells/CSpellHandler.h"

#include <algorithm>

namespace H3AI
{

namespace
{
int64_t experienceReward(const CGHeroInstance * hero, const HeroValuations & val, int multiplier = 1)
{
	// SS 4.8 - the shape shared by Arena, Marletto Tower, Mercenary Camp, Scholar,
	// Tree of Knowledge and University: expForNextLevel(level) * hero->xpValue.
	return static_cast<int64_t>(multiplier * static_cast<double>(experienceForLevel(hero->level)) * val.experienceValue);
}

bool visitedByHero(const CGHeroInstance * hero, const CGObjectInstance * object)
{
	// SS 4.8 - most one-shot objects are gated on a per-hero bitmask (hero + 0x57 ..
	// +0x6F, +0x121).  VCMI keeps the same information as a set of object ids.
	return hero->visitedObjects.count(object->id) > 0;
}

bool visitedByPlayer(H3Context & ctx, const CGObjectInstance * object)
{
	const PlayerState * state = ctx.cb->getPlayerState(ctx.player->getColor(), false);

	return state != nullptr && state->visitedObjects.count(object->id) > 0;
}

bool ownedByUs(H3Context & ctx, PlayerColor owner)
{
	if(!owner.isValidPlayer())
		return false;

	return ctx.cb->getPlayerRelations(owner, ctx.player->getColor()) != PlayerRelations::ENEMIES;
}

bool backpackFull(const CGHeroInstance * hero)
{
	// SS 4.8 - AI_artifact_count(hero, true) >= 64
	return hero->artifactsInBackpack.size() >= BACKPACK_FULL;
}

int currentMorale(const CGHeroInstance * hero)
{
	// TODO: SS 4.8 handlers call hero::get_morale (0x4E39B0), which the report never
	// expands.  VCMI's own morale total is used.
	return hero->valOfBonuses(BonusType::MORALE);
}

int currentLuck(const CGHeroInstance * hero)
{
	// TODO: as with get_morale, the report never expands the original's luck accessor.
	// CGHeroInstance::getCurrentLuck is declared in VCMI but has no definition, so the
	// bonus total is queried directly.
	return hero->valOfBonuses(BonusType::LUCK);
}

/// The guarding army of a map object, or nullptr when it has none.
const CArmedInstance * armyOf(const CGObjectInstance * object)
{
	return dynamic_cast<const CArmedInstance *>(object);
}

bool isGuarded(const CGObjectInstance * object)
{
	const CArmedInstance * army = armyOf(object);

	return army != nullptr && !army->Slots().empty();
}

int dayOfWeek(H3Context & ctx)
{
	return ctx.cb->getCalendar().getDayOfWeek();
}
}

int payForObject(H3Context & ctx, int value, int goldCost, int amount, GameResID resource)
{
	// SS 4.8 - AI_pay_for_object @ 0x529810
	//
	//   if (pd->gold  < goldCost) return 0;
	//   if (pd->res[t] < amount)  return 0;
	//   return (int)(value - goldCost*pd->goldValue - amount*pd->resource_value[t]);
	if(ctx.cb->getResourceAmount(GameResID::GOLD) < goldCost)
		return 0;

	if(amount > 0 && ctx.cb->getResourceAmount(resource) < amount)
		return 0;

	return static_cast<int>(value
		- goldCost * ctx.player->goldValue()
		- amount * ctx.player->resourceValue(resource));
}

int artifactValue(H3Context & ctx, const ArtifactID & artifact)
{
	// SS 4C.3, conditions 0 and 10 - inside AI_get_value_of_artifact (0x4336C0):
	//   if ((cond == 0 || cond == 10) && gpGame->d[0x1F8A0] == artifactId)
	//       value = 5'000'000;
	if((ctx.victory.condition == H3VictoryCondition::ACQUIRE_ARTIFACT
			|| ctx.victory.condition == H3VictoryCondition::TRANSPORT_ARTIFACT)
		&& ctx.victory.artifact == artifact)
	{
		return VICTORY_CONDITION_OVERRIDE;
	}

	// SS 4.9 - the ordinary value is produced by AI_get_value_of_artifact, "evaluated
	// through the type_artifact_effect class hierarchy whose vftables sit at
	// 0x63B6B0...0x63B778".
	// TODO: the report identifies the class hierarchy but gives none of its arithmetic,
	// so no per-artifact valuation can be reproduced.  Callers that need a floor apply
	// SS 4.8's max(value, 10) themselves.
	return 0;
}

int spellValue(H3Context & ctx, const CGHeroInstance * hero, const SpellID & spell)
{
	// SS 4.17 - hero::AI_get_spell_value @ 0x5298D0
	const CSpell * spellData = spell.toSpell();

	if(spellData == nullptr)
		return 0;

	// The Wisdom gate: level > Wisdom + 2 -> 0.  With no Wisdom this caps the hero at
	// level 2, with expert Wisdom (3) at level 5 - exactly H3's rule.
	if(spellData->getLevel() > hero->getSecSkillLevel(SecondarySkill::WISDOM) + 2)
		return 0;

	if(hero->spellbookContainsSpell(spell))
		return 0;

	if(!hero->hasSpellbook())
		return 0;

	// TODO: the real valuer is 0x527640, a counterfactual over the whole spellbook using
	// the shared trio 0x526D40 / 0x5273D0 / 0x5275B0.  SS 4.17 gives its skeleton but
	// none of the three helper bodies, so the number it produces cannot be reproduced.
	return 0;
}

int objectValue(H3Context & ctx, const CGHeroInstance * hero, const int3 & tile, int & moveLimit)
{
	// SS 4.8 - hero::AI_object_value @ 0x528040, dispatched on MapObjectType.
	// getTopObj logs verbosely for tiles under fog and returns nothing there; with the
	// map-open cheat the tile is read straight out of the map instead (see topObjectAt
	// in H3Movement.cpp, which this mirrors).
	const CGObjectInstance * object = nullptr;

	if(ctx.cb->isVisible(tile))
	{
		object = ctx.cb->getTopObj(tile);
	}
	else if(ctx.openMap)
	{
		const TerrainTile * terrain = ctx.cb->getTileUnchecked(tile);

		if(terrain != nullptr && !terrain->visitableObjects.empty())
			object = ctx.cb->getObjInstance(terrain->visitableObjects.back());
	}

	if(object == nullptr || hero == nullptr)
		return 0;

	auto stateIt = ctx.heroStates->find(hero->id);
	const HeroValuations val = stateIt != ctx.heroStates->end()
		? stateIt->second.valuations
		: computeHeroValuations(hero);

	const double goldValue = ctx.player->goldValue();
	const int64_t armyValue = armyAIValue(hero);

	switch(object->ID.toEnum())
	{
	// ---- Arena (4) ---------------------------------------------------------------
	case Obj::ARENA:
		// 2 x expForNextLevel(level) x hero->xpValue, 0 if already used
		if(visitedByHero(hero, object))
			return 0;

		return static_cast<int>(experienceReward(hero, val, 2));

	// ---- Artifact (5) ------------------------------------------------------------
	case Obj::ARTIFACT:
	{
		if(backpackFull(hero))
			return 0;

		const auto * artifactObject = dynamic_cast<const CGArtifact *>(object);
		const ArtifactID artifact = artifactObject != nullptr ? artifactObject->getArtifactType() : ArtifactID::NONE;

		int value = std::max(artifactValue(ctx, artifact), ARTIFACT_MIN_VALUE);

		// SS 4.8 - the pickup sub-switch (jumptable 0x529670).  objCell->d[0] low nibble
		// selects the guard condition:
		//   0 free pickup
		//   1 AI_pay_for_object(value, 2000 gold, 0, 0)
		//   2 requires Wisdom  > 0
		//   3 requires skill at hero + 0xCF (= 0xC9 + 6, Leadership) > 0
		//   4 AI_pay_for_object(value, 2500 gold, n, resource 3 = sulfur)
		//   5 AI_pay_for_object(value, 3000 gold, n, resource 5 = gems)
		//   6 guarded by creatures -> AI_value_of_combat(guards) + artifactValue
		if(isGuarded(object))
		{
			// nibble 6 - guarded
			return static_cast<int>(
				valueOfCombat(ctx.cb, *ctx.player, hero, nullptr, armyOf(object), nullptr)
				+ ctx.player->artifactValue());
		}

		// TODO: VCMI models the other five pickup conditions as configurable rewards on
		// CRewardableObject rather than as the H3 cell nibble, so which of the six arms
		// applies cannot be read back here.  The free-pickup arm is used.
		return value;
	}

	// ---- Pandora's Box (6) -------------------------------------------------------
	case Obj::PANDORAS_BOX:
	{
		// SS 4.8 - "the most complete evaluator":
		//   AI_value_of_combat(guards)                              if guarded
		// + rewardExp * hero->xpValue
		// + AI_resource_cost(playerData, rewardResources)
		// + sum_i rewardPrimarySkill[i] * expForNextLevel * xpValue
		// + sum over reward creature stacks:
		//       (count - alreadyOwnedOfThatType) * xpValueUnit
		//   (only if the hero has a free slot or already owns that creature)
		// + artifactCount * playerData.artifactValue
		// + sum over reward spells: AI_get_spell_value(hero, spell)
		// + morale/luck/mp deltas
		int64_t value = 0;

		if(isGuarded(object))
			value += valueOfCombat(ctx.cb, *ctx.player, hero, nullptr, armyOf(object), nullptr);

		// TODO: VCMI stores Pandora contents as a CRewardableObject reward list whose
		// visibility to an AI is deliberately limited (the player has not opened the box
		// yet).  The report assumes the AI can read the contents directly out of the map
		// object, which VCMI's callback layer does not expose, so only the guard term is
		// reproduced.
		return static_cast<int>(value);
	}

	// ---- Black Market (7) --------------------------------------------------------
	case Obj::BLACK_MARKET:
	{
		if(backpackFull(hero))
			return 0;

		// SS 4.8 - sum over the 7 offered artifacts of
		//   max(0, AI_get_value_of_artifact(art) - price * resource_value[priceResource])
		// and 0 for any artifact the player cannot afford.
		// TODO: needs AI_get_value_of_artifact, which the report does not specify.
		return 0;
	}

	// ---- Keymaster (10) ----------------------------------------------------------
	case Obj::KEYMASTER:
	{
		const PlayerState * state = ctx.cb->getPlayerState(ctx.player->getColor(), false);

		if(state != nullptr && state->wasKeymasterVisited(object->subID))
			return 0;

		return KEYMASTER_VALUE;
	}

	// ---- Buoy (11) ---------------------------------------------------------------
	case Obj::BUOY:
		if(visitedByHero(hero, object))
			return 0;

		return static_cast<int>(luckMoraleToAbsolute(hero, valueOfMorale(currentMorale(hero), 1)));

	// ---- Campfire (12) -----------------------------------------------------------
	case Obj::CAMPFIRE:
		// 100 * amount * goldValue + amount * resourceValue[type]
		// TODO: the campfire's rolled resource type and amount are generated by the
		// server in VCMI and are not readable from the map object.
		return 0;

	// ---- Swan Pond (14) ----------------------------------------------------------
	case Obj::SWAN_POND:
	{
		// SS 4.8 - luck -2 ... +2 clamp; luck_value * armyValue * (priority+40)/40,
		// and it rewrites *limit so the AI knows it costs the rest of the turn.
		const int luck = std::clamp(currentLuck(hero), -2, 2);
		moveLimit = 0;

		return static_cast<int>(luckMoraleToAbsolute(hero, valueOfLuck(luck, 2)));
	}

	// ---- Creature Bank (16), Derelict Ship (24), Dragon Utopia (25) ---------------
	case Obj::CREATURE_BANK:
	case Obj::DERELICT_SHIP:
	case Obj::DRAGON_UTOPIA:
	case Obj::CRYPT:
	case Obj::SHIPWRECK:
	case Obj::PYRAMID:
	{
		// SS 4.8 - AI_value_of_combat(guards); then + resource_value(loot)
		// + traits[reward].AI_value * count + artifactCount * playerData.artifactValue
		int64_t value = valueOfCombat(ctx.cb, *ctx.player, hero, nullptr, armyOf(object), nullptr);

		// TODO: the bank's loot table is a CRewardableObject configuration in VCMI and is
		// not exposed to the AI callback, so only the combat term is reproduced.
		return static_cast<int>(value);
	}

	// ---- Creature dwellings (17, 20) ---------------------------------------------
	case Obj::CREATURE_GENERATOR1:
	case Obj::CREATURE_GENERATOR2:
	case Obj::CREATURE_GENERATOR3:
	case Obj::CREATURE_GENERATOR4:
	{
		// SS 4.8 -> 0x529A30
		if(ownedByUs(ctx, object->getOwner()))
			return 0;

		int64_t value = 0;

		if(isGuarded(object))
			value += valueOfCombat(ctx.cb, *ctx.player, hero, nullptr, armyOf(object), nullptr);

		// "then the value of the creatures buyable now (0x42D780 against the player's purse)"
		const auto * dwelling = dynamic_cast<const CGDwelling *>(object);

		if(dwelling != nullptr)
		{
			ResourceSet purse = ctx.cb->getResourceAmount();

			for(const auto & level : dwelling->creatures)
			{
				if(level.second.empty() || level.first == 0)
					continue;

				const CCreature * creature = level.second.back().toCreature();

				if(creature == nullptr)
					continue;

				int affordable = level.first;

				for(int r = 0; r < GameConstants::RESOURCE_QUANTITY; ++r)
					if(creature->getRecruitCost(GameResID(r)) > 0)
						affordable = std::min<int>(affordable, purse[GameResID(r)] / creature->getRecruitCost(GameResID(r)));

				if(affordable > 0)
					value += static_cast<int64_t>(creature->getAIValue()) * affordable;
			}
		}

		// SS 4C.3, condition 8 - "flag all creature dwellings".
		if(ctx.victory.condition == H3VictoryCondition::FLAG_DWELLINGS && !ownedByUs(ctx, object->getOwner()))
		{
			// The original divides by the obelisk count here.
			// TODO: the report writes "+5 000 000 / obeliskCount" for this arm without
			// saying which count that is; the mine arm (condition 9) divides by the
			// number of mines on the map.  The undivided override is used.
			value += VICTORY_CONDITION_OVERRIDE;
		}

		return static_cast<int>(value);
	}

	// ---- Corpse (22) -------------------------------------------------------------
	case Obj::CORPSE:
		if(visitedByHero(hero, object))
			return 0;

		if(!backpackFull(hero))
			return ctx.player->artifactValue() / CORPSE_ARTIFACT_DIVISOR;

		return static_cast<int>(CORPSE_GOLD * goldValue);

	// ---- Marletto Tower (23) -----------------------------------------------------
	case Obj::MARLETTO_TOWER:
		if(visitedByHero(hero, object))
			return 0;

		return static_cast<int>(experienceReward(hero, val));

	// ---- Faerie Ring (28) --------------------------------------------------------
	case Obj::FAERIE_RING:
		return static_cast<int>(luckMoraleToAbsolute(hero, valueOfLuck(currentLuck(hero), 1)));

	// ---- Flotsam (29) ------------------------------------------------------------
	case Obj::FLOTSAM:
		return static_cast<int>(FLOTSAM_GOLD * goldValue + FLOTSAM_WOOD * ctx.player->resourceValue(GameResID::WOOD));

	// ---- Fountain of Fortune (30) ------------------------------------------------
	case Obj::FOUNTAIN_OF_FORTUNE:
		// SS 4.8 - "luck bonus encoded in the object; luck_value(current, bonus)".
		// TODO: the bonus is rolled by the server in VCMI and is not readable here.
		return 0;

	// ---- Fountain of Youth (31) --------------------------------------------------
	case Obj::FOUNTAIN_OF_YOUTH:
		if(hero->movementPointsRemaining() >= FOUNTAIN_OF_YOUTH_MP)
		{
			moveLimit -= FOUNTAIN_OF_YOUTH_MP;

			return static_cast<int>(luckMoraleToAbsolute(hero, valueOfMorale(currentMorale(hero), 1)));
		}

		return FOUNTAIN_OF_YOUTH_FALLBACK;

	// ---- Garden of Revelation (32) -----------------------------------------------
	case Obj::GARDEN_OF_REVELATION:
		return val.valueOfOther;

	// ---- Garrison (33) -----------------------------------------------------------
	case Obj::GARRISON:
	case Obj::GARRISON2:
	{
		if(ownedByUs(ctx, object->getOwner()))
		{
			ArmyPlanner planner(ctx.cb, ctx.player);

			return static_cast<int>(planner.evaluateTroopExchange(hero, armyOf(object), nullptr, false));
		}

		return valueOfCombat(ctx.cb, *ctx.player, hero, nullptr, armyOf(object), nullptr);
	}

	// ---- Hero (34) ---------------------------------------------------------------
	case Obj::HERO:
	{
		const auto * other = dynamic_cast<const CGHeroInstance *>(object);

		if(other == nullptr)
			return 0;

		if(ownedByUs(ctx, other->getOwner()))
		{
			// SS 4.8 - "own -> merge/refit"
			ArmyPlanner planner(ctx.cb, ctx.player);

			return static_cast<int>(planner.evaluateTroopExchange(hero, other, other, false));
		}

		return valueOfCombat(ctx.cb, *ctx.player, hero, other, other, nullptr);
	}

	// ---- Hill Fort (35) ----------------------------------------------------------
	case Obj::HILL_FORT:
		// SS 4.8 - "value of upgrading the army minus the cost".
		// TODO: the report gives no formula beyond that sentence.
		return 0;

	// ---- Hut of Magi (37) --------------------------------------------------------
	case Obj::HUT_OF_MAGI:
		// SS 4.8 - AI_player[owner].magus_hut_value (type_AI_player + 0x04)
		return ctx.player->magusHutValue();

	// ---- Idol of Fortune (38) ----------------------------------------------------
	case Obj::IDOL_OF_FORTUNE:
	{
		// SS 4.8 - "luck or morale depending on the week".
		// In H3 the Idol grants luck on days 1-6 and both on day 7.
		// TODO: the report does not state which way round the week test goes, only that
		// there is one.  The luck arm is used for days 1-6 and morale added on day 7.
		const int day = dayOfWeek(ctx);
		int64_t value = luckMoraleToAbsolute(hero, valueOfLuck(currentLuck(hero), 1));

		if(day == 7)
			value += luckMoraleToAbsolute(hero, valueOfMorale(currentMorale(hero), 1));

		return static_cast<int>(value);
	}

	// ---- Lean To (39) ------------------------------------------------------------
	case Obj::LEAN_TO:
		if(visitedByHero(hero, object))
			return 0;

		return LEAN_TO_MULTIPLIER * ctx.player->averageResourceValue();

	// ---- Library of Enlightenment (41) -------------------------------------------
	case Obj::LIBRARY_OF_ENLIGHTENMENT:
	{
		// SS 4.8 - requires hero->level + 2 * hero->b[0xCD] >= 10, where 0xCD = 0xC9 + 4,
		// i.e. the Diplomacy skill level.
		const int diplomacy = hero->getSecSkillLevel(SecondarySkill::DIPLOMACY);

		if(static_cast<int>(hero->level) + 2 * diplomacy < LIBRARY_LEVEL_REQUIREMENT)
			return 0;

		return static_cast<int>(armyValue / LIBRARY_ARMY_DIVISOR)
			+ 2 * (val.valueOfSpellPower + val.valueOfOther);
	}

	// ---- Lighthouse (42) ---------------------------------------------------------
	case Obj::LIGHTHOUSE:
		return ownedByUs(ctx, object->getOwner()) ? 0 : LIGHTHOUSE_VALUE;

	// ---- School of Magic (47) ----------------------------------------------------
	case Obj::SCHOOL_OF_MAGIC:
		if(visitedByHero(hero, object))
			return 0;

		return static_cast<int>(std::max(val.valueOfSpellPower, val.valueOfOther)
			- SCHOOL_OF_MAGIC_COST * goldValue);

	// ---- Magic Spring (48) / Magic Well (49) -------------------------------------
	case Obj::MAGIC_SPRING:
	case Obj::MAGIC_WELL:
		// SS 4.8 - 0x52B810 / 0x52A510, "mana refill value".
		// TODO: neither routine is expanded in the report.
		return 0;

	// ---- Mercenary Camp (51) -----------------------------------------------------
	case Obj::MERCENARY_CAMP:
		if(visitedByHero(hero, object))
			return 0;

		return static_cast<int>(experienceReward(hero, val));

	// ---- Mermaid (52) ------------------------------------------------------------
	case Obj::MERMAID:
		if(visitedByHero(hero, object))
			return 0;

		return static_cast<int>(luckMoraleToAbsolute(hero, valueOfLuck(currentLuck(hero), 1)));

	// ---- Mine (53) ---------------------------------------------------------------
	case Obj::MINE:
	{
		// SS 4.8 -> 0x52A010:
		//   AI_value_of_combat(guards)
		// + 2 * dailyYield * resource_value[res] * (get_attack_bonus + 1.0)
		const auto * mine = dynamic_cast<const CGMine *>(object);

		if(mine == nullptr)
			return 0;

		if(ownedByUs(ctx, mine->getOwner()))
			return 0;

		int64_t value = valueOfCombat(ctx.cb, *ctx.player, hero, nullptr, armyOf(mine), nullptr);

		const int resourceIndex = mine->producedResource.getNum();
		const int yield = (resourceIndex >= 0 && resourceIndex < 7) ? MINE_DAILY_YIELD[resourceIndex] : 0;

		value += static_cast<int64_t>(2.0 * yield
			* ctx.player->resourceValue(mine->producedResource)
			* (ctx.player->getAttackBonus(mine->getOwner()) + 1.0));

		// SS 4C.3, condition 9 - "flag all mines".  The original divides the override by
		// the number of mine records on the map, and traps when that list is empty.
		if(ctx.victory.condition == H3VictoryCondition::FLAG_MINES)
		{
			int mineCount = 0;

			for(const CGObjectInstance * candidate : ctx.cb->getAllVisitableObjs())
				if(candidate->ID == Obj::MINE)
					++mineCount;

			// SS 4C.3 flags the divide-by-zero explicitly and asks a reimplementation to
			// guard the division rather than reproduce it.
			if(mineCount > 0)
				value += VICTORY_CONDITION_OVERRIDE / mineCount;
		}

		return static_cast<int>(value);
	}

	// ---- Monster (54) ------------------------------------------------------------
	case Obj::MONSTER:
	{
		// SS 4.8 -> 0x52A140, "AI_value_of_combat + treasure"
		int64_t value = valueOfCombat(ctx.cb, *ctx.player, hero, nullptr, armyOf(object), nullptr);

		// SS 4C.3, condition 7 - defeat a specific monster: the handler matches the
		// object's packed coordinate against the record.
		if(ctx.victory.condition == H3VictoryCondition::DEFEAT_MONSTER
			&& (ctx.victory.targetObject == object->id || ctx.victory.position == object->visitablePos()))
		{
			value += VICTORY_CONDITION_OVERRIDE;
		}

		// TODO: the "treasure" term is the monster's carried resources / artifact, which
		// VCMI generates server-side and does not expose here.
		return static_cast<int>(value);
	}

	// ---- Mystical Garden (55) ----------------------------------------------------
	case Obj::MYSTICAL_GARDEN:
		// SS 4.8 - "(500 * goldValue + 5 * gemsValue) / 2 - the average of its two
		// possible rewards; 0 unless bit 10 of the cell data ('regrown') is set".
		if(visitedByPlayer(ctx, object))
			return 0;

		return static_cast<int>((MYSTICAL_GARDEN_GOLD * goldValue
			+ MYSTICAL_GARDEN_GEMS * ctx.player->resourceValue(GameResID::GEMS)) / 2);

	// ---- Oasis (56) --------------------------------------------------------------
	case Obj::OASIS:
	{
		// SS 4.8 -> 0x52A1E0(hero, 0x80, 400): "morale + mp"
		if(visitedByHero(hero, object))
			return 0;

		int64_t value = luckMoraleToAbsolute(hero, valueOfMorale(currentMorale(hero), 1));

		// TODO: the report does not say how the +400 movement points are priced.
		(void)OASIS_MOVEMENT;

		return static_cast<int>(value);
	}

	// ---- Obelisk (57) ------------------------------------------------------------
	case Obj::OBELISK:
		// SS 4.8 -> 0x52A2B0, "puzzle-map progress".
		// TODO: SS 4.14 explicitly leaves the Grail-area reduction unexpanded, so the
		// reward for narrowing the dig target cannot be reproduced.
		return 0;

	// ---- Redwood Observatory (58) / Pillar of Fire (60) --------------------------
	case Obj::REDWOOD_OBSERVATORY:
	case Obj::PILLAR_OF_FIRE:
		// SS 4.8 -> 0x432220(colour, 20, coord), "scouting-radius value".
		// TODO: not expanded in the report.
		return 0;

	// ---- Star Axis (61) ----------------------------------------------------------
	case Obj::STAR_AXIS:
		if(visitedByHero(hero, object))
			return 0;

		return val.valueOfSpellPower;

	// ---- Prison (62) -------------------------------------------------------------
	case Obj::PRISON:
	{
		// SS 4.8 -> 0x52A3A0: 2500 * goldValue + armyGroup::get_AI_value(freed hero's army)
		const auto * prisoner = dynamic_cast<const CGHeroInstance *>(object);

		return static_cast<int>(PRISON_XP_GOLD * goldValue + armyAIValue(prisoner));
	}

	// ---- Rally Flag (64) ---------------------------------------------------------
	case Obj::RALLY_FLAG:
	{
		// SS 4.8 -> 0x52A5F0, "luck + morale + mp"
		if(visitedByHero(hero, object))
			return 0;

		int64_t value = luckMoraleToAbsolute(hero, valueOfLuck(currentLuck(hero), 1))
			+ luckMoraleToAbsolute(hero, valueOfMorale(currentMorale(hero), 1));

		// TODO: the movement-point term is not specified in the report.
		return static_cast<int>(value);
	}

	// ---- Refugee Camp (78) -------------------------------------------------------
	case Obj::REFUGEE_CAMP:
		// SS 4.8 -> 0x52A700.  TODO: not expanded in the report.
		return 0;

	// ---- Resource (79) -----------------------------------------------------------
	case Obj::RESOURCE:
	{
		const auto * resource = dynamic_cast<const CGResource *>(object);

		if(resource == nullptr)
			return 0;

		return static_cast<int>(resource->getAmount()
			* ctx.player->resourceValue(GameResID(resource->subID.getNum())));
	}

	// ---- Scholar (81) ------------------------------------------------------------
	case Obj::SCHOLAR:
		return static_cast<int>(experienceReward(hero, val));

	// ---- Sea Chest (82) ----------------------------------------------------------
	case Obj::SEA_CHEST:
		if(!backpackFull(hero))
		{
			return ctx.player->artifactValue() / SEA_CHEST_ARTIFACT_DIVISOR
				+ static_cast<int>(SEA_CHEST_GOLD * goldValue);
		}

		return static_cast<int>(SEA_CHEST_GOLD * goldValue);

	// ---- Seer Hut (83) -----------------------------------------------------------
	case Obj::SEER_HUT:
		// SS 4.8 -> 0x5735A0, "quest reward minus quest cost".
		// TODO: not expanded in the report.
		return 0;

	// ---- Shipwreck Survivor (86) -------------------------------------------------
	case Obj::SHIPWRECK_SURVIVOR:
		if(backpackFull(hero))
			return 0;

		return ctx.player->artifactValue();

	// ---- Shipyard (87) -----------------------------------------------------------
	case Obj::SHIPYARD:
		return ownedByUs(ctx, object->getOwner()) ? 0 : SHIPYARD_VALUE;

	// ---- Shrines of Magic I / II / III (88-90) -----------------------------------
	case Obj::SHRINE_OF_MAGIC_INCANTATION:
	case Obj::SHRINE_OF_MAGIC_GESTURE:
	case Obj::SHRINE_OF_MAGIC_THOUGHT:
		// SS 4.8 -> 0x5298D0, i.e. exactly AI_get_spell_value with its three gates.
		// TODO: which spell the shrine holds is a server-side roll in VCMI.
		return 0;

	// ---- Sirens (92) -------------------------------------------------------------
	case Obj::SIRENS:
		// SS 4.8 -> 0x52A960, "XP for sacrificed troops".
		// TODO: not expanded in the report.
		return 0;

	// ---- Spell Scroll (93) -------------------------------------------------------
	case Obj::SPELL_SCROLL:
		// SS 4.8 -> 0x52A8C0.  TODO: not expanded in the report.
		return 0;

	// ---- Stables (94) ------------------------------------------------------------
	case Obj::STABLES:
		// SS 4.8 -> 0x52AAC0, "+400 mp".
		// TODO: the report does not say how movement points are converted to value.
		return 0;

	// ---- Temple (96) -------------------------------------------------------------
	case Obj::TEMPLE:
	{
		// SS 4.8 - morale +1, +2 on day 7
		if(visitedByHero(hero, object))
			return 0;

		const int bonus = dayOfWeek(ctx) == 7 ? 2 : 1;

		return static_cast<int>(luckMoraleToAbsolute(hero, valueOfMorale(currentMorale(hero), bonus)));
	}

	// ---- Town (98) ---------------------------------------------------------------
	case Obj::TOWN:
		return townValue(ctx, hero, dynamic_cast<const CGTownInstance *>(object), moveLimit);

	// ---- Learning Stone (100) ----------------------------------------------------
	case Obj::LEARNING_STONE:
		if(visitedByHero(hero, object))
			return 0;

		return static_cast<int>(val.experienceValue * LEARNING_STONE_XP);

	// ---- Treasure Chest (101) ----------------------------------------------------
	case Obj::TREASURE_CHEST:
	{
		// SS 4.8 -> 0x52B4E0: three tiers, each max(xpValue * k, goldValue * g), plus
		// artifactValue / 20.
		// TODO: which of the three tiers a given chest rolls is decided server-side in
		// VCMI, so the mean of the three is not available either; the first tier is used
		// and the artifact term added, exactly as the handler shapes it.
		const int64_t xpTerm = static_cast<int64_t>(val.experienceValue * TREASURE_CHEST_XP[0]);
		const int64_t goldTerm = static_cast<int64_t>(goldValue * TREASURE_CHEST_GOLD[0]);

		return static_cast<int>(std::max(xpTerm, goldTerm)
			+ ctx.player->artifactValue() / TREASURE_CHEST_ARTIFACT_DIVISOR);
	}

	// ---- Tree of Knowledge (102) -------------------------------------------------
	case Obj::TREE_OF_KNOWLEDGE:
		if(visitedByHero(hero, object))
			return 0;

		return static_cast<int>(experienceReward(hero, val) - TREE_OF_KNOWLEDGE_GOLD_COST * goldValue);

	// ---- University (104) --------------------------------------------------------
	case Obj::UNIVERSITY:
		return static_cast<int>((experienceReward(hero, val)
			- UNIVERSITY_GEMS_COST * ctx.player->resourceValue(GameResID::GEMS)
			- UNIVERSITY_GOLD_COST * goldValue) / UNIVERSITY_DIVISOR);

	// ---- Wagon (105) -------------------------------------------------------------
	case Obj::WAGON:
		// SS 4.8 - "artifact or resources".  TODO: the roll is server-side in VCMI.
		return 0;

	// ---- War Machine Factory (106) -----------------------------------------------
	case Obj::WAR_MACHINE_FACTORY:
		// SS 4.8 - "value of ballista/tent/cart minus cost".
		// TODO: needs AI_get_value_of_artifact, which the report does not specify.
		return 0;

	// ---- School of War (107) -----------------------------------------------------
	case Obj::SCHOOL_OF_WAR:
		// SS 4.8 - "+1 attack/defence for 1000 gold".
		// TODO: the report gives no valuation for a primary-skill point here; the AI's
		// only documented primary-skill pricing is the XP route used by Arena.
		return 0;

	// ---- Warrior's Tomb (108) ----------------------------------------------------
	case Obj::WARRIORS_TOMB:
		// SS 4.8 - "artifact minus morale penalty".
		// TODO: needs AI_get_value_of_artifact.
		return 0;

	// ---- Water Wheel (109) -------------------------------------------------------
	case Obj::WATER_WHEEL:
		// SS 4.8 - "gold, halved after the first week".
		// TODO: the report does not give the base amount.
		return 0;

	// ---- Watering Hole (110) -----------------------------------------------------
	case Obj::WATERING_HOLE:
	{
		if(visitedByHero(hero, object))
			return 0;

		// TODO: the movement-point half of "morale + mp" is not specified.
		return static_cast<int>(luckMoraleToAbsolute(hero, valueOfMorale(currentMorale(hero), 1)));
	}

	// ---- Windmill (112) ----------------------------------------------------------
	case Obj::WINDMILL:
		// SS 4.8 - "resource value".  TODO: the roll is server-side in VCMI.
		return 0;

	// ---- Witch Hut (113) ---------------------------------------------------------
	case Obj::WITCH_HUT:
		// SS 4.8 - "secondary-skill value", i.e. hero::AI_secondary_skill_value.
		// TODO: which skill the hut teaches is a server-side roll in VCMI.
		return 0;

	default:
		// SS 4.8 - the 0x5294E5 default arm.  Boat, Border Guard, Cartographer, Cover of
		// Darkness, Cursed Ground, Event, Eye of Magi, Grail, all Monoliths, Magic
		// Plains, Market of Time, Ocean Bottle, all Random*, Sanctuary, Sign, Tavern,
		// Den of Thieves, Trading Post, Subterranean Gate and Whirlpool are worth 0:
		// "the AI never targets these on their own".
		break;
	}

	// SS 4C.3, condition 4 - the Grail digging spot outranks every other destination on
	// a Grail map.  In the original this override lives inside AI_scan_objects rather
	// than in the object dispatch, which is why it is applied after the switch.
	if(object->ID == Obj::GRAIL)
	{
		if(ctx.victory.condition == H3VictoryCondition::BUILD_GRAIL)
			return VICTORY_CONDITION_OVERRIDE;

		return artifactValue(ctx, ArtifactID::GRAIL);
	}

	return 0;
}

}
