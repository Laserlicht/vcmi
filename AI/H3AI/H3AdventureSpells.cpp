/*
 * H3AdventureSpells.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3AdventureSpells.h"

#include "H3Constants.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/adventure/AdventureSpellEffect.h"
#include "../../lib/spells/adventure/DimensionDoorEffect.h"
#include "../../lib/spells/adventure/SummonBoatEffect.h"

#include <algorithm>
#include <limits>

namespace H3AI
{

namespace
{
/// Chebyshev distance: the adventure map is 8-connected, so this is the number of steps
/// an unobstructed hero needs.
int stepsBetween(const int3 & a, const int3 & b)
{
	if(a.z != b.z)
		return std::numeric_limits<int>::max();

	return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

/// Every adventure spell this hero could cast right now, cheapest first - the order the
/// original's spell book is walked in matters only in that a cheaper spell is preferred
/// when two would do.
std::vector<const CSpell *> castableAdventureSpells(H3Context & ctx, const CGHeroInstance * hero)
{
	std::vector<const CSpell *> out;

	for(const SpellID & known : hero->getSpellsInSpellbook())
	{
		const CSpell * spell = known.toSpell();

		if(spell == nullptr || !spell->isAdventure())
			continue;

		if(!hero->canCastThisSpell(spell) || hero->mana < hero->getSpellCost(spell))
			continue;

		spells::detail::ProblemImpl problem;

		if(!spell->getAdventureMechanics().canBeCast(problem, ctx.cb, hero))
			continue;

		out.push_back(spell);
	}

	std::sort(out.begin(), out.end(), [hero](const CSpell * a, const CSpell * b)
	{
		return hero->getSpellCost(a) < hero->getSpellCost(b);
	});

	return out;
}

/// SS 4F.4 - the Dimension Door landing: whichever legal target inside the spell's own
/// range gets the hero closest to where it wants to be.
int3 chooseDimensionDoorTarget(H3Context & ctx, const CGHeroInstance * hero, const CSpell * spell, const int3 & destination)
{
	const IAdventureSpellMechanics & mechanics = spell->getAdventureMechanics();
	const auto * ranged = mechanics.getEffectAs<AdventureSpellRangedEffect>(hero);

	if(ranged == nullptr)
		return int3(-1, -1, -1);

	// The spell is cast from the hero's sight centre, and that is also where the range is
	// measured from - the same source canBeCastAt uses.
	const int3 from = hero->getSightCenter();
	const int rangeX = ranged->getRangeX();
	const int rangeY = ranged->getRangeY();

	int3 best(-1, -1, -1);
	int bestDistance = stepsBetween(from, destination);

	for(int dy = -rangeY; dy <= rangeY; ++dy)
	{
		for(int dx = -rangeX; dx <= rangeX; ++dx)
		{
			const int3 probe(from.x + dx, from.y + dy, from.z);

			if(!ctx.cb->isInTheMap(probe))
				continue;

			const int distance = stepsBetween(probe, destination);

			if(distance >= bestDistance)
				continue;

			spells::detail::ProblemImpl problem;

			if(!mechanics.canBeCastAt(problem, ctx.cb, hero, probe))
				continue;

			bestDistance = distance;
			best = probe;
		}
	}

	return best;
}
}

bool castAdventureSpells(H3Context & ctx, const CGHeroInstance * hero, const int3 & destination)
{
	if(hero == nullptr || !hero->hasSpellbook())
		return false;

	const TerrainTile * target = ctx.openMap
		? ctx.cb->getTileUnchecked(destination)
		: ctx.cb->getTile(destination, false);

	const bool overWater = target != nullptr && target->isWater();
	const bool afloat = hero->inBoat();

	const std::vector<const CSpell *> castable = castableAdventureSpells(ctx, hero);

	const auto findByBonus = [&castable, hero](BonusType bonus) -> const CSpell *
	{
		for(const CSpell * spell : castable)
			if(spell->getAdventureMechanics().givesBonus(hero, bonus))
				return spell;

		return nullptr;
	};

	const auto findByEffect = [&castable, hero](auto tag) -> const CSpell *
	{
		using EffectType = typename decltype(tag)::type;

		for(const CSpell * spell : castable)
			if(spell->getAdventureMechanics().getEffectAs<EffectType>(hero) != nullptr)
				return spell;

		return nullptr;
	};

	// SS 4F - Summon Boat and Water Walk are the two answers to "the goal is across
	// water".  A hero already at sea needs neither.
	if(overWater && !afloat)
	{
		const CSpell * waterWalk = findByBonus(BonusType::WATER_WALKING);

		if(waterWalk != nullptr)
		{
			ctx.cb->castSpell(hero, waterWalk->getId());
			return true;
		}

		const CSpell * summonBoat = findByEffect(std::type_identity<SummonBoatEffect>{});

		if(summonBoat != nullptr)
		{
			ctx.cb->castSpell(hero, summonBoat->getId());
			return true;
		}
	}

	// SS 4F - Fly answers everything else the ground route cannot: it makes the whole
	// level traversable for the rest of the day.  Casting it while afloat would strand
	// the boat, so a sailing hero does not.
	if(!afloat)
	{
		const CSpell * fly = findByBonus(BonusType::FLYING_MOVEMENT);

		if(fly != nullptr)
		{
			ctx.cb->castSpell(hero, fly->getId());
			return true;
		}
	}

	// SS 4F.4 - Dimension Door, with the AI's mana reserve: it jumps only while it can
	// still afford something else afterwards.  The per-day cast limit is the engine's,
	// and asking past it would only earn a refusal.
	const CSpell * dimensionDoor = findByEffect(std::type_identity<DimensionDoorEffect>{});

	if(dimensionDoor == nullptr)
		return false;

	if(hero->mana < hero->getSpellCost(dimensionDoor) + DIMENSION_DOOR_MANA_RESERVE)
		return false;

	const IAdventureSpellMechanics & mechanics = dimensionDoor->getAdventureMechanics();

	if(mechanics.getCastsAlreadyPerformed(hero) >= mechanics.getCastsLimit(hero, ctx.cb->getMapSize()))
		return false;

	const int3 landing = chooseDimensionDoorTarget(ctx, hero, dimensionDoor, destination);

	if(!landing.isValid())
		return false;

	ctx.cb->castSpell(hero, dimensionDoor->getId(), landing);

	return true;
}

}
