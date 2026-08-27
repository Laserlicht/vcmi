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

#include "H3SpellValue.h"

#include "H3Valuations.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/mapObjects/army/CCreatureSet.h"

#include <algorithm>
#include <numeric>

namespace H3AI
{

namespace
{
/// SS 4.9a - the school mask the magic-school arms and the Tomes/Orbs share:
/// 1 Air, 2 Fire, 4 Water, 8 Earth.
int magicSchoolMask(const SecondarySkill & skill)
{
	switch(skill.getNum())
	{
	case SecondarySkill::AIR_MAGIC:   return 1;
	case SecondarySkill::FIRE_MAGIC:  return 2;
	case SecondarySkill::WATER_MAGIC: return 4;
	case SecondarySkill::EARTH_MAGIC: return 8;
	default:                          return 0;
	}
}
}


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
		// SS 4.12 - 0x5247E5.  The two operands are combined as a product HALVED:
		//   useArmy  -> clamp(spellPower, 1, 99) * valueOfSpellPower / 2
		//   otherwise-> clamp(spellPower, 1, 99) * 25
		return useArmy
			? static_cast<int>(static_cast<int64_t>(spellPower) * val.valueOfSpellPower / 2)
			: spellPower * 25;

	case SecondarySkill::MYSTICISM:
		// "from value-of-+1-knowledge +0x486, /10"
		return val.valueOfKnowledge / 10;

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
		// SS 4.12 - 0x524D20, now implementable: build a type_spellvalue, take the
		// baseline, raise the school, take it again, and return the difference.
		//
		// Note the asymmetry the original builds in: acquiring a school from SCRATCH is
		// priced as if it jumped straight to EXPERT, while an upgrade is priced one
		// level at a time.  The AI's appetite for a school is deliberately front-loaded.
		{
			H3SpellValue sv(hero);

			if(!sv.valid())
				return 0;   // no spell book: might heroes never take a magic school

			// The probe mutates the hero's school level, which we cannot do here, so the
			// difference is taken over the spells the school actually contains.
			const int schoolMask = magicSchoolMask(skill);
			const int before = sv.bestSpellValue(H3_SPELL_ALL_CATEGORIES);
			int best = 0;

			for(int id = 0; id < H3_SPELL_COUNT; ++id)
			{
				const SpellID spellId(id);

				if(!hero->canCastThisSpell(spellId.toSpell()))
					continue;

				if(!spellInSchoolMask(spellId, schoolMask))
					continue;

				best = std::max(best, sv.valueOfSpell(spellId));
			}

			return std::max(0, best - before / 2);
		}

	case SecondarySkill::SCHOLAR:
		// 0 unless Wisdom, then from +0x478 x +0x47E
		if(!hasWisdom)
			return 0;

		return static_cast<int>(static_cast<int64_t>(spellPower) * val.valueOfSpellPower);

	case SecondarySkill::ARTILLERY:
		// 0 unless HasArtifact(this, 4) - a Ballista
		if(!hero->hasArt(ArtifactID::BALLISTA))
			return 0;

		// SS 4.12 - once the Ballista gate passes the arm returns 10 x the hero's attack
		// skill, clamped to 99.  The gate itself only applies when useArmy is set.
		return std::clamp(hero->getPrimSkillLevel(PrimarySkill::ATTACK), 0, 99) * 10;

	case SecondarySkill::OFFENCE:
	case SecondarySkill::ARMORER:
		// SS 4.12 - "the joint most valuable at 7 % of army value"
		return static_cast<int>(army * 7 / 100);

	case SecondarySkill::INTELLIGENCE:
		return static_cast<int>(static_cast<int64_t>(knowledge) * val.valueOfKnowledge);

	case SecondarySkill::SORCERY:
		return static_cast<int>(static_cast<int64_t>(spellPower) * val.valueOfSpellPower);

	case SecondarySkill::RESISTANCE:
		return static_cast<int>(army / 40);

	case SecondarySkill::FIRST_AID:
		// 0 unless HasArtifact(this, 6) - a First Aid Tent
		if(!hero->hasArt(ArtifactID::FIRST_AID_TENT))
			return 0;

		// SS 4.12 - once the First Aid Tent gate passes the arm returns a flat 250.
		return FIRST_AID_SKILL_VALUE;

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

	// SS 4.12 - 0x524DD0.  The rule is not "at least as good as anything else": the
	// original sorts every skill the class can still learn by value and asks whether
	// `skill` falls inside the hero's REMAINING FREE SLOTS.  A hero with seven skills
	// keeps only its single best option; a hero with two keeps its top six.
	std::vector<int> order(value.size());
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(),
		[&](int a, int b) { return value[a] > value[b]; });

	int free = MAX_SECONDARY_SKILLS - hero->secSkills.size();

	for(int candidate : order)
	{
		if(hero->getSecSkillLevel(SecondarySkill(candidate)) > 0)
			continue;

		if(candidate == skill.getNum())
			return true;

		if(--free <= 0)
			return false;
	}

	return true;
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
