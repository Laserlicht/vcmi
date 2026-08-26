/*
 * H3SecondarySkills.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3SecondarySkills.h"

#include "H3Valuations.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CCreatureSet.h"

#include <algorithm>

namespace H3AI
{

namespace
{
struct SkillContext
{
	/// SS 4.12 - "int army = 1000, shooters = 500;" then, when useArmy is set, the
	/// hero's stacks are folded in at traits[t].AI_value * count.
	int64_t army = SKILL_ARMY_FLOOR;
	int64_t shooters = SKILL_SHOOTER_FLOOR;
};

SkillContext buildSkillContext(const CGHeroInstance * hero, bool useArmy)
{
	SkillContext out;

	if(!useArmy)
		return out;

	for(const auto & slot : hero->Slots())
	{
		const CCreature * creature = hero->getCreature(slot.first);

		if(creature == nullptr)
			continue;

		const int64_t v = static_cast<int64_t>(creature->getAIValue()) * hero->getStackCount(slot.first);

		out.army += v;

		if(isShooter(creature))
			out.shooters += v;
	}

	return out;
}
}

int secondarySkillValue(H3Context & ctx, const CGHeroInstance * hero, const SecondarySkill & skill, bool useArmy)
{
	// SS 4.12 - hero::AI_secondary_skill_value @ 0x524690
	if(hero == nullptr)
		return 0;

	const int cur = hero->getSecSkillLevel(skill);

	if(cur == 3)
		return 0; // already expert

	if(cur == 0 && hero->secSkills.size() >= 8)
		return 0; // all eight skill slots full

	const SkillContext sc = buildSkillContext(hero, useArmy);
	const int64_t army = sc.army;
	const int64_t shooters = sc.shooters;

	auto stateIt = ctx.heroStates->find(hero->id);
	const HeroValuations val = stateIt != ctx.heroStates->end()
		? stateIt->second.valuations
		: computeHeroValuations(hero);

	const int spellPower = std::min(hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 99);
	const int knowledge = std::min(hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 99);
	const bool hasWisdom = hero->getSecSkillLevel(SecondarySkill::WISDOM) > 0;

	switch(skill.toEnum())
	{
	case SecondarySkill::PATHFINDING:
		// army * T[cur] / 4.0, T = 0x681860 (doubles)
		// TODO: SS 4.12's depth marker states the table "reads as uniform 1.0 in the
		// image and is probably runtime-filled".  1.0 is used, which is what it holds.
		return static_cast<int>(army * 1.0 / PATHFINDING_DIVISOR);

	case SecondarySkill::ARCHERY:
		// archeryValue = archeryTable[cur] * shooters / 100
		return static_cast<int>(ARCHERY_TABLE[cur] * shooters / 100);

	case SecondarySkill::LOGISTICS:
		return static_cast<int>(army / 10);

	case SecondarySkill::SCOUTING:
	case SecondarySkill::NECROMANCY:
	case SecondarySkill::LEARNING:
		// SS 4.12 - all three share the same arm at 0x524B05.  "Necromancy is not
		// special-cased.  The AI has no notion that Necromancy compounds."
		return static_cast<int>(army / 20);

	case SecondarySkill::DIPLOMACY:
		return static_cast<int>(army / 100);

	case SecondarySkill::NAVIGATION:
		// army * 1.0 / 2.0; the 1.0 comes from 0x681878, which reads as uniform 1.0.
		return static_cast<int>(army * 1.0 / NAVIGATION_DIVISOR);

	case SecondarySkill::LEADERSHIP:
	case SecondarySkill::LUCK:
	case SecondarySkill::TACTICS:
		// SS 4.12 - Leadership and Luck are literally the same code (0x5247CA); Tactics
		// has its own arm with the same divisor.
		return static_cast<int>(army / 50);

	case SecondarySkill::WISDOM:
		// SS 4.12 - "from spell power +0x478 x value-of-+1-power +0x47E, power clamped to 99"
		// TODO: the report gives the two operands but not the exact expression that
		// combines them; the product is the only reading its wording supports.
		return static_cast<int>(static_cast<int64_t>(spellPower) * val.valueOfSpellPower);

	case SecondarySkill::MYSTICISM:
		// "from value-of-+1-knowledge +0x486, /10"
		return val.valueOfOther / 10;

	case SecondarySkill::BALLISTICS:
		// SS 4.12 - "Ballistics outranks almost everything (army / 8), which is why AI
		// heroes so often carry it."
		return static_cast<int>(army / 8);

	case SecondarySkill::EAGLE_EYE:
		// 0 unless the hero has Wisdom, then from +0x47E
		if(!hasWisdom)
			return 0;

		return val.valueOfSpellPower;

	case SecondarySkill::ESTATES:
		// from owner +0x22 and table 0x64038C[cur]
		if(!hero->getOwner().isValidPlayer())
			return 0;

		return ESTATES_TABLE[cur];

	case SecondarySkill::FIRE_MAGIC:
	case SecondarySkill::AIR_MAGIC:
	case SecondarySkill::WATER_MAGIC:
	case SecondarySkill::EARTH_MAGIC:
		// SS 4.12 - all four route through 0x524D20, a counterfactual that re-prices the
		// hero's whole spellbook one level higher (or straight at expert when the school
		// is absent) and takes the difference.
		// TODO: the three spellbook-valuation helpers 0x526D40 / 0x5273D0 / 0x5275B0 are
		// never expanded in the report, so the difference cannot be computed.
		return 0;

	case SecondarySkill::SCHOLAR:
		// 0 unless Wisdom, then from +0x478 x +0x47E
		if(!hasWisdom)
			return 0;

		return static_cast<int>(static_cast<int64_t>(spellPower) * val.valueOfSpellPower);

	case SecondarySkill::ARTILLERY:
		// 0 unless HasArtifact(this, 4) - a Ballista
		if(!hero->hasArt(ArtifactID::BALLISTA))
			return 0;

		// TODO: the report records only the gate for this arm, not what it returns when
		// the gate passes.
		return 0;

	case SecondarySkill::OFFENCE:
	case SecondarySkill::ARMORER:
		// SS 4.12 - "the joint most valuable at 7 % of army value"
		return static_cast<int>(army * 7 / 100);

	case SecondarySkill::INTELLIGENCE:
		return static_cast<int>(static_cast<int64_t>(knowledge) * val.valueOfOther);

	case SecondarySkill::SORCERY:
		return static_cast<int>(static_cast<int64_t>(spellPower) * val.valueOfSpellPower);

	case SecondarySkill::RESISTANCE:
		return static_cast<int>(army / 40);

	case SecondarySkill::FIRST_AID:
		// 0 unless HasArtifact(this, 6) - a First Aid Tent
		if(!hero->hasArt(ArtifactID::FIRST_AID_TENT))
			return 0;

		// TODO: as with Artillery, only the gate is documented.
		return 0;

	default:
		return 0;
	}
}

namespace
{
/// SS 4.12 - 0x524DD0(hero, skill, useArmy), the tie-break predicate used when one
/// offered skill is already known and the other is not.  It ranks the skill against
/// everything the hero's class could ever learn.
bool rankAmongLearnableSkills(H3Context & ctx, const CGHeroInstance * hero, const SecondarySkill & skill, bool useArmy)
{
	std::array<int, GameConstants::SKILL_QUANTITY> value = {};

	const CHeroClass * heroClass = hero->getHeroClass();

	for(int s = 0; s < GameConstants::SKILL_QUANTITY; ++s)
	{
		const SecondarySkill candidate(s);

		if(hero->getSecSkillLevel(candidate) > 0)
		{
			value[s] = 0; // already known => 0
			continue;
		}

		const bool classCanLearn = heroClass != nullptr && heroClass->secSkillProbability.count(candidate) > 0
			&& heroClass->secSkillProbability.at(candidate) != 0;

		if(classCanLearn || candidate == skill)
			value[s] = secondarySkillValue(ctx, hero, candidate, useArmy);
		else
			value[s] = 0;
	}

	// SS 4.12 - "... rank `skill` among val[] ..."
	// TODO: the report shows the array being filled but elides the ranking rule itself.
	// "the offered known skill is at least as good as anything else the class could
	// learn" is the only reading that makes the caller's branch meaningful.
	const int own = secondarySkillValue(ctx, hero, skill, useArmy);
	const int best = *std::max_element(value.begin(), value.end());

	return own >= best;
}
}

SecondarySkill chooseSecondarySkill(
	H3Context & ctx,
	const CGHeroInstance * hero,
	const SecondarySkill & skillA,
	const SecondarySkill & skillB,
	bool useArmy)
{
	// SS 4.12 - hero::AI_choose_secondary_skill @ 0x52BBD0
	const bool haveA = hero->getSecSkillLevel(skillA) > 0;
	const bool haveB = hero->getSecSkillLevel(skillB) > 0;

	if(haveA == haveB)
	{
		const int vA = secondarySkillValue(ctx, hero, skillA, useArmy);
		const int vB = secondarySkillValue(ctx, hero, skillB, useArmy);

		return (vA >= vB) ? skillA : skillB; // ties go to skillA
	}

	// exactly one is already known - normalise so `known` is the known one
	const SecondarySkill known = haveA ? skillA : skillB;
	const SecondarySkill fresh = haveA ? skillB : skillA;

	// SS 4.12 - "the values are never compared at all.  The AI runs a separate predicate
	// on the already-known skill and takes it if that passes, otherwise takes the new one."
	return rankAmongLearnableSkills(ctx, hero, known, useArmy) ? known : fresh;
}

}
