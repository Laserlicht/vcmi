/*
 * H3SpellValue.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3SpellValue.h"

#include "H3Valuations.h"

#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/bonuses/Bonus.h"

#include <algorithm>
#include <cmath>

namespace H3AI
{

namespace
{

/// The original truncates every floating result with ftol (round toward zero).
int ftol(double v)
{
	return static_cast<int>(v);
}

/// SS 4.9b - the zero case of armyGroup::spell_effect_fraction (0x44A4D0), which is the
/// only part of it any adventure-side caller tests.  Outside a battle VCMI answers the
/// same question through the creature's immunity bonuses.
bool isImmuneToSpell(const CCreature * creature, const CSpell * spell)
{
	if(creature == nullptr || spell == nullptr)
		return false;

	if(creature->hasBonusOfType(BonusType::SPELL_IMMUNITY, BonusSubtypeID(SpellID(spell->getIndex()))))
		return true;

	if(creature->valOfBonuses(BonusType::LEVEL_SPELL_IMMUNITY) >= spell->getLevel())
		return true;

	bool immune = false;

	spell->forEachSchool([&](const SpellSchool & school, bool & stop)
	{
		if(creature->hasBonusOfType(BonusType::SPELL_SCHOOL_IMMUNITY, BonusSubtypeID(school)))
		{
			immune = true;
			stop = true;
		}
	});

	return immune;
}

const H3SpellInfo * spellInfo(const SpellID & spell)
{
	const int id = spell.getNum();

	if(id < 0 || id >= H3_SPELL_COUNT)
		return nullptr;

	return &H3_SPELLS[id];
}

}

H3SpellValue::H3SpellValue(const CGHeroInstance * heroIn)
	: hero(heroIn)
{
	// SS 4.9b - 0x526D40.  Two artifact gates before anything else is computed; when
	// either fires every field stays 0 and valid() is false.
	if(hero == nullptr || !hero->hasSpellbook())
		return;

	// hasArt's third argument is "search combined parts", which is exactly the
	// original's "worn, or the assembled set that contains it" test (SS 4.9a).
	if(hero->hasArt(ArtifactID(H3_ARTIFACT_ORB_OF_INHIBITION), false, true))
		return;

	armyValue = armyAIValue(hero);
	spellPower = std::clamp(hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 1, 99);

	// this + 0x0C = spellPower + hero::spell_duration_bonus (0x4E4DB0).  VCMI models the
	// same thing as a SPELL_DURATION bonus.
	duration = spellPower + hero->valOfBonuses(BonusType::SPELL_DURATION, BonusSubtypeID());

	// this + 0x10 = ftol(mana multiplier * knowledge * 10).  VCMI's manaLimit() applies
	// exactly that product, including the Intelligence/Mysticism multipliers.
	mana = hero->manaLimit();

	// SS 4.9b - the constructor pushes one 12-byte record per occupied slot and then
	// sorts them.  The mass-effect arm reads "the first N" off the sorted list, so the
	// order is part of the specification, not an implementation detail.
	for(const auto & slot : hero->Slots())
	{
		const CCreature * creature = hero->getCreature(slot.first);

		if(creature == nullptr)
			continue;

		Stack s;
		s.creature = creature->getId().getNum();
		s.count = hero->getStackCount(slot.first);
		s.totalValue = static_cast<int64_t>(creature->getAIValue()) * s.count;
		stacks.push_back(s);
	}

	std::sort(stacks.begin(), stacks.end(),
		[](const Stack & a, const Stack & b) { return a.totalValue < b.totalValue; });
}

int H3SpellValue::valueOfBuff(const SpellID & spell, int schoolLevel, int casts, int64_t reference) const
{
	// SS 4.9b - value_of_buff @ 0x5270E0.
	const H3SpellInfo * info = spellInfo(spell);

	if(info == nullptr || reference <= 0)
		return 0;

	int amount = info->effect[schoolLevel] * (spellPower + schoolLevel);

	// SS 5B.4 - the one hard-coded exception: Titan's Lightning Bolt reads its flat
	// damage straight out of spellTraits[57] + 0x34 instead of scaling with power.
	if(spell.getNum() == SpellID::TITANS_LIGHTNING_BOLT)
		amount = H3_SPELLS[SpellID::TITANS_LIGHTNING_BOLT].effect[0];

	// The original passes the magnitude through hero::spell_effect_amount (0x4E5760)
	// before scaling.  That routine applies the caster's per-spell bonuses, and VCMI's
	// equivalent (Mechanics::applySpellEffects) needs a live battle, which the adventure
	// AI does not have.  The magnitude is used unmodified here; for every spell without
	// a hero specialty the two agree, and the difference is a scale factor on one term
	// rather than a change of shape.  Stated rather than approximated with a guess.
	const double x = static_cast<double>(amount * 10) / static_cast<double>(reference);

	// Locate x in the descending breakpoint column, then take that row's linear segment.
	int row = 0;

	while(row < 13 && x < H3_CAST_CURVE[row][1])
		++row;

	const double linear = x * H3_CAST_CURVE[row][2] + H3_CAST_CURVE[row][3];
	const double saturating = x * H3_CAST_CURVE[std::min(casts, 13)][0];

	return ftol(static_cast<double>(reference) * std::min(linear, saturating));
}

int H3SpellValue::valueOfSpell(const SpellID & spell) const
{
	// SS 4.9b - type_spellvalue::value_of_spell @ 0x5273D0.
	const H3SpellInfo * info = spellInfo(spell);
	const CSpell * spellData = spell.toSpell();

	if(info == nullptr || spellData == nullptr)
		return 0;

	const int schoolLevel = std::clamp(hero->getSpellSchoolLevel(spellData), 0, 3);
	const int cost = hero->getSpellCost(spellData);

	// A spell the hero cannot pay for even once is worth nothing at all.
	if(cost > mana)
		return 0;

	const int casts = cost > 0 ? mana / cost : 1000;

	switch(info->category)
	{
	case (unsigned)H3SpellCategory::DIRECT_DAMAGE:
		return valueOfBuff(spell, schoolLevel, casts, armyValue);

	case (unsigned)H3SpellCategory::OPENING_DAMAGE:
		// The same curve, but priced as if it could only ever be cast once.
		return valueOfBuff(spell, schoolLevel, 1, armyValue);

	case (unsigned)H3SpellCategory::HITS_BOTH_SIDES:
	{
		// SS 4.9b - 0x5271C0.  This category is the Armageddon family: the damage lands
		// on the caster's own army too, so what is being measured is how much of that
		// army is IMMUNE.  A caster whose troops are all immune keeps the full buff
		// term; one whose troops are vulnerable lands at -armyValue and is clamped to 0.
		int64_t immune = 0;

		for(const auto & slot : hero->Slots())
		{
			const CCreature * creature = hero->getCreature(slot.first);

			if(creature == nullptr)
				continue;

			// armyGroup::spell_effect_fraction (0x44A4D0) returns EFFECTIVENESS:
			// 1.0 fully affected, 0.0 immune.  Only its zero case matters here, and
			// outside a battle that reduces to the three immunity bonuses.
			if(isImmuneToSpell(creature, spellData))
			{
				immune += static_cast<int64_t>(creature->getAIValue())
					* hero->getStackCount(slot.first);
			}
		}

		if(immune == 0)
			return 0;

		const int64_t net = immune - armyValue + valueOfBuff(spell, schoolLevel, casts, immune);

		return net < 0 ? 0 : static_cast<int>(net);
	}

	case (unsigned)H3SpellCategory::ENCHANTMENT:
	{
		// SS 4.9b - 0x527290, the mass-effect arm.  The original asks
		// spell_affects_all_at_level (0x59E060); VCMI marks a spell that takes no target
		// selection as AimType::NOTHING, which is the same set.
		const bool mass = spellData->getTargetType() == spells::AimType::NOTHING;

		int n = duration * casts;
		int limit = 0;

		if(mass)
		{
			limit = n < 7 ? 1 : std::min(n / 7, duration);
			n = 7;
		}
		else
		{
			limit = static_cast<int>(stacks.size());
		}

		int64_t acc = 0;
		const int upTo = std::min(static_cast<int>(stacks.size()), limit);

		for(int i = 0; i < upTo; ++i)
		{
			// st->d[0x00] > 0 gates an immunity test; VCMI answers the same question
			// through the spell's own immunity check.
			acc += static_cast<int64_t>(info->aiValue[schoolLevel]) * stacks[i].totalValue * n;
		}

		return static_cast<int>(acc / H3_SPELL_MASS_DIVISOR);
	}

	case (unsigned)H3SpellCategory::RESURRECTION:
	{
		// SS 4.9b - the damage/heal curve at 0x527519.
		const int power = spellPower + schoolLevel;
		const int magnitude = info->effect[schoolLevel] * power;

		if(armyValue <= 0)
			return 0;

		double fraction = static_cast<double>(magnitude * 10) / static_cast<double>(armyValue);

		if(!(fraction <= H3_SPELL_DAMAGE_FRACTION_CAP))
			fraction = H3_SPELL_DAMAGE_FRACTION_CAP;

		fraction *= H3_CAST_CURVE[std::min(casts, 13)][0];

		if(!(fraction <= H3_SPELL_DAMAGE_TOTAL_CAP))
			fraction = H3_SPELL_DAMAGE_TOTAL_CAP;

		return ftol(static_cast<double>(armyValue) * fraction);
	}

	case (unsigned)H3SpellCategory::ADVENTURE:
	{
		// SS 4.9b - the utility arm at 0x5274D2.
		const double r = std::sqrt(static_cast<double>(casts)) * H3_SPELL_UTILITY_SLOPE
			+ H3_SPELL_UTILITY_BASE;

		return ftol(r * static_cast<double>(info->aiValue[schoolLevel] * armyValue));
	}

	default:
		// Including Titan's Lightning Bolt's 0x108000, which matches no arm.
		return H3_SPELL_TOKEN_VALUE;
	}
}

int H3SpellValue::bestSpellValue(unsigned categoryMask) const
{
	// SS 4.9b - get_best_spell_value @ 0x5275B0.
	if(spellPower == 0)
		return 0;

	const bool recanters = hero->hasArt(ArtifactID(H3_ARTIFACT_RECANTERS_CLOAK), false, true);
	int best = 0;

	for(int id = 0; id < H3_SPELL_COUNT; ++id)
	{
		const SpellID spell(id);

		// hero + 0x430: the spells AVAILABLE to the hero, which includes those granted
		// by artifacts, not only the ones written in the spell book.
		if(!hero->canCastThisSpell(spell.toSpell()))
			continue;

		if((H3_SPELLS[id].category & categoryMask) == 0)
			continue;

		// Recanter's Cloak caps both heroes at spell level 2.
		if(recanters && H3_SPELLS[id].level > 2)
			continue;

		best = std::max(best, valueOfSpell(spell));
	}

	return best;
}

int aiGetSpellValue(const CGHeroInstance * hero, const SpellID & spell)
{
	// SS 4.9b - AI_get_spell_value @ 0x527640.
	H3SpellValue ctx(hero);

	if(!ctx.valid())
		return 0;

	const int value = ctx.valueOfSpell(spell);
	const H3SpellInfo * info = spellInfo(spell);

	if(info == nullptr)
		return 0;

	const unsigned group = info->category & H3_SPELL_COMPETING_GROUP;

	// Outside the competing group nothing else can substitute for it.
	if(group == 0)
		return value;

	const int best = ctx.bestSpellValue(group);

	// Strictly worse than what we already have: the token value, not zero.
	if(best >= value)
		return 1;

	return value - best;
}

}
