/*
 * H3ArtifactValue.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3ArtifactValue.h"

#include "H3Constants.h"
#include "H3Player.h"
#include "H3SpellValue.h"
#include "H3Valuations.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/spells/CSpell.h"

#include <algorithm>
#include <cmath>

namespace H3AI
{

namespace
{

int ftol(double v)
{
	return static_cast<int>(v);
}

/// SS 4.9a - every effect class that reads an artifact tests "worn, or the assembled
/// combination set that contains it".  hasArt's searchCombinedParts does exactly that.
bool wears(const CGHeroInstance * hero, int artifact)
{
	return hero->hasArt(ArtifactID(artifact), false, true);
}

const H3ArtifactEffects * effectsFor(const ArtifactID & artifact)
{
	for(const auto & row : H3_ARTIFACT_EFFECTS)
	{
		if(row.artifact == artifact.getNum())
			return &row;
	}

	return nullptr;
}

/// SS 4.9a - the shooter sub-total the Ammo Cart and shooter-bonus classes use.
int64_t shooterAIValue(const CGHeroInstance * hero)
{
	int64_t total = 0;

	for(const auto & slot : hero->Slots())
	{
		const CCreature * creature = hero->getCreature(slot.first);

		if(creature != nullptr && isShooter(creature))
			total += static_cast<int64_t>(creature->getAIValue()) * hero->getStackCount(slot.first);
	}

	return total;
}

/// SS 4.9a - the Spell Scroll arm @ 0x4336F9.  `param` is the spell the scroll carries;
/// VCMI keeps it on the artifact instance, so the caller supplies it.
int spellScrollArtifactValue(H3Context & ctx, const CGHeroInstance * hero, bool equipped)
{
	// TODO(VCMI): the scroll's spell lives on the CArtifactInstance, not on the
	// ArtifactID, so it cannot be recovered from an id alone.  Callers that hold the
	// instance should use aiGetSpellValue(hero, spell) directly, which is the whole
	// body of this arm once the two "already knows it" gates pass.
	(void)ctx; (void)hero; (void)equipped;
	return 0;
}

/// SS 4.9a - the Ballista arm @ 0x43373E.
int ballistaArtifactValue(const CGHeroInstance * hero)
{
	const int attack = std::clamp(hero->getPrimSkillLevel(PrimarySkill::ATTACK), 0, 99) + 1;
	int base = ftol(std::sqrt(static_cast<double>(attack)) * 500.0);

	// hero + 0xDD is Artillery: x1, x1.5, x2, x2.5.
	base += base * hero->getSecSkillLevel(SecondarySkill::ARTILLERY) / 2;

	const int64_t army = armyAIValue(hero);
	const int64_t scaled = army * (primarySkillSum(hero) + 40) / 40;

	if(scaled + base == 0)
		return 0;

	// The harmonic combination the original ends on.
	return static_cast<int>(scaled * base / (scaled + base));
}

/// SS 4.9a - the Ammo Cart arm @ 0x4337D4.
int ammoCartArtifactValue(const CGHeroInstance * hero)
{
	return static_cast<int>(shooterAIValue(hero) / 40);
}

/// SS 4.9a - the First Aid Tent arm @ 0x433837.
int firstAidTentArtifactValue(const CGHeroInstance * hero)
{
	const int heal = ftol(firstAidAmount(hero) * 25.0);
	int best = 0;

	for(const auto & slot : hero->Slots())
	{
		const CCreature * creature = hero->getCreature(slot.first);

		if(creature == nullptr)
			continue;

		const int hp = creature->getMaxHealth();
		const int candidate = heal >= hp
			? creature->getAIValue()
			: static_cast<int>(static_cast<int64_t>(creature->getAIValue()) * heal / std::max(1, hp));

		best = std::max(best, candidate);
	}

	return best;
}

/// SS 4.9a - type_creature_growth_artifact @ 0x432D70.  `mag` is a dwelling index and
/// `aux` the extra creatures per week.
int creatureGrowthArtifactValue(H3Context & ctx, const CGHeroInstance * hero, int mag, int aux, bool cheap)
{
	if(ctx.cb == nullptr)
		return 0;

	auto valueForTown = [&](const CGTownInstance * town) -> int
	{
		if(town == nullptr)
			return 0;

		// The original tests a per-dwelling-level built mask, then the upgraded mask, and
		// picks dwelling slot `mag` or `mag + 7` accordingly (SS 4.9a).
		if(mag < 0 || mag > 6)
			return 0;

		if(!town->hasBuilt(BuildingID(BuildingID::DWELL_LVL_1 + mag)))
			return 0;

		if(town->getOwner() != hero->getOwner())
			return 1;   // someone else's town: the token value 1

		const bool upgraded = town->hasBuilt(BuildingID(BuildingID::DWELL_LVL_1 + 7 + mag));
		const CTown * townType = town->getTown();

		if(townType == nullptr || mag >= static_cast<int>(townType->creatures.size()))
			return 0;

		const auto & tier = townType->creatures[mag];
		const size_t upgrade = (upgraded && tier.size() > 1) ? 1 : 0;

		if(tier.size() <= upgrade)
			return 0;

		const CCreature * creature = tier[upgrade].toCreature();

		return creature == nullptr ? 0 : creature->getAIValue() * aux;
	};

	if(cheap)
	{
		// "the town I am standing in"
		return valueForTown(hero->getVisitedTown());
	}

	int best = 0;

	for(const CGTownInstance * town : ctx.cb->getTownsInfo(true))
		best = std::max(best, valueForTown(town));

	return best;
}

/// SS 4.9a - type_angelic_alliance_artifact @ 0x433130.
int angelicAllianceValue(H3Context & ctx, const CGHeroInstance * hero, int mag, bool cheap)
{
	if(ctx.cb == nullptr)
		return 0;

	// The original sums the AI value of every creature we own whose alignment is already
	// represented, across all heroes and all towns, then adds 5 % of it to an army term.
	int64_t mixed = 0;

	auto scan = [&](const CCreatureSet * army)
	{
		if(army == nullptr)
			return;

		for(const auto & slot : army->Slots())
		{
			const CCreature * creature = army->getCreature(slot.first);

			if(creature == nullptr || isAlignmentFree(creature))
				continue;

			mixed += static_cast<int64_t>(creature->getAIValue()) * army->getStackCount(slot.first);
		}
	};

	for(const CGHeroInstance * h : ctx.cb->getHeroesInfo())
		scan(h);

	for(const CGTownInstance * t : ctx.cb->getTownsInfo(true))
		scan(t);

	const int64_t base = cheap ? 0 : armyAIValue(hero) * mag / 40;

	return static_cast<int>(base + mixed * 5 / 100);
}

/// SS 4.9a - type_statue_of_legion_artifact @ 0x433580: scans the owner's towns for the
/// five Legion parts, whose ids the original keeps in the table at 0x640558.
int statueOfLegionValue(H3Context & ctx, const CGHeroInstance * hero)
{
	static const int LEGION_PARTS[5] = { 118, 119, 120, 121, 122 };

	if(ctx.cb == nullptr)
		return 0;

	int owned = 0;

	for(const CGHeroInstance * h : ctx.cb->getHeroesInfo())
	{
		for(int part : LEGION_PARTS)
		{
			if(wears(h, part))
				++owned;
		}
	}

	(void)hero;
	return owned;
}

/// SS 4.9a - one type_artifact_effect::evaluate call.
int evaluateEffect(H3Context & ctx, const CGHeroInstance * hero,
	const H3ArtifactEffect & e, bool equipped, bool cheap,
	const HeroValuations & valuations)
{
	const int64_t army = armyAIValue(hero);
	const int mag = e.magnitude;

	switch(e.kind)
	{
	case H3ArtifactEffectKind::MIGHT:            // 0x4325A0
		return cheap ? 0 : static_cast<int>(army * mag / 40);

	case H3ArtifactEffectKind::POWER:            // 0x4325E0
		return cheap ? 0 : valuations.valueOfSpellPower * mag;

	case H3ArtifactEffectKind::KNOWLEDGE:        // 0x432610
		return cheap ? 0 : valuations.valueOfKnowledge * mag;

	case H3ArtifactEffectKind::DURATION:         // 0x432860
		return cheap ? 0 : valuations.valueOfSpellDuration * mag;

	case H3ArtifactEffectKind::SCOUTING:         // 0x432510
		return static_cast<int>(static_cast<int64_t>(hero->movementPointsLimit()) * mag / 100);

	case H3ArtifactEffectKind::COMBAT:           // 0x432560
		return static_cast<int>(army * mag / 100);

	case H3ArtifactEffectKind::MOVEMENT:         // 0x4326E0
		return static_cast<int>((army + 2500) * mag / 100);

	case H3ArtifactEffectKind::SPELLCASTER:      // 0x432720
		if(valuations.valueOfSpellPower == 0)
			return 0;

		if(hero->getSecSkillLevel(SecondarySkill::WISDOM) == 0)
			return 0;

		return static_cast<int>(army * mag / 100);

	case H3ArtifactEffectKind::SHOOTER_BONUS:    // 0x4330B0
		return static_cast<int>(shooterAIValue(hero) * mag / 100);

	case H3ArtifactEffectKind::MORALE:           // 0x432780
	{
		if(cheap)
			return 0;

		int current = currentMorale(hero);

		if(equipped)
			current -= mag;

		return ftol(valueOfMorale(current, mag) * static_cast<double>(army));
	}

	case H3ArtifactEffectKind::LUCK:             // 0x4327F0
	{
		if(cheap)
			return 0;

		int current = currentLuck(hero);

		if(equipped)
			current -= mag;

		return ftol(valueOfLuck(current, mag) * static_cast<double>(army));
	}

	case H3ArtifactEffectKind::ANTIMORALE:       // 0x432B20 - Spirit of Oppression
	{
		int v = ftol(valueOfMorale(0, 2) * static_cast<double>(army));

		if(!cheap)
		{
			const int m = currentMorale(hero);

			// Note: the original ADDS our own loss rather than subtracting it.
			if(m > 0)
				v = ftol(valueOfMorale(m, -m) * static_cast<double>(army) + static_cast<double>(v));
		}

		return v;
	}

	case H3ArtifactEffectKind::ANTILUCK:         // 0x432BA0 - Hourglass of the Evil Hour
	{
		int v = ftol(valueOfLuck(0, 2) * static_cast<double>(army));

		if(!cheap)
		{
			const int l = currentLuck(hero);

			if(l > 0)
				v = ftol(valueOfLuck(l, -l) * static_cast<double>(army) + static_cast<double>(v));
		}

		return v;
	}

	case H3ArtifactEffectKind::NECROMANCY:       // 0x432640
	{
		int headroom = ftol((1.0 - necromancyFraction(hero)) * 100.0);

		if(equipped)
		{
			if(headroom > 0)
				headroom = 0;

			headroom += mag;
		}
		else
		{
			headroom = std::max(headroom, mag);
		}

		if(headroom <= 0)
			return 0;

		return static_cast<int>(army * headroom / 250);
	}

	case H3ArtifactEffectKind::UNDEAD_KING_CLOAK: // 0x4333A0
	{
		const int necro = hero->getSecSkillLevel(SecondarySkill::NECROMANCY);

		// Without Necromancy the Cloak is priced exactly like a necromancy artifact of
		// magnitude 30 (the value its constructor is built with).
		H3ArtifactEffect asNecromancy{ H3ArtifactEffectKind::NECROMANCY, 30, 0 };

		const int base = evaluateEffect(ctx, hero, asNecromancy, equipped, cheap, valuations);

		if(necro == 0)
			return base;

		// With Necromancy it prices the UPGRADE of the raised creature instead:
		// basic -> Walking Dead (58), advanced -> Wight (60), expert -> Lich (64).
		static const int raised[4] = { 0, 58, 60, 64 };
		const CCreature * skeleton = CreatureID(56).toCreature();
		const CCreature * target = CreatureID(raised[std::clamp(necro, 1, 3)]).toCreature();

		if(skeleton == nullptr || target == nullptr || skeleton->getAIValue() == 0)
			return base;

		const double ratio = static_cast<double>(target->getAIValue() - skeleton->getAIValue())
			/ static_cast<double>(skeleton->getAIValue());

		return ftol(ratio * static_cast<double>(base));
	}

	case H3ArtifactEffectKind::ANTIMAGIC:        // 0x432A50
	{
		int v = mag == 0 ? static_cast<int>(army / 5) : static_cast<int>(army / 8);

		if(cheap || !equipped)
			return v;

		const int sp = std::clamp(hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 1, 99);

		// It silences us as well as them.
		return v - sp * (mag == 0 ? 50 : 25);
	}

	case H3ArtifactEffectKind::INCOME:           // 0x432D20
	{
		if(ctx.player == nullptr)
			return 0;

		return ftol(static_cast<double>(mag) * ctx.player->resourceValue(GameResID(e.aux)) * 3.0);
	}

	case H3ArtifactEffectKind::SPELL:            // 0x432F90 - Titan's Thunder
	{
		if(cheap)
			return 0;

		const SpellID granted(mag);

		if(hero->spellbookContainsSpell(granted))
			return 0;

		if(!equipped && hero->canCastThisSpell(granted.toSpell()))
			return 0;

		H3SpellValue sv(hero);

		if(!sv.valid())
			return 0;

		return sv.valueOfSpell(granted);
	}

	case H3ArtifactEffectKind::TOME:             // 0x432C20 - the four Tomes
	{
		if(cheap)
			return 0;

		H3SpellValue sv(hero);

		if(!sv.valid())
			return 0;

		int best = 0;

		for(int id = 0; id < H3_SPELL_COUNT; ++id)
		{
			const SpellID spell(id);

			if(hero->spellbookContainsSpell(spell))
				continue;

			if(!equipped && hero->canCastThisSpell(spell.toSpell()))
				continue;

			if(!spellInSchoolMask(spell, e.aux))
				continue;

			best = std::max(best, sv.valueOfSpell(spell));
		}

		return best;
	}

	case H3ArtifactEffectKind::SCHOOL:           // 0x432890 - the four Orbs
	{
		if(cheap)
			return 0;

		H3SpellValue sv(hero);

		if(!sv.valid())
			return 0;

		const int sp = std::clamp(hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 1, 99);
		int lo = sp;
		int hi = sp;

		if(equipped)
			lo = 100 * sp / (mag + 100);   // strip our own boost back off
		else
			hi = sp * (mag + 100) / 100;

		int acc = 0;

		for(int id = 0; id < H3_SPELL_COUNT; ++id)
		{
			const SpellID spell(id);

			if(!hero->canCastThisSpell(spell.toSpell()))
				continue;

			if(!spellInSchoolMask(spell, e.aux))
				continue;

			// spellTraits + 0x0C bit 9: the spell scales with power at all.
			if(H3_SPELLS[id].power == 0)
				continue;

			sv.spellPower = lo;
			const int before = sv.valueOfSpell(spell);
			sv.spellPower = hi;
			const int after = sv.valueOfSpell(spell);
			sv.spellPower = sp;

			const int delta = after - before;

			acc = mag < 0 ? std::min(acc, delta) : std::max(acc, delta);
		}

		return acc;
	}

	case H3ArtifactEffectKind::CREATURE_GROWTH:  // 0x432D70 - the Legion set and the Grail
		return creatureGrowthArtifactValue(ctx, hero, mag, e.aux, cheap);

	case H3ArtifactEffectKind::ANGELIC_ALLIANCE: // 0x433130
		return angelicAllianceValue(ctx, hero, mag, cheap);

	case H3ArtifactEffectKind::ELIXIR_OF_LIFE:   // 0x433520
	{
		int64_t total = 0;

		for(const auto & slot : hero->Slots())
		{
			const CCreature * creature = hero->getCreature(slot.first);

			// traits + 0x10 bit 4 = "living": the creatures the Elixir can help.
			if(creature != nullptr && isLiving(creature))
				total += static_cast<int64_t>(creature->getAIValue()) * hero->getStackCount(slot.first);
		}

		return static_cast<int>(total / 8);
	}

	case H3ArtifactEffectKind::STATUE_OF_LEGION: // 0x433580
		return statueOfLegionValue(ctx, hero);
	}

	return 0;
}

}

int artifactValueForHero(H3Context & ctx, const CGHeroInstance * hero,
	const ArtifactID & artifact, bool equipped, bool cheap)
{
	// SS 4.9a - AI_get_value_of_artifact @ 0x4336C0.
	if(hero == nullptr || artifact == ArtifactID::NONE)
		return 0;

	// The four hard-coded arms come first; they never touch the effect table.
	switch(artifact.getNum())
	{
	case ArtifactID::SPELL_SCROLL:
		return spellScrollArtifactValue(ctx, hero, equipped);

	case ArtifactID::BALLISTA:
		return ballistaArtifactValue(hero);

	case ArtifactID::AMMO_CART:
		return ammoCartArtifactValue(hero);

	case ArtifactID::FIRST_AID_TENT:
		return firstAidTentArtifactValue(hero);

	default:
		break;
	}

	int value = 0;

	// SS 4C.3 - the victory-condition override, conditions 0 and 10.
	if((ctx.victory.condition == H3VictoryCondition::ACQUIRE_ARTIFACT
			|| ctx.victory.condition == H3VictoryCondition::TRANSPORT_ARTIFACT)
		&& ctx.victory.artifact == artifact)
	{
		value = VICTORY_CONDITION_OVERRIDE;
	}

	const H3ArtifactEffects * row = effectsFor(artifact);

	// SS 4.9b - the +1-stat valuations are the same for every effect on this hero, and
	// computing them means running five 70-spell probes.  Hoisted out of the loop: the
	// original reads them straight out of the hero record, where AI_update_valuations
	// left them once per turn.
	const HeroValuations valuations = computeHeroValuations(hero);

	if(row != nullptr)
	{
		for(int i = 0; i < row->count; ++i)
			value += evaluateEffect(ctx, hero, row->effects[i], equipped, cheap, valuations);
	}

	// SS 4.9a - a combination artifact also carries the effects of every member of its
	// set.  VCMI expresses membership through the artifact's own part list.
	const CArtifact * art = artifact.toArtifact();

	if(art != nullptr)
	{
		for(const auto & part : art->getConstituents())
		{
			const H3ArtifactEffects * partRow = effectsFor(part->getId());

			if(partRow == nullptr)
				continue;

			for(int i = 0; i < partRow->count; ++i)
				value += evaluateEffect(ctx, hero, partRow->effects[i], equipped, cheap, valuations);
		}
	}

	return value;
}

int totalArtifactValue(H3Context & ctx, const CGHeroInstance * hero,
	const ArtifactID & artifact, bool equipped)
{
	// SS 4.9a - hero::total_artifact_value @ 0x4339E0.
	if(hero == nullptr)
		return 0;

	int gain = artifactValueForHero(ctx, hero, artifact, equipped, false);

	if(gain < 0)
		gain = 0;

	const CArtifact * art = artifact.toArtifact();

	if(art == nullptr)
		return gain;

	// Does a free slot exist?  If not, the backpack value is the whole story.
	bool slotFree = false;

	for(const auto & slot : art->getPossibleSlots().at(ArtBearer::HERO))
	{
		if(hero->getArt(slot) == nullptr)
		{
			slotFree = true;
			break;
		}
	}

	if(slotFree)
		return gain;

	// A slot exists but is taken: the artifact already there must be given up.  The
	// original's loop writes `loss` unconditionally rather than accumulating or
	// maximising, so the LAST occupied slot wins.  That is the shipped behaviour.
	int loss = 0;

	for(const auto & slot : art->getPossibleSlots().at(ArtBearer::HERO))
	{
		const CArtifactInstance * worn = hero->getArt(slot);

		if(worn == nullptr)
			continue;

		loss = artifactValueForHero(ctx, hero, worn->getTypeId(), true, false);
	}

	const int net = gain - loss;

	return net > 0 ? net : 0;
}

int artifactValueForPlayer(H3Context & ctx, const ArtifactID & artifact)
{
	// SS 4.9a - AI_get_value_of_artifact @ 0x433AA0: the most any of our heroes would
	// gain, floored at 10.
	if(ctx.cb == nullptr || artifact == ArtifactID::NONE)
		return 0;

	int best = 10;

	for(const CGHeroInstance * hero : ctx.cb->getHeroesInfo())
		best = std::max(best, totalArtifactValue(ctx, hero, artifact, false));

	return best;
}

}
